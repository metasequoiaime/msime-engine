#include "../../local_modes/date_time_query.h"
#include "../../local_modes/emoji_query.h"
#include "../../local_modes/kaomoji_query.h"
#include "../../local_modes/local_database.h"
#include "../../local_modes/quick_phrase_query.h"
#include "../../core/data_path.h"

#include <sqlite3.h>

#include <array>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>

namespace
{
using metasequoia::local_modes::LocalDateTime;

class Database
{
  public:
    explicit Database(const std::filesystem::path &path)
    {
        if (sqlite3_open(metasequoia::path_to_utf8(path).c_str(), &database_) != SQLITE_OK)
        {
            throw std::runtime_error("Failed to create the quick-phrase test database.");
        }
    }

    ~Database()
    {
        sqlite3_close(database_);
    }

    void execute(const char *sql)
    {
        char *error = nullptr;
        if (sqlite3_exec(database_, sql, nullptr, nullptr, &error) != SQLITE_OK)
        {
            const std::string message = error == nullptr ? "SQLite operation failed." : error;
            sqlite3_free(error);
            throw std::runtime_error(message);
        }
    }

  private:
    sqlite3 *database_ = nullptr;
};

void require(bool condition, const char *message)
{
    if (!condition)
    {
        throw std::runtime_error(message);
    }
}

void set_data_directory(const std::filesystem::path &directory)
{
#ifdef _WIN32
    if (_wputenv_s(L"METASEQUOIA_IME_DATA_DIR", directory.c_str()) != 0)
    {
        throw std::runtime_error("Failed to set the test data directory.");
    }
#else
    if (setenv("METASEQUOIA_IME_DATA_DIR", metasequoia::path_to_utf8(directory).c_str(), 1) != 0)
    {
        throw std::runtime_error("Failed to set the test data directory.");
    }
#endif
}

LocalDateTime sample_time()
{
    return {2026, 8, 9, 0, 14, 30, 0};
}

template <std::size_t Size>
void require_words(const std::vector<WordItem> &actual, const std::array<const char *, Size> &expected,
                   const char *message)
{
    require(actual.size() == expected.size(), message);
    for (std::size_t index = 0; index < expected.size(); ++index)
    {
        require(actual[index].word == expected[index] && actual[index].source == CandidateSource::Generated &&
                    actual[index].weight == static_cast<std::int64_t>(expected.size() - index),
                message);
    }
}

// Structural invariant for generated date/time candidates: the list holds exactly the expected number of rows, no row
// carries a blank word (a blank row would render in the candidate window and commit an empty string), and the weights
// stay a contiguous descending run so the window keeps its order even when a row is suppressed.
void require_generated_candidates(const std::vector<WordItem> &actual, std::size_t expected_size, const char *message)
{
    require(actual.size() == expected_size, message);
    for (std::size_t index = 0; index < actual.size(); ++index)
    {
        require(!actual[index].word.empty() && actual[index].source == CandidateSource::Generated &&
                    actual[index].weight == static_cast<std::int64_t>(actual.size() - index),
                message);
    }
}
} // namespace

int main()
{
    const LocalDateTime now = sample_time();
    const std::array<const char *, 17> expected_dates = {
        "2026年8月9日",
        "2026-08-09",
        "2026/08/09",
        "2026.08.09",
        "20260809",
        "26年8月9日",
        "8月9日",
        "08-09",
        "0809",
        "2026年8月9日 星期日",
        "8月9日 周日",
        "2026-08-09 Sun",
        "2026-08-09 14:30",
        "8月9日 14:30",
        "二〇二六年八月九日",
        "贰零贰陆年捌月零玖日",
        "丙午年六月二十七日",
    };
    for (const char *keyword : std::array<const char *, 3>{"rq", "riqi", "date"})
    {
        require_words(metasequoia::local_modes::query_date_time(keyword, &now), expected_dates,
                      "A date alias did not preserve the Windows candidate order.");
    }

    const std::array<const char *, 13> expected_times = {
        "14:30",
        "14:30:00",
        "1430",
        "143000",
        "下午2:30",
        "下午2点30分",
        "下午两点半",
        "2:30 PM",
        "2:30pm",
        "02:30:00 PM",
        "2026-08-09 14:30:00",
        "2026年8月9日 14:30",
        "8月9日 下午2:30",
    };
    for (const char *keyword : std::array<const char *, 3>{"sj", "shijian", "time"})
    {
        require_words(metasequoia::local_modes::query_date_time(keyword, &now), expected_times,
                      "A time alias did not preserve the Windows candidate order.");
    }

    const std::array<const char *, 4> expected_sunday = {"星期日", "星期天", "Sunday", "Sun"};
    for (const char *keyword : std::array<const char *, 3>{"xq", "xingqi", "week"})
    {
        require_words(metasequoia::local_modes::query_date_time(keyword, &now), expected_sunday,
                      "A weekday alias did not preserve the Windows candidate order.");
    }

    LocalDateTime monday = now;
    monday.weekday = 1;
    const std::array<const char *, 3> expected_monday = {"星期一", "Monday", "Mon"};
    require_words(metasequoia::local_modes::query_date_time("week", &monday), expected_monday,
                  "Monday candidates were formatted incorrectly.");

    require(!metasequoia::local_modes::is_date_time_keyword("today") &&
                metasequoia::local_modes::is_date_time_keyword("week"),
            "Date/time keyword recognition diverged from Windows.");
    require(metasequoia::local_modes::query_date_time("today", &now).empty() &&
                metasequoia::local_modes::query_date_time("rq", &now, 0).empty() &&
                metasequoia::local_modes::query_date_time("rq", &now, -1).empty() &&
                metasequoia::local_modes::query_date_time("rq", &now, 3).size() == 3,
            "Date/time query limit or unknown-keyword handling was incorrect.");

    // The lunar table resolves only 1900-01-31..2101-01-28. Outside that window lunar_date() yields an empty string,
    // which must drop the lunar row entirely instead of shipping a blank candidate.
    const LocalDateTime last_supported_lunar_day = {2101, 1, 28, 5, 9, 5, 7};
    require_generated_candidates(metasequoia::local_modes::query_date_time("rq", &last_supported_lunar_day), 17,
                                 "The last date covered by the lunar table lost its lunar candidate.");

    const LocalDateTime first_unsupported_lunar_day = {2101, 1, 29, 6, 9, 5, 7};
    require_generated_candidates(metasequoia::local_modes::query_date_time("rq", &first_unsupported_lunar_day), 16,
                                 "The first date past the lunar table emitted a blank date candidate.");

    const LocalDateTime before_lunar_epoch = {1900, 1, 1, 1, 9, 5, 7};
    require_generated_candidates(metasequoia::local_modes::query_date_time("date", &before_lunar_epoch), 16,
                                 "A date before the lunar epoch emitted a blank date candidate.");

    const LocalDateTime far_future = {2150, 6, 15, 3, 9, 5, 7};
    const auto far_future_dates = metasequoia::local_modes::query_date_time("riqi", &far_future);
    require_generated_candidates(far_future_dates, 16, "A far-future date emitted a blank date candidate.");
    require(far_future_dates.back().word == "贰壹伍零年陆月壹伍日" && far_future_dates.back().weight == 1,
            "An unresolvable lunar date dropped the wrong row instead of the lunar one.");

    // current_local_date_time() returns an all-zero LocalDateTime when std::time or localtime_r/localtime_s fails;
    // every keyword must still produce well-formed rows.
    const LocalDateTime unreadable_clock = {};
    require_generated_candidates(metasequoia::local_modes::query_date_time("rq", &unreadable_clock), 16,
                                 "A failed clock reading emitted a blank date candidate.");
    require_generated_candidates(metasequoia::local_modes::query_date_time("sj", &unreadable_clock), 13,
                                 "A failed clock reading emitted a blank time candidate.");
    require_generated_candidates(metasequoia::local_modes::query_date_time("xq", &unreadable_clock), 4,
                                 "A failed clock reading emitted a blank weekday candidate.");

    const auto suffix = std::to_string(std::chrono::high_resolution_clock::now().time_since_epoch().count());
    const std::filesystem::path quick_phrase_directory =
        std::filesystem::temp_directory_path() / ("metasequoia-quick-phrase-" + suffix);
    std::filesystem::create_directories(quick_phrase_directory);
    const std::filesystem::path quick_phrase_database = quick_phrase_directory / "msime.db";
    {
        Database database(quick_phrase_database);
        database.execute("CREATE TABLE quick_parases(key TEXT,value TEXT,weight INTEGER)");
        database.execute("INSERT INTO quick_parases VALUES('ab','highest weight',20)");
        database.execute("INSERT INTO quick_parases VALUES('aa','tie b',10)");
        database.execute("INSERT INTO quick_parases VALUES('aa','tie a',10)");
        database.execute("INSERT INTO quick_parases VALUES('b','outside prefix',100)");
    }

    const auto quick_phrases = metasequoia::local_modes::query_quick_phrases("a", quick_phrase_database, 10);
    require(!quick_phrases.diagnostic.has_value() && quick_phrases.candidates.size() == 3 &&
                quick_phrases.candidates[0].pinyin == "ab" && quick_phrases.candidates[0].word == "highest weight" &&
                quick_phrases.candidates[1].pinyin == "aa" && quick_phrases.candidates[1].word == "tie a" &&
                quick_phrases.candidates[2].word == "tie b" &&
                quick_phrases.candidates[0].source == CandidateSource::QuickPhrase,
            "Quick-phrase prefix ordering diverged from Windows.");
    const auto limited_quick_phrases = metasequoia::local_modes::query_quick_phrases("a", quick_phrase_database, 2);
    require(!limited_quick_phrases.diagnostic.has_value() && limited_quick_phrases.candidates.size() == 2,
            "Quick-phrase query did not honor its limit.");
    require(metasequoia::local_modes::query_quick_phrases("", quick_phrase_database, 10).candidates.empty() &&
                metasequoia::local_modes::query_quick_phrases("A", quick_phrase_database, 10).candidates.empty() &&
                metasequoia::local_modes::query_quick_phrases("a1", quick_phrase_database, 10).candidates.empty() &&
                metasequoia::local_modes::query_quick_phrases("a", quick_phrase_database, 0).candidates.empty(),
            "Quick-phrase query accepted an invalid prefix or limit.");

    const auto missing_quick_phrases =
        metasequoia::local_modes::query_quick_phrases("secret", quick_phrase_directory / "private-missing.db", 10);
    require(missing_quick_phrases.candidates.empty() && missing_quick_phrases.diagnostic.has_value() &&
                missing_quick_phrases.diagnostic->find("secret") == std::string::npos &&
                missing_quick_phrases.diagnostic->find("private-missing") == std::string::npos,
            "A missing quick-phrase database lacked a privacy-safe diagnostic.");

    const std::filesystem::path corrupt_database = quick_phrase_directory / "private-corrupt.db";
    {
        std::ofstream stream(corrupt_database, std::ios::binary);
        stream << "not a sqlite database";
    }
    const auto corrupt_quick_phrases = metasequoia::local_modes::query_quick_phrases("secret", corrupt_database, 10);
    require(corrupt_quick_phrases.candidates.empty() && corrupt_quick_phrases.diagnostic.has_value() &&
                corrupt_quick_phrases.diagnostic->find("secret") == std::string::npos &&
                corrupt_quick_phrases.diagnostic->find("private-corrupt") == std::string::npos,
            "A corrupt quick-phrase database lacked a privacy-safe diagnostic.");

    const std::filesystem::path others_database = quick_phrase_directory / "others.db";
    {
        Database database(others_database);
        database.execute("CREATE TABLE emoji_pinyin(key TEXT,emoji TEXT,sort_order INTEGER)");
        database.execute("INSERT INTO emoji_pinyin VALUES('xiaolian','😀',10)");
        database.execute("INSERT INTO emoji_pinyin VALUES('xiaolian','😄',20)");
        database.execute("INSERT INTO emoji_pinyin VALUES('xiaolian','😀',30)");
        database.execute("INSERT INTO emoji_pinyin VALUES('xl','😀',10)");
        database.execute("INSERT INTO emoji_pinyin VALUES('laugh','😀',10)");
        database.execute("INSERT INTO emoji_pinyin VALUES('xnlm','raw shuangpin match',40)");
        database.execute("CREATE TABLE kaomoji(pinyin TEXT,jianpin TEXT,kaomoji TEXT,sort_order INTEGER)");
        database.execute("INSERT INTO kaomoji VALUES('haixiu','hx','(*/ω＼*)',10)");
        database.execute("INSERT INTO kaomoji VALUES('haixiu','hx','(^_^)',20)");
        database.execute("INSERT INTO kaomoji VALUES('haixiu','hx','(*/ω＼*)',30)");
        database.execute("INSERT INTO kaomoji VALUES('kiss','','( ˘ ³˘)♥',40)");
        database.execute("INSERT INTO kaomoji VALUES('kind','','single prefix',50)");
    }

    const auto ranked_local_candidates = metasequoia::local_modes::build_local_query_result(
        "xiaolian", {{"😀", 20}, {"😄", 30}, {"😀", 10}}, 2, CandidateSource::Emoji);
    require(ranked_local_candidates.candidates.size() == 2 && ranked_local_candidates.candidates[0].word == "😀" &&
                ranked_local_candidates.candidates[1].word == "😄" &&
                ranked_local_candidates.candidates[0].weight == 2 &&
                ranked_local_candidates.candidates[1].weight == 1 &&
                ranked_local_candidates.candidates[0].pinyin == "xiaolian" &&
                ranked_local_candidates.candidates[0].source == CandidateSource::Emoji,
            "Shared local candidate ranking did not preserve order, deduplication, or weights.");

    const auto emoji = metasequoia::local_modes::query_emoji("XIAOLIAN", SchemeType::Quanpin, others_database, 10);
    require(!emoji.diagnostic.has_value() && emoji.candidates.size() == 2 && emoji.candidates[0].word == "😀" &&
                emoji.candidates[1].word == "😄" && emoji.candidates[0].pinyin == "xiaolian" &&
                emoji.candidates[0].source == CandidateSource::Emoji,
            "Emoji lookup did not normalize, order, or deduplicate full-pinyin matches.");
    require(
        metasequoia::local_modes::query_emoji("xl", SchemeType::Quanpin, others_database, 10).candidates.front().word ==
                "😀" &&
            metasequoia::local_modes::query_emoji("laugh", SchemeType::Quanpin, others_database, 10)
                    .candidates.front()
                    .word == "😀",
        "Emoji lookup did not support jianpin and English keywords.");
    const auto shuangpin_emoji =
        metasequoia::local_modes::query_emoji("xnlm", SchemeType::Shuangpin, others_database, 10);
    require(shuangpin_emoji.candidates.size() == 3 && shuangpin_emoji.candidates[0].word == "😀" &&
                shuangpin_emoji.candidates[1].word == "😄" &&
                shuangpin_emoji.candidates[2].word == "raw shuangpin match",
            "Emoji lookup did not merge raw and Xiaohe-shuangpin prefixes in catalog order.");
    require(
        metasequoia::local_modes::query_emoji("xiaolian", SchemeType::Quanpin, others_database, 1).candidates.size() ==
                1 &&
            metasequoia::local_modes::query_emoji("", SchemeType::Quanpin, others_database, 10).candidates.empty() &&
            metasequoia::local_modes::query_emoji("bad1", SchemeType::Quanpin, others_database, 10)
                .candidates.empty() &&
            metasequoia::local_modes::query_emoji("x", SchemeType::Quanpin, others_database, 0).candidates.empty(),
        "Emoji lookup did not honor its validation and limit rules.");

    const auto kaomoji = metasequoia::local_modes::query_kaomoji("HAIXIU", SchemeType::Quanpin, others_database, 10);
    require(!kaomoji.diagnostic.has_value() && kaomoji.candidates.size() == 2 &&
                kaomoji.candidates[0].word == "(*/ω＼*)" && kaomoji.candidates[1].word == "(^_^)" &&
                kaomoji.candidates[0].pinyin == "haixiu" && kaomoji.candidates[0].source == CandidateSource::Kaomoji,
            "Kaomoji lookup did not normalize, order, or deduplicate full-pinyin matches.");
    require(
        metasequoia::local_modes::query_kaomoji("hx", SchemeType::Quanpin, others_database, 10)
                    .candidates.front()
                    .word == "(*/ω＼*)" &&
            metasequoia::local_modes::query_kaomoji("kiss", SchemeType::Quanpin, others_database, 10)
                    .candidates.front()
                    .word == "( ˘ ³˘)♥" &&
            !metasequoia::local_modes::query_kaomoji("k", SchemeType::Quanpin, others_database, 10).candidates.empty(),
        "Kaomoji lookup did not support jianpin, English, and single-letter prefixes.");
    const auto shuangpin_kaomoji =
        metasequoia::local_modes::query_kaomoji("hx", SchemeType::Shuangpin, others_database, 10);
    require(shuangpin_kaomoji.candidates.size() == 2 && shuangpin_kaomoji.candidates.front().word == "(*/ω＼*)",
            "Kaomoji lookup did not expand Xiaohe shuangpin or deduplicate merged matches.");

    const auto missing_emoji = metasequoia::local_modes::query_emoji(
        "privatecode", SchemeType::Quanpin, quick_phrase_directory / "private-others-missing.db", 10);
    require(missing_emoji.candidates.empty() && missing_emoji.diagnostic.has_value() &&
                missing_emoji.diagnostic->find("privatecode") == std::string::npos &&
                missing_emoji.diagnostic->find("private-others-missing") == std::string::npos,
            "A missing Emoji database lacked a privacy-safe diagnostic.");
    const auto corrupt_kaomoji =
        metasequoia::local_modes::query_kaomoji("privatecode", SchemeType::Quanpin, corrupt_database, 10);
    require(corrupt_kaomoji.candidates.empty() && corrupt_kaomoji.diagnostic.has_value() &&
                corrupt_kaomoji.diagnostic->find("privatecode") == std::string::npos &&
                corrupt_kaomoji.diagnostic->find("private-corrupt") == std::string::npos,
            "A corrupt kaomoji database lacked a privacy-safe diagnostic.");

    // The shipped dictionary names are cached, while fixture paths stay
    // operation-scoped so a test can remove its temporary directory right
    // after a query. Holding the first connection proves that close clears
    // the cache without invalidating an in-flight reader.
    set_data_directory(quick_phrase_directory);
    metasequoia::local_modes::close_cached_local_databases();
    auto first_connection = metasequoia::local_modes::open_local_database(quick_phrase_database);
    auto second_connection = metasequoia::local_modes::open_local_database(quick_phrase_database);
    require(first_connection && second_connection && first_connection.get() == second_connection.get(),
            "The active shipped dictionary did not reuse its read-only connection.");
    metasequoia::local_modes::close_cached_local_databases();
    auto third_connection = metasequoia::local_modes::open_local_database(quick_phrase_database);
    require(third_connection && third_connection.get() != first_connection.get(),
            "Closing cached local databases did not release the previous generation.");
    metasequoia::local_modes::close_cached_local_databases();
    first_connection.reset();
    second_connection.reset();
    third_connection.reset();
    std::filesystem::remove_all(quick_phrase_directory);
    return 0;
}
