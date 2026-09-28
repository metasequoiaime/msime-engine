#include <metasequoia/session.h>

#include "../../contracts/assets/assets.h"
#include "../../core/data_path.h"
#include "../../english/english_dictionary.h"
#include "../../core/input_session.h"
#include "../../core/runtime_paths.h"
#include "test_directory_cleanup.h"

#include <sqlite3.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace
{
using namespace metasequoia;

void require(bool value, const char *message)
{
    if (!value)
    {
        throw std::runtime_error(message);
    }
}

void execute(const std::filesystem::path &path, const std::string &sql)
{
    sqlite3 *database = nullptr;
    require(sqlite3_open(path_to_utf8(path).c_str(), &database) == SQLITE_OK, "Fixture open failed");
    const int result = sqlite3_exec(database, sql.c_str(), nullptr, nullptr, nullptr);
    sqlite3_close(database);
    require(result == SQLITE_OK, "Fixture SQL failed");
}

std::int64_t query_weight(const std::filesystem::path &path, const std::string &key, const std::string &value)
{
    sqlite3 *database = nullptr;
    require(sqlite3_open(path_to_utf8(path).c_str(), &database) == SQLITE_OK, "Weight query open failed");
    sqlite3_stmt *statement = nullptr;
    const bool prepared = sqlite3_prepare_v2(database, "SELECT weight FROM wubi86 WHERE key=?1 AND value=?2", -1,
                                             &statement, nullptr) == SQLITE_OK;
    const bool bound = prepared && sqlite3_bind_text(statement, 1, key.c_str(), -1, SQLITE_TRANSIENT) == SQLITE_OK &&
                       sqlite3_bind_text(statement, 2, value.c_str(), -1, SQLITE_TRANSIENT) == SQLITE_OK;
    const bool stepped = bound && sqlite3_step(statement) == SQLITE_ROW;
    const auto weight = stepped ? sqlite3_column_int64(statement, 0) : -1;
    sqlite3_finalize(statement);
    sqlite3_close(database);
    require(stepped, "Weight query returned no row");
    return weight;
}

std::vector<std::string> words(const InputSession &session)
{
    std::vector<std::string> result;
    result.reserve(session.candidates().size());
    for (const auto &candidate : session.candidates())
    {
        result.push_back(candidate.word);
    }
    return result;
}

std::vector<std::string> type(InputSession &session, const std::string &code)
{
    for (const char letter : code)
    {
        session.handle_character(letter);
    }
    return words(session);
}

// The fixture answers ni'hao and zi in quanpin and wq in wubi, and leaves nihao unanswered by the
// wubi table. Hardcoding codes against the shipped dictionary would tie these assertions to what a
// checkout happens to have built, which is how the first version of this file passed here and
// failed on every CI platform.
std::filesystem::path prepare_resources(const std::filesystem::path &root)
{
    const auto resources = root / "resources";
    std::filesystem::create_directories(resources);
    execute(resources / assets::main_dictionary,
            "CREATE TABLE tbl_1_n(key TEXT,jp TEXT,value TEXT,weight INTEGER);"
            "INSERT INTO tbl_1_n VALUES('ni','n','你',10000);"
            "CREATE TABLE tbl_1_z(key TEXT,jp TEXT,value TEXT,weight INTEGER);"
            "INSERT INTO tbl_1_z VALUES('zi','z','子',10000);"
            "CREATE TABLE tbl_2_n(key TEXT,jp TEXT,value TEXT,weight INTEGER);"
            "INSERT INTO tbl_2_n VALUES('ni''hao','nh','你好',10000),('ni''hao','nh','拟好',9000);"
            "CREATE TABLE tbl_1_t(key TEXT,jp TEXT,value TEXT,weight INTEGER);"
            "INSERT INTO tbl_1_t VALUES('ta','t','他',10000);"
            "CREATE TABLE wubi86(key TEXT,value TEXT,weight INTEGER);"
            "INSERT INTO wubi86 VALUES('wq','你好',10000),('wqaa','众人',9000),"
            "('taaa','笔',6000),('a','工',10000),('aaaa','工',5000),('aaab','苛',4000);");
    require(EnglishDictionary::ensure_schema(path_to_utf8(resources / assets::english_dictionary)),
            "English schema failed");
    return resources;
}

// InputSession is neither copyable nor movable, so a session is built where it is used.
RuntimePaths paths_for(const std::filesystem::path &resources, const std::filesystem::path &root,
                       const std::string &tag)
{
    return prepare_runtime_paths(resources, root / ("user-" + tag), root / ("cache-" + tag), "v1");
}
} // namespace

int main()
{
    try
    {
        const auto root =
            std::filesystem::temp_directory_path() /
            ("msime-wubi-mixed-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        test::ScopedDataDirectoryCleanup cleanup(root);
        const auto resources = prepare_resources(root);
        int counter = 0;
        const auto next = [&counter] { return std::to_string(counter++); };

        // A code the wubi table cannot answer is answered by quanpin for the same letters.
        {
            InputSession plain(SchemeType::Wubi, GetXiaoheShuangpinProfile(), paths_for(resources, root, next()));
            plain.set_wubi_input_options(WubiInputOptions{false});
            require(type(plain, "nihao").empty(), "The fixture answered nihao in wubi.");

            InputSession mixed(SchemeType::Wubi, GetXiaoheShuangpinProfile(), paths_for(resources, root, next()));
            mixed.set_wubi_input_options(WubiInputOptions{true});
            InputSession reference(SchemeType::Quanpin, GetXiaoheShuangpinProfile(),
                                   paths_for(resources, root, next()));
            reference.set_wubi_input_options(WubiInputOptions{false});
            require(type(mixed, "nihao") == type(reference, "nihao"),
                    "Mixed input answered an unmatched code with something other than quanpin.");
        }

        // A code the table does answer keeps its own candidates, untouched by the setting.
        {
            InputSession plain(SchemeType::Wubi, GetXiaoheShuangpinProfile(), paths_for(resources, root, next()));
            plain.set_wubi_input_options(WubiInputOptions{false});
            const auto native = type(plain, "wq");
            require(!native.empty(), "The fixture did not answer wq in wubi.");

            InputSession mixed(SchemeType::Wubi, GetXiaoheShuangpinProfile(), paths_for(resources, root, next()));
            mixed.set_wubi_input_options(WubiInputOptions{true});
            require(type(mixed, "wq") == native, "Mixed input changed the candidates for a matched code.");
        }

        // A longer-code prefix is only a hint. Mixed input must still offer quanpin for a prefix
        // that has no exact wubi row, while plain wubi keeps the hint visible.
        {
            InputSession plain(SchemeType::Wubi, GetXiaoheShuangpinProfile(), paths_for(resources, root, next()));
            plain.set_wubi_input_options(WubiInputOptions{false});
            const auto hints = type(plain, "ta");
            require(std::find(hints.begin(), hints.end(), "笔") != hints.end(),
                    "Plain wubi did not show the prefix hint for ta.");

            InputSession mixed(SchemeType::Wubi, GetXiaoheShuangpinProfile(), paths_for(resources, root, next()));
            mixed.set_wubi_input_options(WubiInputOptions{true});
            const auto candidates = type(mixed, "ta");
            require(std::find(candidates.begin(), candidates.end(), "他") != candidates.end(),
                    "A wubi prefix hint blocked the pinyin fallback for ta.");
            require(mixed.answered_by_pinyin_fallback(), "ta was not marked as a pinyin fallback.");
        }

        // A word with both a short and a full code appears once, under the exact code that was typed.
        {
            InputSession session(SchemeType::Wubi, GetXiaoheShuangpinProfile(), paths_for(resources, root, next()));
            session.set_wubi_input_options(WubiInputOptions{false});
            const auto candidates = type(session, "a");
            require(std::count(candidates.begin(), candidates.end(), "工") == 1,
                    "A word with a short and a full code was listed twice.");
            require(session.candidates().front().word == "工" && session.candidates().front().pinyin == "a",
                    "The deduplicated word did not keep the exact-code row.");
        }

        // Wubi selection learning updates the existing code row and journals it for replay.
        {
            const auto user_paths = paths_for(resources, root, next());
            InputSession session(SchemeType::Wubi, GetXiaoheShuangpinProfile(), user_paths);
            session.set_wubi_input_options(WubiInputOptions{false});
            type(session, "a");
            const auto found = std::find_if(session.candidates().begin(), session.candidates().end(),
                                            [](const auto &item) { return item.word == "苛"; });
            require(found != session.candidates().end(), "The Wubi ranking fixture lost 苛.");
            require(session.select_candidate(static_cast<std::size_t>(found - session.candidates().begin())).handled,
                    "Selecting a Wubi candidate was not handled.");
            require(query_weight(user_paths.dictionary(assets::main_dictionary), "aaab", "苛") == 4001,
                    "Wubi selection did not persist the promoted weight.");
        }

        // z is not a wubi letter. Dropping it does not refuse a spelling, it silently becomes a
        // different one, so mixed input has to accept it -- and only mixed input.
        {
            InputSession plain(SchemeType::Wubi, GetXiaoheShuangpinProfile(), paths_for(resources, root, next()));
            plain.set_wubi_input_options(WubiInputOptions{false});
            type(plain, "zi");
            require(plain.preedit() == "i", "Plain wubi stopped dropping z.");

            InputSession mixed(SchemeType::Wubi, GetXiaoheShuangpinProfile(), paths_for(resources, root, next()));
            mixed.set_wubi_input_options(WubiInputOptions{true});
            const auto candidates = type(mixed, "zi");
            require(mixed.preedit() == "zi", "Mixed input dropped the z from a spelling.");
            require(std::find(candidates.begin(), candidates.end(), "子") != candidates.end(),
                    "Mixed input did not reach the spelling that needed z.");
        }

        // The four-letter limit holds until the table has failed the code in hand.
        {
            InputSession matched(SchemeType::Wubi, GetXiaoheShuangpinProfile(), paths_for(resources, root, next()));
            matched.set_wubi_input_options(WubiInputOptions{true});
            const auto four = type(matched, "wqaa");
            require(!four.empty(), "The fixture did not answer the four-letter code wqaa.");
            matched.handle_character('a');
            require(matched.preedit() == "wqaa", "A matched four-letter code accepted a fifth letter.");
            require(words(matched) == four, "A refused fifth letter still changed the candidates.");

            InputSession unmatched(SchemeType::Wubi, GetXiaoheShuangpinProfile(), paths_for(resources, root, next()));
            unmatched.set_wubi_input_options(WubiInputOptions{true});
            type(unmatched, "nihao");
            require(unmatched.preedit() == "nihao", "Mixed input could not reach a spelling past four letters.");
            unmatched.handle_command(Command::Backspace);
            require(unmatched.preedit() == "niha", "Backspace did not shorten an extended composition.");
        }

        // Backspacing a composition away starts over. Backspace never goes through reset_composition, so the only
        // thing that clears the "this composition is being answered by pinyin" flag is the emptied request itself --
        // and an empty request is also an invalid one. A flag that outlives the composition makes every later code
        // look like one the table failed, so the table's own answer gets thrown away and replaced by an empty pinyin
        // one.
        {
            InputSession fresh(SchemeType::Wubi, GetXiaoheShuangpinProfile(), paths_for(resources, root, next()));
            fresh.set_wubi_input_options(WubiInputOptions{true});
            const auto answer_for_wq = type(fresh, "wq");
            require(!answer_for_wq.empty(), "The fixture did not answer wq in mixed wubi.");
            const auto answer_for_wqaa = type(fresh, "aa");
            require(!answer_for_wqaa.empty(), "The fixture did not answer wqaa in mixed wubi.");

            InputSession reused(SchemeType::Wubi, GetXiaoheShuangpinProfile(), paths_for(resources, root, next()));
            reused.set_wubi_input_options(WubiInputOptions{true});
            require(!type(reused, "nihao").empty(), "Pinyin did not answer nihao, so no fallback flag was ever set.");
            for (int remaining = 5; remaining > 0; --remaining)
            {
                reused.handle_command(Command::Backspace);
            }
            require(reused.preedit().empty(), "Backspacing every letter of nihao left a preedit behind.");
            require(words(reused).empty(), "An emptied composition kept the candidates of the letters removed.");

            require(type(reused, "wq") == answer_for_wq,
                    "A code the wubi table answers came back blanked after an emptied pinyin-fallback composition.");
            require(type(reused, "aa") == answer_for_wqaa,
                    "The blackout from an emptied composition outlived the code that followed it.");
        }

        // Committing a spelling out of a longer one leaves the rest composing, and the rest stays
        // with pinyin: wq is a code the wubi table knows, and answering it with wubi would swap
        // schemes underneath a spelling the user is still in the middle of.
        {
            const auto tail = [&](SchemeType scheme, bool mixed) {
                InputSession session(scheme, GetXiaoheShuangpinProfile(), paths_for(resources, root, next()));
                session.set_wubi_input_options(WubiInputOptions{mixed});
                type(session, "nihaowq");
                const auto &items = session.candidates();
                const auto found =
                    std::find_if(items.begin(), items.end(), [](const auto &item) { return item.word == "你好"; });
                require(found != items.end(), "nihaowq did not offer 你好.");
                session.select_candidate(static_cast<std::size_t>(found - items.begin()));
                return std::make_pair(session.preedit(), words(session));
            };
            const auto pinyin = tail(SchemeType::Quanpin, false);
            const auto mixed = tail(SchemeType::Wubi, true);
            require(mixed.first == pinyin.first, "Mixed input dropped the rest of the composition on selection.");
            require(mixed.second == pinyin.second, "The rest of a composition was answered with wubi candidates.");
        }

        // Off unless asked for, and the setting reaches a live composition rather than the next one.
        {
            InputSession session(SchemeType::Wubi, GetXiaoheShuangpinProfile(), paths_for(resources, root, next()));
            session.set_wubi_input_options(WubiInputOptions{false});
            require(type(session, "nihao").empty(), "Mixed input answered without being switched on.");
            session.set_wubi_input_options(WubiInputOptions{true});
            require(!words(session).empty(), "Switching the setting on left the composition unanswered.");
            session.set_wubi_input_options(WubiInputOptions{false});
            require(words(session).empty(), "Switching the setting off left the fallback candidates on screen.");
        }

        // The public session carries the setting from its options and at runtime.
        {
            SessionOptions options;
            options.paths = prepare_runtime_paths(resources, root / "user-public", root / "cache-public", "v1");
            options.scheme = SchemeType::Wubi;
            options.wubi.mixed_pinyin = true;
            Session session(options);
            for (const char letter : std::string("nihao"))
            {
                session.character(letter);
            }
            require(!session.snapshot().candidates.empty(), "SessionOptions did not carry the mixed setting.");

            SessionOptions plain;
            plain.paths = prepare_runtime_paths(resources, root / "user-toggle", root / "cache-toggle", "v1");
            plain.scheme = SchemeType::Wubi;
            Session toggled(plain);
            for (const char letter : std::string("nihao"))
            {
                toggled.character(letter);
            }
            require(toggled.snapshot().candidates.empty(), "Mixed input was on without being asked for.");
            toggled.set_wubi_mixed_pinyin(true);
            require(!toggled.snapshot().candidates.empty(), "set_wubi_mixed_pinyin did not reach the composition.");
            require(toggled.snapshot().answered_by_pinyin_fallback,
                    "The snapshot did not report that pinyin answered the code.");

            // A host that acts on candidate counts has to tell these apart: four letters answered by
            // one pinyin word is not the unique four-code wubi candidate that auto-commit looks for.
            SessionOptions native;
            native.paths = prepare_runtime_paths(resources, root / "user-native", root / "cache-native", "v1");
            native.scheme = SchemeType::Wubi;
            native.wubi.mixed_pinyin = true;
            Session matched(native);
            for (const char letter : std::string("wqaa"))
            {
                matched.character(letter);
            }
            require(!matched.snapshot().candidates.empty(), "The four-letter code answered with nothing.");
            require(!matched.snapshot().answered_by_pinyin_fallback,
                    "A code the wubi table answered was reported as a pinyin fallback.");
        }
    }
    catch (const std::exception &error)
    {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
    return 0;
}
