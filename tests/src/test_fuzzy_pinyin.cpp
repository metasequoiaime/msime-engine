#include <metasequoia/session.h>
#include "quanpin/fuzzy_pinyin.h"
#include "quanpin/quanpin_dictionary.h"
#include "user_dictionary/user_dictionary_journal.h"
#include <sqlite3.h>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <fstream>
#include <stdexcept>

using namespace metasequoia;
void require(bool value, const std::string &message)
{
    if (!value)
        throw std::runtime_error(message);
}
void type(Session &session, const std::string &text)
{
    for (char c : text)
        require(session.character(c).handled, "character rejected");
}
std::size_t index(Session &session, const std::string &word)
{
    const auto snapshot = session.snapshot();
    for (std::size_t i = 0; i < snapshot.candidates.size(); ++i)
        if (snapshot.candidates[i].word == word)
            return i;
    throw std::runtime_error("Missing " + word + " for " + snapshot.preedit);
}
bool contains(const std::vector<WordItem> &items, const std::string &word)
{
    return std::any_of(items.begin(), items.end(), [&](const auto &item) { return item.word == word; });
}
int main()
{
    const auto directory =
        std::filesystem::temp_directory_path() /
        ("msime-fuzzy-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
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
    try
    {
        sqlite3 *db = nullptr;
        require(sqlite3_open((directory / "msime.db").u8string().c_str(), &db) == SQLITE_OK, "open fixture");
        const auto insert = [&](const std::string &key, const std::string &word, std::int64_t weight = 100) {
            const auto segments = quanpin::split_segments(key);
            const auto table = quanpin::build_table_name(segments);
            std::string escaped;
            for (char c : key)
            {
                escaped += c;
                if (c == '\'')
                    escaped += c;
            }
            const auto sql = "CREATE TABLE IF NOT EXISTS " + table +
                             "(key TEXT,jp TEXT,value TEXT,weight INTEGER);INSERT INTO " + table + " VALUES('" +
                             escaped + "','','" + word + "'," + std::to_string(weight) + ");";
            require(sqlite3_exec(db, sql.c_str(), nullptr, nullptr, nullptr) == SQLITE_OK, "fixture insert " + key);
        };
        const std::vector<std::pair<std::string, std::string>> pairs = {
            {"zan", "zhan"}, {"can", "chan"}, {"san", "shan"}, {"na", "la"},      {"fa", "ha"},     {"ran", "lan"},
            {"ban", "bang"}, {"ben", "beng"}, {"bin", "bing"}, {"lian", "liang"}, {"guan", "guang"}};
        for (std::size_t i = 0; i < pairs.size(); ++i)
        {
            insert(pairs[i].first, "原" + std::to_string(i));
            insert(pairs[i].second, "糊" + std::to_string(i));
        }
        insert("zong", "宗");
        insert("zhong", "中");
        insert("guo", "国");
        insert("zhong'guo", "中国");
        // Shipped dictionary scales, pinned by upstream 809c6299 and 9bbd86b6.
        insert("xian", "先", 1662684);
        insert("xi'an", "西安", 55003);
        // Keep the strong alternative path covered when it falls outside the first page.
        insert("xian", "现", 1500000);
        insert("xian", "线", 1400000);
        insert("xian", "县", 1300000);
        insert("xian", "限", 1200000);
        insert("xian", "显", 1100000);
        insert("xian", "险", 1000000);
        insert("xie", "些", 3752167);
        insert("xie", "写", 605147);
        insert("xi'e", "西鄂", 6);
        insert("jiang", "将", 2629219);
        insert("jiang", "僵", 94955);
        insert("ji'ang", "激昂", 23740);
        insert("you'dian", "邮电", 999);
        insert("you'di'an", "尤迪安", 7);
        // Keep the best alternative segmentation inside the first page. A protection slot should not
        // move a candidate that natural ordering already placed where the user tuned it.
        insert("jian", "见", 3460998);
        insert("jian", "间", 3067939);
        insert("jian", "剑", 1151704);
        insert("jian", "件", 935235);
        insert("jian", "建", 598616);
        insert("jian", "检", 500000);
        insert("ji'an", "吉安", 766925);
        insert("ji'an", "积案", 9420);
        sqlite3_close(db);
        std::filesystem::create_directories(directory / "helpcodes");
        std::ofstream(directory / "helpcodes" / "helpcode.txt") << "中=ab\n宗=cd\n国=ef\n";
        RuntimePaths paths{directory, directory, directory, directory};
        QuanpinDictionary dictionary({}, paths);
        for (std::size_t i = 0; i < pairs.size(); ++i)
        {
            FuzzyPinyinOptions fuzzy{1u << i};
            const auto forward = dictionary.query(pairs[i].first, pairs[i].first, false, fuzzy);
            require(contains(forward, "糊" + std::to_string(i)), "missing forward rule " + std::to_string(i));
            require(
                contains(dictionary.query(pairs[i].second, pairs[i].second, false, fuzzy), "原" + std::to_string(i)),
                "missing reverse rule");
            require(!contains(dictionary.query(pairs[i].first, pairs[i].first, 0u), "糊" + std::to_string(i)),
                    "fuzzy polluted exact cache");
            require(!contains(dictionary.query(pairs[i].first, pairs[i].first, false,
                                               FuzzyPinyinOptions{1u << ((i + 1) % pairs.size())}),
                              "糊" + std::to_string(i)),
                    "unselected rule expanded");
        }
        const auto position = [](const std::vector<WordItem> &items, const std::string &word) {
            const auto found =
                std::find_if(items.begin(), items.end(), [&](const auto &item) { return item.word == word; });
            require(found != items.end(), "missing ranking fixture");
            return std::distance(items.begin(), found);
        };
        const FuzzyPinyinOptions fuzzy_on{1u};
        const auto xian_list = dictionary.query("xian", "xian", 0u, fuzzy_on);
        require(xian_list.at(0).word == "先" && xian_list.at(1).word == "西安",
                "real ambiguity lost its protected slot");
        const auto xie_list = dictionary.query("xie", "xie", 0u, fuzzy_on);
        require(xie_list.at(0).word == "些" && xie_list.at(1).word == "写" && position(xie_list, "西鄂") > 1,
                "rare re-segmentation outranked the exact reading");
        const auto jiang_list = dictionary.query("jiang", "jiang", 0u, fuzzy_on);
        require(jiang_list.at(0).word == "将" && jiang_list.at(1).word == "僵" && position(jiang_list, "激昂") > 1,
                "rare homophone word outranked the exact reading");
        const auto jian_list = dictionary.query("jian", "jian", 0u, fuzzy_on);
        require(position(jian_list, "吉安") == 4, "protected slot overrode a tuned candidate's earned rank");
        const auto youdian_list = dictionary.query("youdian", "you'dian", 0u, fuzzy_on);
        require(youdian_list.at(0).word == "邮电" && position(youdian_list, "尤迪安") > 0,
                "longer alternative key outranked the exact reading");
        const std::vector<WordItem> mixed_scale = {
            {"xi'e", "西鄂", 6, CandidateSource::Database, "xi'e"},
            {"xie", "些", 3752167, CandidateSource::Database, "xie"},
            {"xie", "写", 605147, CandidateSource::Database, "xie"},
        };
        bool changed = false;
        require(user_dictionary::adjust_candidate_ranking((directory / "msime.db").u8string(),
                                                          (directory / "msime_user.db").u8string(), "xie", mixed_scale,
                                                          "xie", "写", "pin", 1, 1, true, &changed) &&
                    changed,
                "pin did not update selected candidate");
        require(sqlite3_open((directory / "msime.db").u8string().c_str(), &db) == SQLITE_OK, "open ranking fixture");
        const auto weight = [&](const char *sql) {
            sqlite3_stmt *stmt = nullptr;
            require(sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK, "prepare ranking query");
            require(sqlite3_step(stmt) == SQLITE_ROW, "missing ranking row");
            const auto result = sqlite3_column_int64(stmt, 0);
            sqlite3_finalize(stmt);
            return result;
        };
        require(weight("SELECT weight FROM tbl_1_x WHERE value='写'") > 3752167, "pin used a different syllable scale");
        require(weight("SELECT weight FROM tbl_2_x WHERE value='西鄂'") == 6, "pin modified a different key");
        changed = false;
        require(user_dictionary::adjust_candidate_ranking((directory / "msime.db").u8string(),
                                                          (directory / "msime_user.db").u8string(), "xie", mixed_scale,
                                                          "xie", "些", "pin", 1, 1, true, &changed) &&
                    !changed,
                "same-scale leader was unnecessarily changed");
        require(weight("SELECT weight FROM tbl_1_x WHERE value='些'") == 3752167, "same-scale leader was demoted");
        sqlite3_close(db);
        require(quanpin::fuzzy_syllables("zh", {0x7ff}) == std::vector<std::string>{"zh"},
                "incomplete initial changed");
        require(quanpin::fuzzy_syllables("bian", {1u << 6}) == std::vector<std::string>{"bian"},
                "an rule changed ian final");
        require(quanpin::fuzzy_segmentations(quanpin::Segments(20, "lan"), {0x7ff}).size() <= 63,
                "unbounded fuzzy beam");
        SessionOptions options;
        options.paths = paths;
        options.helpcode = false;
        options.autocorrect_types = 0;
        options.learning = false;
        options.fuzzy_pinyin.rules = 1;
        Session session(options);
        type(session, "zongguo");
        auto view = session.snapshot();
        const auto selected = view.candidates[index(session, "中国")];
        require(selected.pinyin == "zong'guo" && selected.canonical_pinyin == "zhong'guo",
                "typed/canonical identity lost");
        require(session.select(index(session, "中")).commit == "中" && session.snapshot().editing_text == "guo",
                "partial selection consumed canonical length");
        require(session.finish().commit == "国", "remaining composition failed");
        type(session, "zhong");
        require(session.select(index(session, "宗")).commit == "宗" && session.snapshot().preedit.empty(),
                "reverse selection left input behind");
        session.set_nine_key_enabled(true);
        type(session, "9664");
        require(session.select(index(session, "中")).commit == "中" && session.snapshot().preedit.empty(),
                "nine-key fuzzy consumed wrong digit count");
        options.scheme = SchemeType::Shuangpin;
        Session doublePinyin(options);
        type(doublePinyin, "zsgo");
        require(doublePinyin.select(index(doublePinyin, "中")).commit == "中" &&
                    doublePinyin.snapshot().editing_text == "go",
                "shuangpin fuzzy consumption");
        require(doublePinyin.finish().commit == "国", "shuangpin suffix failed");
        options.helpcode = true;
        Session helped(options);
        type(helped, "zsaB");
        require(helped.select(index(helped, "中")).commit == "中" && helped.snapshot().preedit.empty(),
                "shuangpin helpcode fuzzy selection");
        options.scheme = SchemeType::Quanpin;
        Session helpedFull(options);
        type(helpedFull, "zongAB");
        require(helpedFull.select(index(helpedFull, "中")).commit == "中" && helpedFull.snapshot().preedit.empty(),
                "quanpin helpcode fuzzy selection");
        options.helpcode = false;
        options.learning = true;
        Session learned(options);
        type(learned, "zongguo");
        require(learned.select(index(learned, "中")).commit == "中", "learn prefix");
        require(learned.select(index(learned, "国")).commit == "国", "learn suffix");
        require(dictionary.find_candidate("zhong'guo", "中国").has_value(), "canonical phrase lost");
        require(!dictionary.find_candidate("zong'guo", "中国").has_value(), "learned mistyped pronunciation");
        options.scheme = SchemeType::Shuangpin;
        options.fuzzy_pinyin.rules = 0;
        Session exact(options);
        type(exact, "zsgo");
        require(!contains(exact.snapshot().candidates, "中国"), "session configuration leaked");
        // SessionOptions::shuangpin_preedit_uses_raw rewrites SessionSnapshot::preedit, the string every frontend
        // renders. Both branches are pinned below, plus the local-mode and dedicated-English guards that keep the
        // rewrite out of compositions that are not shuangpin pinyin.
        SessionOptions shuangpinDisplay;
        shuangpinDisplay.paths = paths;
        shuangpinDisplay.scheme = SchemeType::Shuangpin;
        shuangpinDisplay.helpcode = false;
        shuangpinDisplay.autocorrect_types = 0;
        shuangpinDisplay.learning = false;
        shuangpinDisplay.fuzzy_pinyin.rules = 1;
        shuangpinDisplay.shuangpin_preedit_uses_raw = true;
        Session rawPreedit(shuangpinDisplay);
        type(rawPreedit, "zsgo");
        const auto rawView = rawPreedit.snapshot();
        // The default shows the plain typed keys, not the segmented raw input and not the pinyin.
        require(rawView.preedit == "zsgo",
                "shuangpin_preedit_uses_raw=true stopped rendering the typed keys, preedit is " + rawView.preedit);
        require(rawView.raw_segmentation == "zs'go" && rawView.normalized_segmentation == "zong'guo",
                "shuangpin segmentation fields changed: " + rawView.raw_segmentation + " / " +
                    rawView.normalized_segmentation);
        shuangpinDisplay.shuangpin_preedit_uses_raw = false;
        Session convertedPreedit(shuangpinDisplay);
        type(convertedPreedit, "zsgo");
        const auto convertedView = convertedPreedit.snapshot();
        require(convertedView.preedit == "zong'guo",
                "shuangpin_preedit_uses_raw=false stopped rendering the converted pinyin, preedit is " +
                    convertedView.preedit);
        require(convertedView.raw_segmentation == "zs'go" && convertedView.normalized_segmentation == "zong'guo",
                "the converted preedit overwrote the segmentation fields");
        // The rewrite is guarded by local_input_mode() == None; without the guard the Unicode preedit would be replaced
        // by the pinyin segmentation of an untouched composition.
        Session convertedUnicode(shuangpinDisplay);
        require(convertedUnicode.character('U', true).handled, "Shift+U was rejected by a shuangpin session");
        type(convertedUnicode, "4e2d");
        const auto unicodeView = convertedUnicode.snapshot();
        require(unicodeView.local_mode == LocalInputMode::Unicode, "Shift+U did not enter Unicode mode");
        require(unicodeView.preedit == "U4e2d",
                "the converted shuangpin preedit leaked into Unicode mode, preedit is " + unicodeView.preedit);
        // Same for dedicated English, whose preedit is the ASCII the user typed rather than pinyin.
        Session convertedEnglish(shuangpinDisplay);
        convertedEnglish.set_dedicated_english(true);
        type(convertedEnglish, "zs");
        const auto englishView = convertedEnglish.snapshot();
        require(englishView.dedicated_english && englishView.preedit == "zs",
                "the converted shuangpin preedit leaked into dedicated English mode, preedit is " +
                    englishView.preedit);
        const auto start = std::chrono::steady_clock::now();
        for (int i = 0; i < 200; ++i)
            dictionary.query("zongguo", "zong'guo", false, {0x7ff});
        const auto elapsed =
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
        require(elapsed < 5000, "warm fuzzy queries exceeded 25 ms per query");
        std::cout << "200 warm fuzzy queries: " << elapsed << " ms\n";
        std::cout << "Fuzzy pinyin rules, cache isolation and session selection passed\n";
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
