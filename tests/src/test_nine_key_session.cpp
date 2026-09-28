#include <metasequoia/session.h>
#include <sqlite3.h>
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include "core/candidate_utils.h"
#include "contracts/assets/assets.h"
#include <unordered_set>

using namespace metasequoia;
void require(bool ok, const char *message)
{
    if (!ok)
        throw std::runtime_error(message);
}
std::size_t candidate(Session &session, const std::string &word)
{
    const auto view = session.snapshot();
    for (std::size_t i = 0; i < view.candidates.size(); ++i)
        if (view.candidates[i].word == word)
            return i;
    throw std::runtime_error("Missing candidate: " + word);
}
void type(Session &session, const std::string &digits)
{
    for (char digit : digits)
        require(session.character(digit).handled, "digit unhandled");
}
int main()
{
    std::vector<WordItem> duplicate_candidates{WordItem("a", "first", 3), WordItem("b", "second", 2),
                                               WordItem("c", "first", 1)};
    deduplicate_candidates_by_word(duplicate_candidates);
    require(duplicate_candidates.size() == 2 && duplicate_candidates[0].word == "first" &&
                duplicate_candidates[1].word == "second",
            "Candidate word deduplication did not preserve the first occurrence.");
    std::unordered_set<std::string> seen_words{"first"};
    const auto unique_candidates =
        take_unique_candidates(std::vector<WordItem>{WordItem("a", "first", 3), WordItem("b", "third", 1)}, seen_words);
    require(unique_candidates.size() == 1 && unique_candidates.front().word == "third" &&
                seen_words.count("third") == 1,
            "Candidate collection did not honor an existing word set.");

    const auto directory =
        std::filesystem::temp_directory_path() /
        ("msime-nine-key-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(directory);
    struct Cleanup
    {
        std::filesystem::path path;
        ~Cleanup()
        {
            std::error_code ec;
            std::filesystem::remove_all(path, ec);
        }
    } cleanup{directory};
    sqlite3 *db = nullptr;
    require(sqlite3_open((directory / "msime.db").u8string().c_str(), &db) == SQLITE_OK, "open fixture");
    require(sqlite3_exec(db,
                         "CREATE TABLE tbl_1_n(key TEXT,jp TEXT,value TEXT,weight INTEGER);"
                         "INSERT INTO tbl_1_n VALUES('ni','n','你',100);"
                         "CREATE TABLE tbl_1_m(key TEXT,jp TEXT,value TEXT,weight INTEGER);"
                         "INSERT INTO tbl_1_m VALUES('mi','m','米',50);"
                         "CREATE TABLE tbl_1_h(key TEXT,jp TEXT,value TEXT,weight INTEGER);"
                         "INSERT INTO tbl_1_h VALUES('hao','h','好',100);"
                         "CREATE TABLE tbl_2_n(key TEXT,jp TEXT,value TEXT,weight INTEGER);"
                         "INSERT INTO tbl_2_n VALUES('ni''hao','nh','你好',1000);",
                         nullptr, nullptr, nullptr) == SQLITE_OK,
            "populate fixture");
    sqlite3_close(db);
    // 九键上按的是数字,英文候选得把数字还原成字母才查得到。
    sqlite3 *english = nullptr;
    require(sqlite3_open((directory / "english.db").u8string().c_str(), &english) == SQLITE_OK, "open english fixture");
    require(sqlite3_exec(english,
                         "CREATE TABLE english_words(word TEXT,display TEXT,weight INTEGER);"
                         "INSERT INTO english_words VALUES('ok','ok',900);"
                         "INSERT INTO english_words VALUES('old','old',1000);"
                         "INSERT INTO english_words VALUES('older','older',800);"
                         "INSERT INTO english_words VALUES('ogham','ogham',0);",
                         nullptr, nullptr, nullptr) == SQLITE_OK,
            "populate english fixture");
    sqlite3_close(english);
    SessionOptions options;
    options.paths = {directory, directory, directory, directory};
    options.learning = false;
    options.english.mixed_candidates = true;
    Session session(options);
    require(!session.character('6').handled, "ordinary pinyin swallowed digit");
    session.set_nine_key_enabled(true);
    require(!session.character('0').handled && !session.character('1').handled, "invalid digits accepted");
    type(session, "65");
    // old 的词频更高,但 65 正好拼满 ok,先给拼满的那个。
    require(candidate(session, "ok") < candidate(session, "old"), "nine-key english ranks the exact code first");
    session.command(Command::Cancel);
    // 64426 是 ni'hao,也是 ogham 唯一能拼出的键序。拼满不等于该给第二格 —— 词频为 0 的词排到最后。
    type(session, "64426");
    require(candidate(session, "你好") == 0, "the pinyin reading lost the first slot");
    require(candidate(session, "ogham") + 1 == session.snapshot().candidates.size(),
            "a zero-weight english word did not go to the end");
    // 合成候选（整句 Generated / 回退 Fallback）与词典词条的 weight 不是同一个尺度，一起按 weight 排
    // 会让拼出来的东西压过真词：64426 曾出成「你好 / 你好哦 / 你敢哦 / 米糕哦 / 密函哦 / 你敢」，前面
    // 四个合成的没有一个是词，真词「你敢」「你搞」「蜜柑」全被压在下面。合成的一旦开始，后面就不该再
    // 冒出词典词条。
    {
        const auto view = session.snapshot();
        const auto synthesised = [](CandidateSource source) {
            return source == CandidateSource::Generated || source == CandidateSource::Fallback;
        };
        std::size_t first_synthesised = view.candidate_sources.size();
        for (std::size_t i = 0; i < view.candidate_sources.size(); ++i)
            if (synthesised(view.candidate_sources[i]))
            {
                first_synthesised = i;
                break;
            }
        for (std::size_t i = first_synthesised; i < view.candidate_sources.size(); ++i)
            require(view.candidate_sources[i] != CandidateSource::Database &&
                        view.candidate_sources[i] != CandidateSource::UserDatabase,
                    "a dictionary entry was ranked below a synthesised candidate");
    }
    session.command(Command::Cancel);
    type(session, "64");
    candidate(session, "你");
    candidate(session, "米");
    const auto before = session.snapshot().preedit;
    require(!session.select(999).handled && !session.choose_nine_key_spelling(999).handled &&
                session.snapshot().preedit == before,
            "invalid index changed composition");
    auto view = session.snapshot();
    require(view.nine_key_spellings.front() == "ni", "preferred spelling was offscreen");
    const auto ni = std::find(view.nine_key_spellings.begin(), view.nine_key_spellings.end(), "ni");
    require(ni != view.nine_key_spellings.end(), "missing disambiguation");
    require(session.choose_nine_key_spelling(ni - view.nine_key_spellings.begin()).handled, "choose spelling");
    require(session.snapshot().preedit == "ni", "locked preedit");
    for (const auto &item : session.snapshot().candidates)
        require(item.word != "米", "lock failed");
    type(session, "426");
    require(session.snapshot().nine_key_spellings.front() == "hao", "locked suffix spelling order");
    require(session.select(candidate(session, "你好")).commit == "你好" && session.snapshot().preedit.empty(),
            "phrase selection");
    type(session, "64426");
    view = session.snapshot();
    auto spelling = std::find(view.nine_key_spellings.begin(), view.nine_key_spellings.end(), "ni");
    session.choose_nine_key_spelling(spelling - view.nine_key_spellings.begin());
    session.choose_nine_key_spelling(0); // hao, the preferred remaining syllable
    require(session.select(candidate(session, "你")).commit == "你" && session.snapshot().editing_text == "426",
            "partial selection");
    require(session.snapshot().preedit == "hao", "partial selection lost the locked suffix");
    require(session.finish().commit == "好" && session.snapshot().preedit.empty(), "finish residual");
    type(session, "64");
    session.command(Command::Backspace);
    require(session.snapshot().editing_text == "6", "backspace");
    session.command(Command::Backspace);
    require(!session.command(Command::Backspace).handled, "idle backspace");
    type(session, "64426");
    require(session.punctuation(',').commit == "你好，" && session.snapshot().preedit.empty(), "punctuation commit");
    type(session, "64");
    require(session.command(Command::CommitRaw).commit == "64", "raw commit");
    type(session, "64");
    session.command(Command::Cancel);
    require(session.snapshot().preedit.empty() && session.snapshot().nine_key_spellings.empty(), "cancel");
    type(session, "64");
    session.switch_scheme(SchemeType::Shuangpin);
    require(session.snapshot().preedit.empty() && !session.character('6').handled, "shuangpin isolation");
    session.switch_scheme(SchemeType::Quanpin);
    session.set_nine_key_enabled(false);
    require(session.character('n').handled && session.snapshot().editing_text == "n", "restore qwerty");
    session.command(Command::Cancel);
    session.set_nine_key_enabled(true);
    type(session, std::string(32, '7'));
    require(session.character('7').diagnostic.has_value() && session.snapshot().editing_text.size() == 32,
            "digit limit did not preserve composition");
    session.command(Command::Cancel);
    // Learning and explicit management use canonical dictionary keys, never digit strings.
    type(session, "64");
    require(session.select(candidate(session, "米")).commit == "米", "disabled learning selection");
    Session unchanged(options);
    unchanged.set_nine_key_enabled(true);
    type(unchanged, "64");
    require(unchanged.snapshot().candidates.front().word == "你", "disabled learning changed ranking");
    auto learning_options = options;
    learning_options.learning = true;
    learning_options.frequency.mode = FrequencyAdjustmentMode::Promote;
    Session learner(learning_options);
    learner.set_nine_key_enabled(true);
    type(learner, "64");
    auto learned = learner.select(candidate(learner, "米"));
    require(learned.commit == "米" && !learned.diagnostic, "learning failed");
    Session managed(options);
    managed.set_nine_key_enabled(true);
    type(managed, "64");
    require(managed.snapshot().candidates.front().word == "米", "learning did not persist");
    auto pinned = managed.pin(candidate(managed, "你"));
    require(pinned.handled && !pinned.commit && !pinned.diagnostic && managed.snapshot().editing_text == "64" &&
                managed.snapshot().candidates.front().word == "你",
            "manual pin failed or committed");
    require(!managed.remove(candidate(managed, "你")).handled, "single character was removed");
    require(!managed.pin(999).handled && !managed.remove(999).handled && !managed.fix_position(0, 6).handled &&
                !managed.clear_position(999).handled,
            "invalid management action handled");
    require(managed.fix_position(candidate(managed, "米"), 1).handled &&
                managed.snapshot().candidates.front().word == "米",
            "fixed position not applied");
    Session fixed(options);
    fixed.set_nine_key_enabled(true);
    type(fixed, "64");
    require(fixed.snapshot().candidates.front().word == "米", "fixed position not persisted");
    const auto fixed_view = fixed.snapshot();
    const auto lock = std::find(fixed_view.nine_key_spellings.begin(), fixed_view.nine_key_spellings.end(), "ni");
    fixed.choose_nine_key_spelling(lock - fixed_view.nine_key_spellings.begin());
    require(fixed.snapshot().candidates.front().word == "你", "fixed position escaped spelling constraint");
    require(managed.clear_position(candidate(managed, "米")).handled &&
                managed.snapshot().candidates.front().word == "你",
            "clear fixed position failed");
    managed.command(Command::Cancel);
    type(managed, "64426");
    auto removed = managed.remove(candidate(managed, "你好"));
    require(removed.handled && !removed.commit && !removed.diagnostic && managed.snapshot().editing_text == "64426",
            "phrase removal failed or changed composition");
    Session after_removal(options);
    after_removal.set_nine_key_enabled(true);
    type(after_removal, "64426");
    for (const auto &item : after_removal.snapshot().candidates)
        require(item.word != "你好", "phrase removal did not persist");

    // A persistence failure must preserve the user's commit and surface a diagnostic.
    auto failure_options = learning_options;
    failure_options.paths.user_data = directory / "blocked";
    std::filesystem::create_directories(failure_options.paths.user_data / assets::user_journal);
    Session failing(failure_options);
    failing.set_nine_key_enabled(true);
    type(failing, "64");
    auto failed_learning = failing.finish(candidate(failing, "米"));
    require(failed_learning.commit == "米" && failed_learning.diagnostic && failing.snapshot().preedit.empty(),
            "failed learning lost commit or diagnostic");
    // 英文九键:同一个键面,数字拼的是单词而不是音节。
    //
    // English is a mode rather than a scheme, so the grid stays available in it. The words are the whole answer here
    // rather than an addition to a pinyin list, which is why they appear with mixed candidates switched off and from
    // the very first digit.
    auto english_options = options;
    english_options.english.mixed_candidates = false;
    english_options.english.minimum_prefix = 2;
    Session words(english_options);
    words.set_nine_key_enabled(true);
    words.set_dedicated_english(true);
    type(words, "65");
    require(candidate(words, "ok") < candidate(words, "old"), "english nine-key ranks the exact code first");
    for (const auto &item : words.snapshot().candidates)
        require(item.word != "\u4f60" && item.word != "\u7c73", "pinyin candidates leaked into english nine-key");
    require(words.snapshot().nine_key_spellings.empty(), "english nine-key offered pinyin spellings");
    // 键面靠这个标志画:九键一激活就把整个会话的快照顶掉,不带上模式的话第一个数字就把中文键面换回来了。
    require(words.snapshot().dedicated_english, "the grid's snapshot dropped the English mode");
    words.command(Command::Cancel);

    // 一个数字就该出词:混排时用来压噪音的前缀长度,在只剩单词的列表里没有意义。
    type(words, "6");
    require(words.select(candidate(words, "old")).commit == "old",
            "english nine-key withheld words behind the prefix length");

    // 退出英文,音节就回来了;两个开关的先后顺序不该影响结果。
    words.set_dedicated_english(false);
    type(words, "64");
    candidate(words, "\u4f60");
    require(!words.snapshot().nine_key_spellings.empty(), "pinyin spellings did not return");
    words.command(Command::Cancel);
    words.set_nine_key_enabled(false);
    words.set_dedicated_english(true);
    words.set_nine_key_enabled(true);
    type(words, "65");
    candidate(words, "ok");
    words.command(Command::Cancel);

    std::cout << "Nine-key input contract passed\n";
}
