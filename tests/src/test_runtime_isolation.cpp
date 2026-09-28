#include <metasequoia/session.h>
#include "../../core/data_path.h"
#include "../../core/input_session.h"
#include "../../core/pinyin_decoder.h"
#include "../../japanese/japanese_sentence_decoder.h"
#include "../../contracts/assets/assets.h"
#include "../../user_dictionary/user_dictionary_journal.h"
#include "test_directory_cleanup.h"
#include <sqlite3.h>
#include <algorithm>
#include <chrono>
#include <fstream>
#include <future>
#include <stdexcept>

namespace
{
using namespace metasequoia;
void require(bool value, const char *message)
{
    if (!value)
        throw std::runtime_error(message);
}
void execute(const std::filesystem::path &path, const std::string &sql)
{
    sqlite3 *db = nullptr;
    if (sqlite3_open(path_to_utf8(path).c_str(), &db) != SQLITE_OK)
        throw std::runtime_error("fixture open failed");
    const int result = sqlite3_exec(db, sql.c_str(), nullptr, nullptr, nullptr);
    const std::string error = sqlite3_errmsg(db);
    sqlite3_close(db);
    if (result != SQLITE_OK)
        throw std::runtime_error(error);
}
std::string bytes(const std::filesystem::path &path)
{
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}
void make_japanese_model(const std::filesystem::path &path, const std::string &surface)
{
    // One-token MSJPDT1 fixture, with explicit little-endian fields rather than native packing.
    std::ofstream output(path, std::ios::binary);
    const auto integer = [&](std::uint64_t value, int width) {
        for (int i = 0; i < width; ++i)
            output.put(static_cast<char>((value >> (i * 8)) & 255));
    };
    const std::string reading = "かな";
    output.write("MSJPDT1", 8);
    integer(1, 4);
    integer(1, 4);
    integer(1, 4);
    integer(0, 4);
    integer(56, 8);
    integer(76, 8);
    integer(78, 8);
    integer(reading.size() + surface.size(), 8);
    integer(0, 4);
    integer(reading.size(), 2);
    integer(reading.size(), 4);
    integer(surface.size(), 2);
    integer(0, 2);
    integer(0, 2);
    integer(1, 4);
    integer(0, 2);
    output << reading << surface;
}
void make_resources(const std::filesystem::path &path, const std::string &word)
{
    std::filesystem::create_directories(path / "helpcodes");
    execute(path / assets::main_dictionary, "CREATE TABLE tbl_1_n(key TEXT,jp TEXT,value TEXT,weight INTEGER);"
                                            "INSERT INTO tbl_1_n VALUES('ni','n','" +
                                                word +
                                                "',10000),('ni','n','拟',9000);"
                                                "CREATE TABLE quick_parases(key TEXT,value TEXT,weight INTEGER);"
                                                "INSERT INTO quick_parases VALUES('x','" +
                                                word + "短语',10);");
    require(EnglishDictionary::ensure_schema(path_to_utf8(path / assets::english_dictionary)),
            "English fixture failed");
    execute(path / assets::english_dictionary,
            "INSERT INTO english_words(word,display,weight) VALUES('hello','hello',10);");
    std::ofstream(path / assets::translations) << "hello\t" << word << "翻译\n";
    std::ofstream(path / assets::helpcode_lantian) << word << "=aa\n拟=cc\n";
    std::ofstream(path / assets::helpcode_xiaohe) << word << "=cc\n拟=aa\n";
}
void type(Session &session, const std::string &input)
{
    for (char ch : input)
        session.character(ch);
}
} // namespace

void test_runtime_isolation()
{
    using namespace metasequoia;
    const auto root = std::filesystem::temp_directory_path() /
                      ("msime-runtime-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    test::ScopedDataDirectoryCleanup cleanup(root);
    const auto resource_a = root / std::filesystem::u8path("资源甲");
    const auto resource_b = root / std::filesystem::u8path("资源乙");
    make_resources(resource_a, "你");
    make_resources(resource_b, "妮");
    make_japanese_model(resource_a / assets::japanese_model, "甲");
    make_japanese_model(resource_b / assets::japanese_model, "乙");
    {
        const auto path = root / "mapped-japanese.dat";
        make_japanese_model(path, "甲");
        japanese::JapaneseSentenceDecoder original(path_to_utf8(path));
        require(original.ready() && original.ExactLemmas("かな").front().surface == "甲",
                "Japanese model mapping did not load");
        const auto replacement = root / "replacement-japanese.dat";
        make_japanese_model(replacement, "乙");
        // Publishers replace files, never truncate mapped inodes in place.
        std::filesystem::remove(path);
        std::filesystem::rename(replacement, path);
        japanese::JapaneseSentenceDecoder updated(path_to_utf8(path));
        require(updated.ready() && updated.ExactLemmas("かな").front().surface == "乙" &&
                    original.ExactLemmas("かな").front().surface == "甲",
                "Replacing a model altered an existing decoder");
        const auto valid = bytes(path);
        const auto bad = root / "invalid-japanese.dat";
        const auto rejected = [&](const std::string &data) {
            {
                std::ofstream output(bad, std::ios::binary);
                output.write(data.data(), data.size());
            }
            japanese::JapaneseSentenceDecoder decoder(path_to_utf8(bad));
            require(!decoder.ready() && decoder.ExactLemmas("かな").empty(), "Invalid Japanese model accepted");
        };
        for (size_t length : {size_t(0), size_t(55), size_t(77), valid.size() - 1})
            rejected(valid.substr(0, length));
        auto invalid_offset = valid;
        invalid_offset.replace(24, 8, 8, char(0xff));
        rejected(invalid_offset);
        auto invalid_reading = valid;
        invalid_reading[60] = char(0xff);
        invalid_reading[61] = char(0xff);
        rejected(invalid_reading);
    }
    const auto reject_overlap = [&](const std::filesystem::path &resources, const std::filesystem::path &user,
                                    const std::filesystem::path &cache) {
        bool rejected = false;
        try
        {
            (void)prepare_runtime_paths(resources, user, cache, "overlap");
        }
        catch (const std::invalid_argument &)
        {
            rejected = true;
        }
        require(rejected, "Overlapping runtime roots were accepted");
        require(!std::filesystem::exists(user / "dictionaries/overlap"),
                "Rejected runtime roots still published a generation");
    };
    reject_overlap(resource_a, root / "same-user-cache", root / "same-user-cache");
    reject_overlap(resource_a, root / "cache-parent/user", root / "cache-parent");
    reject_overlap(resource_a, root / "user-parent", root / "user-parent/cache");
    reject_overlap(resource_a, resource_a / "user", root / "separate-cache");
    reject_overlap(resource_a, root / "separate-user", resource_a / "cache");
    reject_overlap(resource_a, root, root / "separate-cache");
    require(!std::filesystem::exists(root / "same-user-cache") && !std::filesystem::exists(root / "cache-parent") &&
                !std::filesystem::exists(resource_a / "user") && !std::filesystem::exists(resource_a / "cache"),
            "Path validation created directories before rejecting overlap");
#ifndef _WIN32
    const auto resource_alias = root / "resource-alias";
    std::filesystem::create_directory_symlink(resource_a, resource_alias);
    reject_overlap(resource_a, root / "alias-user", resource_alias / "cache");
#endif
    const auto original = bytes(resource_a / assets::main_dictionary);
    const auto paths_a = prepare_runtime_paths(resource_a, root / "user-a", root / "cache-a", "v1");
    const auto paths_b = prepare_runtime_paths(resource_b, root / "user-b", root / "cache-b", "v1");
    {
        SessionOptions options;
        options.paths = paths_a;
        options.learning = false;
        Session session(options);
        Session independent(options);
        type(session, "ni");
        session.command(Command::MoveLeft);
        const auto before = session.snapshot();
        session.set_chinese_punctuation_enabled(false);
        session.set_chinese_punctuation_enabled(false);
        const auto unchanged = session.snapshot();
        require(unchanged.editing_text == before.editing_text && unchanged.preedit == before.preedit &&
                    unchanged.caret_position == before.caret_position &&
                    unchanged.candidates.size() == before.candidates.size(),
                "Live punctuation mode changed composition or caret");
        const auto ignored = session.punctuation(',');
        require(!ignored.handled && !ignored.commit && session.snapshot().editing_text == before.editing_text,
                "ASCII punctuation mode consumed composition");
        require(independent.punctuation(',').commit == std::optional<std::string>("，"),
                "Live punctuation mode leaked to another session");
        session.command(Command::Cancel);
        session.set_chinese_punctuation_enabled(true);
        require(session.punctuation('"').commit == std::optional<std::string>("“"),
                "Chinese punctuation did not reopen");
        session.set_chinese_punctuation_enabled(false);
        require(!session.punctuation('"').handled, "Disabled quote was consumed");
        session.set_chinese_punctuation_enabled(true);
        require(session.punctuation('"').commit == std::optional<std::string>("”"),
                "Punctuation mode reset quote pairing");
    }
    {
        SessionOptions options;
        options.paths = paths_a;
        options.scheme = SchemeType::Shuangpin;
        options.shuangpin_profile = GetMicrosoftShuangpinProfile();
        options.learning = false;
        Session session(options);
        require(session.snapshot().shuangpin_profile == "microsoft", "Snapshot lost applied profile");
        require(session.snapshot().nine_key_spellings.empty(), "Profile leaked into nine-key spellings");
        require(session.segment_raw_boundaries().empty(), "Idle session reported input units");
        for (const auto &fixture : std::vector<std::pair<std::string, std::vector<std::size_t>>>{
                 {"b;ni", {0, 2, 4}}, {"nihkb;", {0, 2, 4, 6}}, {"nihcb;", {0, 2, 3, 5, 6}}})
        {
            type(session, fixture.first);
            require(session.segment_raw_boundaries() == fixture.second, "Microsoft raw units disagree with parsing");
            session.command(Command::Cancel);
        }
        require(!session.character(';').handled, "Microsoft semicolon started a syllable");
        session.character('n');
        const auto ing = session.character(';');
        require(ing.handled && !ing.commit && session.snapshot().preedit == "n;",
                "Public Session rejected Microsoft ing final");
        const auto query = session.online_query();
        require(query && query->query_text == "ning", "Microsoft ing lost its canonical query");
        require(!session.character(';').handled && session.snapshot().preedit == "n;",
                "Microsoft semicolon accepted outside the final position");
        session.command(Command::Backspace);
        require(session.snapshot().preedit == "n", "Microsoft final could not be erased");
        session.command(Command::Cancel);
        type(session, "ni'n");
        require(session.character(';').handled && session.snapshot().preedit == "ni'n;",
                "Microsoft final after a manual separator was rejected");
        session.command(Command::MoveLeft);
        session.command(Command::DeleteForward);
        // Leave a suffix so the replacement exercises the middle-edit path.
        session.command(Command::MoveEnd);
        type(session, "ni");
        session.command(Command::MoveLeft);
        session.command(Command::MoveLeft);
        require(session.character(';').handled && session.snapshot().editing_text == "ni'n;ni",
                "Microsoft ing could not be inserted before a suffix");
        session.command(Command::Cancel);
        type(session, "ni'");
        require(!session.character(';').handled && session.snapshot().preedit == "ni'",
                "Microsoft separator was counted as a syllable initial");
        options.shuangpin_profile = GetXiaoheShuangpinProfile();
        Session xiaohe(options);
        xiaohe.character('n');
        require(!xiaohe.character(';').handled && xiaohe.snapshot().preedit == "n",
                "Microsoft semicolon leaked to Xiaohe");
    }
    {
        SessionOptions options;
        options.paths = paths_a;
        options.learning = false;
        Session session(options), other(options);
        require(!session.command(Command::MoveLeft).handled, "Empty caret movement consumed host input");
        type(session, "ni");
        type(other, "ni");
        const auto query = session.online_query();
        require(query.has_value(), "Missing pre-edit online request");
        session.command(Command::MoveHome);
        require(session.snapshot().caret_position == 0 && other.snapshot().caret_position == 2,
                "Caret movement leaked between sessions");
        require(session.online_query()->generation == query->generation,
                "Caret-only movement invalidated unchanged candidates");
        session.command(Command::Backspace);
        require(session.snapshot().editing_text == "ni", "Home backspace removed input");
        session.command(Command::DeleteForward);
        require(session.snapshot().editing_text == "i", "Forward delete removed the wrong character");
        session.character('n');
        require(session.snapshot().editing_text == "ni" && session.snapshot().caret_position == 1 &&
                    session.snapshot().candidates.empty(),
                "Middle insertion did not refresh candidates");
        session.command(Command::MoveEnd);
        require(session.snapshot().candidates.front().word == "你",
                "Moving back to the end did not restore the full-string decode");
        require(!session.apply_online_candidate(*query, "旧响应", CandidateSource::CloudSuggestion),
                "Editing back to the same text accepted an old online response");
        session.command(Command::MoveLeft);
        session.character('\'');
        session.character('\'');
        require(session.snapshot().editing_text == "n'i", "Duplicate separator was inserted at caret");
        session.command(Command::Backspace);
        session.command(Command::MoveEnd);
        session.command(Command::DeleteForward);
        require(session.snapshot().editing_text == "ni" && session.snapshot().caret_position == 2,
                "End forward delete changed input");
        session.command(Command::MoveHome);
        require(session.snapshot().candidates.empty() && !session.select(0).handled,
                "A caret at the start still offered or accepted candidates");
        session.command(Command::MoveEnd);
        require(session.select(0).commit == "你" && session.snapshot().caret_position == 0,
                "Selection did not clear caret with composition");
        type(session, "ni");
        require(session.snapshot().caret_position == 2, "New composition inherited the old caret");
    }
    for (const char marker : {'Y', 'J', 'E', 'M', 'K', 'U', 'R'})
    {
        SessionOptions options;
        options.paths = paths_a;
        options.learning = false;
        Session session(options);
        session.character(marker, true);
        const std::string payload = marker == 'U' ? "41" : "ka";
        type(session, payload);
        session.command(Command::MoveHome);
        require(session.snapshot().caret_position == 1, "Local-mode caret crossed its marker");
        session.command(Command::Backspace);
        session.command(Command::DeleteForward);
        session.character(payload.front());
        require(session.snapshot().editing_text == std::string(1, marker) + payload &&
                    session.snapshot().caret_position == 2,
                "Local-mode middle edit lost its source text");
        session.command(Command::Cancel);
        require(session.snapshot().editing_text.empty() && session.snapshot().caret_position == 0,
                "Cancel retained local-mode editing state");
    }
    for (const auto scheme : {SchemeType::Shuangpin, SchemeType::Wubi, SchemeType::JapaneseRomaji})
    {
        SessionOptions options;
        options.paths = paths_a;
        options.scheme = scheme;
        options.learning = false;
        Session session(options);
        type(session, "ka");
        session.command(Command::MoveLeft);
        session.command(Command::Backspace);
        session.character('k');
        require(session.snapshot().editing_text == "ka" && session.snapshot().caret_position == 1,
                "Scheme edit used rendered text instead of raw source");
        session.switch_scheme(SchemeType::Quanpin);
        require(session.snapshot().caret_position == 0, "Scheme change retained the old caret");
    }
    {
        SessionOptions options;
        options.paths = paths_a;
        Session session(options);
        session.set_dedicated_english(true);
        type(session, "ello");
        session.command(Command::MoveHome);
        session.character('H');
        require(session.snapshot().editing_text == "Hello" && session.snapshot().caret_position == 1 &&
                    session.snapshot().candidates.front().word == "hello",
                "English middle edit lost case or candidates");
    }
    {
        execute(paths_a.dictionary(assets::main_dictionary),
                "CREATE TABLE tbl_1_h(key TEXT,jp TEXT,value TEXT,weight INTEGER);"
                "INSERT INTO tbl_1_h VALUES('hao','h','好',10000);");
        SessionOptions options;
        options.paths = paths_a;
        options.learning = false;
        Session session(options);
        type(session, "nihao");
        const auto view = session.snapshot();
        const auto prefix = std::find_if(view.candidates.begin(), view.candidates.end(),
                                         [](const auto &word) { return word.word == "你"; });
        require(prefix != view.candidates.end(), "Missing partial-selection fixture");
        const auto prefix_index = static_cast<std::size_t>(prefix - view.candidates.begin());
        session.command(Command::MoveHome);
        require(session.snapshot().candidates.empty() && !session.select(prefix_index).handled,
                "A caret at the start still offered or accepted candidates");
        session.command(Command::MoveEnd);
        const auto selected = session.select(prefix_index);
        require(selected.commit == "你" && session.snapshot().editing_text == "hao" &&
                    session.snapshot().caret_position == 3,
                "Partial selection retained the old caret");
        session.command(Command::MoveHome);
        session.command(Command::DeleteForward);
        session.character('h');
        session.command(Command::MoveEnd);
        require(session.finish().commit == "好", "Editing the remainder lost the selected prefix boundary");
    }
    require(paths_a.resources != paths_a.dictionaries && paths_a.cache != paths_a.dictionaries,
            "Mutable dictionaries must live separately from resources and cache");
    {
        SessionOptions options_a;
        options_a.paths = paths_a;
        options_a.learning = false;
        SessionOptions options_b;
        options_b.paths = paths_b;
        options_b.learning = false;
        Session a(options_a), b(options_b);
        type(a, "ni");
        type(b, "ni");
        require(a.snapshot().candidates.front().word == "你", "Session A used another resource directory");
        require(b.snapshot().candidates.front().word == "妮", "Session B used another resource directory");
        const auto query_a = a.online_query();
        require(query_a.has_value(), "No online request for complete pinyin");
        require(!b.apply_online_candidate(*query_a, "跨会话建议", CandidateSource::CloudSuggestion),
                "Another session's online response was accepted");
        require(a.apply_online_candidate(*query_a, "本会话建议", CandidateSource::CloudSuggestion),
                "Own online response was rejected");
        a.command(Command::Cancel);
        b.command(Command::Cancel);
        require(a.set_helpcode_schema("lantian") && b.set_helpcode_schema("xiaohe"), "Cannot select helpcodes");
        type(a, "niC");
        type(b, "niC");
        require(a.snapshot().candidates.front().word == "拟", "Session A helpcode changed with B");
        require(b.snapshot().candidates.front().word == "妮", "Session B helpcode was not isolated");
        require(a.punctuation('"').commit == "拟“", "Punctuation did not flush composition");
        b.command(Command::Cancel);
        require(b.punctuation('"').commit == "“", "Quotation state leaked across sessions");
        const auto run_session = [](Session &session, const std::string &expected) {
            for (int i = 0; i < 20; ++i)
            {
                session.command(Command::Cancel);
                type(session, "ni");
                const auto snapshot = session.snapshot();
                require(!snapshot.candidates.empty() && snapshot.candidates.front().word == expected,
                        "Concurrent session read another user's dictionary");
            }
        };
        auto first_session = std::async(std::launch::async, run_session, std::ref(a), "你");
        auto second_session = std::async(std::launch::async, run_session, std::ref(b), "妮");
        first_session.get();
        second_session.get();
        a.command(Command::Cancel);
        a.character('K', true);
        a.character('x');
        require(a.snapshot().candidates.front().word == "你短语", "Local mode ignored runtime paths");
    }
    {
        SessionOptions options;
        options.paths = paths_a;
        options.learning = false;
        Session session(options);
        type(session, "ni'ni");
        const auto view = session.snapshot();
        const auto selected = std::find_if(view.candidates.begin(), view.candidates.end(),
                                           [](const auto &item) { return item.word == "拟"; });
        require(selected != view.candidates.end(), "No alternate first-segment candidate");
        require(session.finish(static_cast<std::size_t>(selected - view.candidates.begin())).commit == "拟你",
                "Finishing ignored the highlight or dropped the remaining segment");
        require(session.snapshot().preedit.empty(), "Finishing left an active composition");
        require(!session.finish().handled, "Finishing an empty session consumed input");
        type(session, "ni");
        require(session.finish(9999).commit == "ni", "Invalid highlight did not preserve raw input");

        session.set_helpcode_enabled(false);
        type(session, "ni");
        require(!session.character('C').handled, "Disabled helpcode still consumed uppercase input");
        session.set_helpcode_enabled(true);
        require(session.character('C').handled && session.snapshot().candidates.front().word == "拟",
                "Re-enabling helpcode did not update this composition");
        session.command(Command::Cancel);
        session.set_dedicated_english(true);
        require(session.snapshot().dedicated_english, "Empty dedicated English mode was lost in the snapshot");
        type(session, "hel");
        require(session.snapshot().dedicated_english && session.snapshot().preedit == "hel",
                "Dedicated English state was not represented in the snapshot");
        session.set_dedicated_english(false);
        require(!session.snapshot().dedicated_english && session.snapshot().preedit.empty(),
                "Leaving dedicated English left stale snapshot state");
    }
    {
        SessionOptions options_a;
        options_a.paths = paths_a;
        options_a.scheme = SchemeType::JapaneseRomaji;
        SessionOptions options_b;
        options_b.paths = paths_b;
        options_b.scheme = SchemeType::JapaneseRomaji;
        Session a(options_a), b(options_b);
        type(a, "kana");
        type(b, "kana");
        const auto has = [](const Session &session, const std::string &word) {
            const auto snapshot = session.snapshot();
            return std::any_of(snapshot.candidates.begin(), snapshot.candidates.end(),
                               [&](const auto &candidate) { return candidate.word == word; });
        };
        require(has(a, "甲") && !has(a, "乙"), "Japanese session A used another model");
        require(has(b, "乙") && !has(b, "甲"), "Japanese session B reused the first model");
    }
    // Explicit pinning must use the selected canonical key, preserve input, and survive replay.
    const auto pin_resources = root / "pin-resources";
    make_resources(pin_resources, "你");
    execute(pin_resources / assets::main_dictionary,
            "CREATE TABLE tbl_2_n(key TEXT,jp TEXT,value TEXT,weight INTEGER);"
            "INSERT INTO tbl_2_n VALUES('ni''hao','nh','你好',10000),('ni''hao','nh','拟好',9000);"
            "CREATE TABLE wubi86(key TEXT,value TEXT,weight INTEGER);"
            "INSERT INTO wubi86 VALUES('wq','你好',10000),('wq','拟好',9000);");
    execute(pin_resources / assets::english_dictionary,
            "INSERT INTO english_words(word,display,weight) VALUES('help','help',5);");
    const auto pin_main_bytes = bytes(pin_resources / assets::main_dictionary);
    const auto pin_english_bytes = bytes(pin_resources / assets::english_dictionary);
    for (const auto scheme : {SchemeType::Quanpin, SchemeType::Shuangpin, SchemeType::Wubi})
    {
        const auto suffix = std::to_string(static_cast<int>(scheme));
        const auto user = root / ("position-user-" + suffix);
        const auto cache = root / ("position-cache-" + suffix);
        SessionOptions options;
        options.paths = prepare_runtime_paths(pin_resources, user, cache, "v1");
        options.scheme = scheme;
        options.learning = false;
        const auto input = scheme == SchemeType::Quanpin ? "nihao" : (scheme == SchemeType::Shuangpin ? "nihc" : "wq");
        {
            Session session(options);
            type(session, input);
            const auto before = session.snapshot();
            const auto found = std::find_if(before.candidates.begin(), before.candidates.end(),
                                            [](const auto &word) { return word.word == "拟好"; });
            require(found != before.candidates.end(), "Missing fixed-position fixture");
            const auto index = static_cast<std::size_t>(found - before.candidates.begin());
            require(!session.fix_position(index, 0).handled && !session.fix_position(index, 6).handled,
                    "Invalid fixed slot accepted");
            const auto fixed = session.fix_position(index, 1);
            require(fixed.handled && !fixed.commit && !fixed.diagnostic &&
                        session.snapshot().preedit == before.preedit &&
                        session.snapshot().candidates.front().word == "拟好" &&
                        session.snapshot().candidates.front().fixed_position == 1,
                    "Fixed slot did not refresh snapshot");
            execute(options.paths.user(assets::user_journal),
                    "CREATE TRIGGER reject_fixed BEFORE INSERT ON fixed_candidate_positions "
                    "BEGIN SELECT RAISE(ABORT,'fixture rejection'); END;");
            const auto rejected = session.fix_position(0, 2);
            require(rejected.handled && rejected.diagnostic && !rejected.commit &&
                        session.snapshot().candidates.front().fixed_position == 1,
                    "Failed fixed-slot write changed the snapshot or hid its diagnostic");
            execute(options.paths.user(assets::user_journal), "DROP TRIGGER reject_fixed");
            auto independent_options = options;
            independent_options.paths =
                prepare_runtime_paths(pin_resources, root / ("independent-position-user-" + suffix),
                                      root / ("independent-position-cache-" + suffix), "v1");
            Session independent(independent_options);
            type(independent, input);
            require(independent.snapshot().candidates.front().word == "你好",
                    "Fixed position leaked into another user layout");
        }
        options.paths = prepare_runtime_paths(pin_resources, user, cache, "v2");
        Session replayed(options);
        type(replayed, input);
        require(replayed.snapshot().candidates.front().word == "拟好", "Fixed slot lost after generation change");
        const auto cleared = replayed.clear_position(0);
        require(cleared.handled && !cleared.diagnostic && !cleared.commit &&
                    replayed.snapshot().candidates.front().word == "你好",
                "Clearing slot did not restore dictionary order");
    }
    for (const int mode : {0, 1, 2})
    {
        SessionOptions options;
        options.paths = prepare_runtime_paths(pin_resources, root / ("fixed-english-" + std::to_string(mode)),
                                              root / ("fixed-english-cache-" + std::to_string(mode)), "v1");
        options.learning = false;
        options.english.mixed_candidates = mode == 2;
        Session session(options);
        if (mode == 1)
            session.character('Y', true);
        else if (mode == 0)
            session.set_dedicated_english(true);
        type(session, "he");
        const auto before = session.snapshot();
        const auto found = std::find_if(before.candidates.begin(), before.candidates.end(),
                                        [](const auto &word) { return word.word == "help"; });
        require(found != before.candidates.end(), "Missing English fixed-slot fixture");
        const auto fixed = session.fix_position(static_cast<std::size_t>(found - before.candidates.begin()), 1);
        require(fixed.handled && !fixed.diagnostic && session.snapshot().candidates.front().word == "help",
                "English fixed slot was not applied");
        require(session.clear_position(0).handled, "English fixed slot could not be cleared");
    }
    const auto pin_word = [&](Session &session, const std::string &word) {
        const auto before = session.snapshot();
        const auto selected = std::find_if(before.candidates.begin(), before.candidates.end(),
                                           [&](const auto &item) { return item.word == word; });
        require(selected != before.candidates.end(), "No candidate for explicit pinning");
        const auto result = session.pin(static_cast<std::size_t>(selected - before.candidates.begin()));
        require(result.handled && !result.commit && !result.diagnostic, "Explicit pin failed or committed text");
        require(session.snapshot().preedit == before.preedit, "Pin changed the composition");
        const auto after = session.snapshot();
        const auto first = std::find_if(after.candidates.begin(), after.candidates.end(),
                                        [&](const auto &item) { return item.source == selected->source; });
        require(first != after.candidates.end() && first->word == word,
                "Pin did not refresh dictionary candidate order");
        require(!session.pin(before.candidates.size() + 100).handled, "Invalid pin index was accepted");
    };
    for (const auto scheme : {SchemeType::Quanpin, SchemeType::Shuangpin, SchemeType::Wubi})
    {
        const auto suffix = std::to_string(static_cast<int>(scheme));
        const auto user = root / ("pin-user-" + suffix);
        const auto cache = root / ("pin-cache-" + suffix);
        SessionOptions options;
        options.paths = prepare_runtime_paths(pin_resources, user, cache, "v1");
        options.scheme = scheme;
        options.learning = false;
        const std::string input =
            scheme == SchemeType::Quanpin ? "nihao" : (scheme == SchemeType::Shuangpin ? "nihc" : "wq");
        {
            Session session(options);
            require(!session.pin(0).handled, "Empty session accepted pin");
            type(session, input);
            pin_word(session, "拟好");
            require(session.finish().commit == "拟好", "Pin interfered with later selection");
        }
        options.paths = prepare_runtime_paths(pin_resources, user, cache, "v2");
        Session replayed(options);
        type(replayed, input);
        require(replayed.snapshot().candidates.front().word == "拟好", "Pin did not survive generation replay");
    }
    for (const bool temporary : {false, true})
    {
        SessionOptions options;
        const std::string suffix = temporary ? "temporary" : "dedicated";
        const auto user = root / ("pin-english-user-" + suffix);
        const auto cache = root / ("pin-english-cache-" + suffix);
        options.paths = prepare_runtime_paths(pin_resources, user, cache, "v1");
        options.learning = false;
        {
            Session session(options);
            if (temporary)
                session.character('Y', true);
            else
                session.set_dedicated_english(true);
            type(session, "he");
            pin_word(session, "help");
        }
        options.paths = prepare_runtime_paths(pin_resources, user, cache, "v2");
        Session replayed(options);
        replayed.set_dedicated_english(true);
        type(replayed, "he");
        require(replayed.snapshot().candidates.front().word == "help", "English pin did not survive replay");
        replayed.command(Command::Cancel);
        type(replayed, "zzzz");
        require(!replayed.pin(0).handled, "Generated candidate accepted dictionary pinning");
    }
    {
        SessionOptions options;
        options.paths = prepare_runtime_paths(pin_resources, root / "pin-mixed-user", root / "pin-mixed-cache", "v1");
        options.learning = false;
        options.english.mixed_candidates = true;
        Session mixed(options);
        type(mixed, "he");
        pin_word(mixed, "help");
    }
    {
        SessionOptions options;
        options.paths = prepare_runtime_paths(pin_resources, root / "pin-failed-user", root / "pin-failed-cache", "v1");
        options.learning = false;
        // A directory in place of the journal is a portable, deterministic write failure.
        std::filesystem::remove(options.paths.user(assets::user_journal));
        std::filesystem::create_directory(options.paths.user(assets::user_journal));
        Session session(options);
        type(session, "nihao");
        const auto before = session.snapshot();
        const auto selected = std::find_if(before.candidates.begin(), before.candidates.end(),
                                           [](const auto &item) { return item.word == "拟好"; });
        require(selected != before.candidates.end(), "No candidate for failed pin test");
        const auto result = session.pin(static_cast<std::size_t>(selected - before.candidates.begin()));
        require(result.handled && !result.commit && result.diagnostic.has_value(),
                "Failed pin was reported as success or committed text");
        require(session.snapshot().preedit == before.preedit &&
                    session.snapshot().candidates.front().word == before.candidates.front().word,
                "Failed pin changed input or candidate order");
    }
    require(bytes(pin_resources / assets::main_dictionary) == pin_main_bytes &&
                bytes(pin_resources / assets::english_dictionary) == pin_english_bytes,
            "Explicit pinning modified immutable resources");
    // Query/learning changes affect only the user working copy and journal; replay retains them.
    {
        QuanpinDictionary dictionary({}, paths_a);
        require(dictionary.create_word_from_canonical_pinyin("ni", "伱") == 0, "User insertion failed");
    }
    require(bytes(resource_a / assets::main_dictionary) == original, "Learning modified immutable source");
    std::ofstream(paths_a.cache / "disposable") << "cache fixture";
    std::filesystem::remove_all(paths_a.cache);
    require(std::filesystem::is_regular_file(paths_a.user(assets::user_journal)),
            "Clearing cache removed the user journal");
    {
        QuanpinDictionary dictionary({}, paths_a);
        require(dictionary.find_candidate("ni", "伱").has_value(), "Clearing cache removed learned vocabulary");
    }
    const auto paths_v2 = prepare_runtime_paths(resource_a, paths_a.user_data, paths_a.cache, "v2");
    {
        QuanpinDictionary dictionary({}, paths_v2);
        require(dictionary.find_candidate("ni", "伱").has_value(), "Upgrade lost user phrase replay");
        EnglishDictionary english(path_to_utf8(paths_v2.dictionary(assets::english_dictionary)), false,
                                  path_to_utf8(paths_v2.resource(assets::translations)));
        require(english.query_chinese_gloss("hello") == "你翻译", "Translation sidecar did not use resource path");
    }
    {
        QuanpinDictionary dictionary({}, paths_v2);
        require(dictionary.create_word_from_canonical_pinyin("ni", "倪") == 0, "Second-generation insertion failed");
    }
    const auto restored_v1 = prepare_runtime_paths(resource_a, paths_a.user_data, paths_a.cache, "v1");
    {
        QuanpinDictionary dictionary({}, restored_v1);
        require(dictionary.find_candidate("ni", "倪").has_value(), "Returning to old generation lost newer learning");
    }
    // 联网补回来的释义落在 user 目录,不落 english.db。english.db 是按代复制的工作副本,换代时从资源
    // 目录重拷、只回放 msime_user.db,写进去的释义会被静默清掉。
    {
        EnglishDictionary english(path_to_utf8(restored_v1.dictionary(assets::english_dictionary)), false,
                                  path_to_utf8(restored_v1.resource(assets::translations)),
                                  path_to_utf8(restored_v1.user(assets::gloss_cache)));
        require(english.query_chinese_gloss("nimbus").empty(), "A gloss appeared before anything cached it");
        require(english.cache_gloss(false, "nimbus", "积雨云"), "Caching a fetched gloss failed");
        require(english.query_chinese_gloss("nimbus") == "积雨云", "A cached gloss was not readable");
        // 缓存排在最后一层:它装的恰恰是词库查不到的词,质量最没保证,不该盖掉出货词库和用户自己写的
        // sidecar。
        require(english.cache_gloss(false, "hello", "机翻"), "Caching alongside a shipped gloss failed");
        require(english.query_chinese_gloss("hello") == "你翻译", "The cache overrode the translation sidecar");
    }
    // 反证:同一条释义写进 english.db 的工作副本,换一代就没了。这就是缓存不能放在那里的原因。
    require(EnglishDictionary::upsert_gloss(path_to_utf8(restored_v1.dictionary(assets::english_dictionary)), false,
                                            "cumulus", "积云"),
            "Writing a gloss into the working copy failed");
    const auto paths_v4 = prepare_runtime_paths(resource_a, paths_a.user_data, paths_a.cache, "v4");
    {
        EnglishDictionary english(path_to_utf8(paths_v4.dictionary(assets::english_dictionary)), false,
                                  path_to_utf8(paths_v4.resource(assets::translations)),
                                  path_to_utf8(paths_v4.user(assets::gloss_cache)));
        require(english.query_chinese_gloss("cumulus").empty(),
                "A gloss written into the copied dictionary survived a generation change");
        require(english.query_chinese_gloss("nimbus") == "积雨云", "A new generation lost the cached gloss");
    }

    const auto before_failure = bytes(paths_v2.dictionary(assets::main_dictionary));
    require(user_dictionary::record_upsert(path_to_utf8(paths_a.user(assets::user_journal)),
                                           user_dictionary::DictionaryKind::Pinyin, "@", "无效词", 10),
            "Cannot write failure fixture");
    bool rejected = false;
    try
    {
        (void)prepare_runtime_paths(resource_a, paths_a.user_data, paths_a.cache, "v3");
    }
    catch (const std::runtime_error &)
    {
        rejected = true;
    }
    require(rejected, "Invalid replay did not reject generation");
    require(bytes(paths_v2.dictionary(assets::main_dictionary)) == before_failure,
            "Failed replay replaced working data");
    require(!std::filesystem::exists(paths_a.user_data / "dictionaries/v3"), "Failed generation was published");
    require(!std::filesystem::exists(paths_a.user_data / "dictionaries/v3.incoming"), "Failed staging was retained");

    // Use the real pinned decoder model. Interleave independent users, invalid model loading,
    // destruction, and concurrent calls; candidate contents must equal isolated decoding.
    const auto model = resource_a / assets::pinyin_model;
    std::filesystem::copy_file(path_from_utf8(METASEQUOIA_TEST_PINYIN_MODEL), model);
    {
        PinyinDecoder decoder_a(model, resource_a / "decoder-a.dat"), decoder_b(model, resource_b / "decoder-b.dat");
        const auto expected_a = decoder_a.sentence("nihao");
        const auto expected_b = decoder_b.sentence("zhongguo");
        require(!expected_a.empty() && !expected_b.empty(), "Real decoder model did not load");
        {
            PinyinDecoder missing(root / "missing.dat", root / "missing-user.dat");
            require(missing.sentence("nihao").empty(), "Missing model used another session's decoder");
        }
        require(decoder_a.sentence("nihao") == expected_a, "Failed model load damaged existing session");
        const auto run = [](const PinyinDecoder &decoder, const std::string &input, const std::string &expected) {
            for (int i = 0; i < 12; ++i)
                require(decoder.sentence(input) == expected, "Concurrent decoder result leaked across sessions");
        };
        auto first = std::async(std::launch::async, run, std::cref(decoder_a), "nihao", expected_a);
        auto second = std::async(std::launch::async, run, std::cref(decoder_b), "zhongguo", expected_b);
        first.get();
        second.get();
    }
}

// The lattice asks for its ngram tables through RuntimePaths::dictionary(), which resolves against
// the prepared generation rather than the resource directory. Preparation used to copy exactly the
// two databases, so a table shipped beside them was invisible to the decoder and the context terms
// scored nothing - shipped and inert, with nothing reporting either.
void test_generation_tables_are_staged()
{
    using namespace metasequoia;
    const auto root = std::filesystem::temp_directory_path() /
                      ("msime-ngram-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    test::ScopedDataDirectoryCleanup cleanup(root);
    const auto resources = root / "resources";
    make_resources(resources, "你");
    std::ofstream(resources / assets::bigram_table, std::ios::binary) << "first";

    const auto user = root / "user";
    auto paths = prepare_runtime_paths(resources, user, root / "cache", "v1");
    require(std::filesystem::is_regular_file(paths.dictionary(assets::bigram_table)),
            "A table shipped with the dictionary did not reach the generation");
    require(bytes(paths.dictionary(assets::bigram_table)) == bytes(resources / assets::bigram_table),
            "The staged table is not the one the resource set carries");
    require(!std::filesystem::exists(paths.dictionary(assets::trigram_table)),
            "A table the resource set does not carry was invented");

    // A build that starts shipping a table must not need a new dictionary generation to deliver it,
    // and must not disturb the one a user has already learned into.
    std::ofstream(resources / assets::trigram_table, std::ios::binary) << "third";
    std::ofstream(resources / assets::bigram_table, std::ios::binary) << "second";
    paths = prepare_runtime_paths(resources, user, root / "cache", "v1");
    require(std::filesystem::is_regular_file(paths.dictionary(assets::trigram_table)),
            "A table added after preparation never reached the prepared generation");
    require(bytes(paths.dictionary(assets::bigram_table)) == std::string("first"),
            "Re-preparing a ready generation replaced a table a session may have mapped");
}

int main()
{
    test_runtime_isolation();
    test_generation_tables_are_staged();
    return 0;
}
