#include "../contracts/assets/assets.h"
#include "english_dictionary.h"
#include "../common/string_utils.h"
#include "../core/data_path.h"
#include "../common/sqlite_database.h"
#include "../common/sqlite_statement.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <limits>
#include <spdlog/spdlog.h>
#include <utility>

namespace
{
using Statement = metasequoia::SqliteStatement;

bool IsLowerAsciiWord(const std::string &value)
{
    return !value.empty() && std::all_of(value.begin(), value.end(),
                                         [](unsigned char ch) { return CommonUtils::is_ascii_lowercase(ch); });
}

} // namespace

EnglishDictionary::EnglishDictionary(std::string db_path, bool initialize_schema, std::string translations_path,
                                     std::string gloss_cache_path)
    : translations_path_(std::move(translations_path)), db_path_(std::move(db_path)),
      gloss_cache_path_(std::move(gloss_cache_path))
{
    if (initialize_schema)
    {
        (void)ensure_schema(db_path_);
    }
    load_custom_translations();
}

EnglishDictionary::~EnglishDictionary()
{
    close_database();
}

std::vector<WordItem> EnglishDictionary::query_prefix(const std::string &prefix, size_t limit)
{
    if (!IsLowerAsciiWord(prefix) || limit == 0 || !ensure_query_statement())
    {
        return {};
    }

    const std::string upper_bound = prefix + "{";
    const int sqlite_limit =
        static_cast<int>((std::min)(limit, static_cast<size_t>((std::numeric_limits<int>::max)())));

    sqlite3_reset(query_statement_.get());
    sqlite3_clear_bindings(query_statement_.get());
    if (sqlite3_bind_text(query_statement_.get(), 1, prefix.c_str(), -1, SQLITE_TRANSIENT) != SQLITE_OK ||
        sqlite3_bind_text(query_statement_.get(), 2, upper_bound.c_str(), -1, SQLITE_TRANSIENT) != SQLITE_OK ||
        sqlite3_bind_int(query_statement_.get(), 3, sqlite_limit) != SQLITE_OK)
    {
        sqlite3_reset(query_statement_.get());
        return {};
    }

    std::vector<WordItem> candidates;
    int result = SQLITE_ROW;
    while ((result = sqlite3_step(query_statement_.get())) == SQLITE_ROW)
    {
        const auto *word = reinterpret_cast<const char *>(sqlite3_column_text(query_statement_.get(), 0));
        const auto *display = reinterpret_cast<const char *>(sqlite3_column_text(query_statement_.get(), 1));
        if (word == nullptr || display == nullptr)
        {
            continue;
        }
        candidates.emplace_back(word, display, sqlite3_column_int64(query_statement_.get(), 2),
                                CandidateSource::EnglishDictionary);
    }

    sqlite3_reset(query_statement_.get());
    if (result != SQLITE_DONE)
    {
        (void)0;
        return {};
    }
    return candidates;
}

namespace
{
std::string QueryGloss(sqlite3_stmt *statement, const std::string &key)
{
    sqlite3_reset(statement);
    sqlite3_clear_bindings(statement);
    if (sqlite3_bind_text(statement, 1, key.c_str(), -1, SQLITE_TRANSIENT) != SQLITE_OK)
        return {};
    std::string result;
    if (sqlite3_step(statement) == SQLITE_ROW)
    {
        const auto *value = reinterpret_cast<const char *>(sqlite3_column_text(statement, 0));
        if (value != nullptr)
            result = value;
    }
    sqlite3_reset(statement);
    return result;
}
} // namespace

// 三层,顺序就是权威性顺序:用户自己写的 custom_translations.txt 最大,出货词库的 ECDICT 次之,
// 联网补回来的缓存最后 —— 缓存里的东西恰恰是词库查不到的那些,质量最没保证,不该盖掉前两层。
std::string EnglishDictionary::query_chinese_gloss(const std::string &english)
{
    const auto custom = custom_en_zh_.find(english);
    if (custom != custom_en_zh_.end())
        return custom->second;
    if (english.empty())
        return {};
    if (ensure_gloss_statements())
    {
        auto gloss = QueryGloss(en_zh_statement_.get(), english);
        if (!gloss.empty())
            return gloss;
    }
    return ensure_cache_statements() ? QueryGloss(cache_en_zh_statement_.get(), english) : std::string{};
}

std::string EnglishDictionary::query_english_gloss(const std::string &chinese)
{
    const auto custom = custom_zh_en_.find(chinese);
    if (custom != custom_zh_en_.end())
        return custom->second;
    if (chinese.empty())
        return {};
    if (ensure_gloss_statements())
    {
        auto gloss = QueryGloss(zh_en_statement_.get(), chinese);
        if (!gloss.empty())
            return gloss;
    }
    return ensure_cache_statements() ? QueryGloss(cache_zh_en_statement_.get(), chinese) : std::string{};
}

bool EnglishDictionary::cache_gloss(bool chinese_to_english, const std::string &key, const std::string &gloss)
{
    if (gloss_cache_path_.empty() || !upsert_gloss(gloss_cache_path_, chinese_to_english, key, gloss))
        return false;
    // 读句柄是只读打开的,而且可能是在缓存文件还不存在的时候就失败过一次。写完直接关掉,
    // 下次查询重新打开,省得去想「这条新写的什么时候能被看见」。
    close_cache();
    return true;
}

bool EnglishDictionary::upsert_gloss(const std::string &db_path, bool chinese_to_english, const std::string &key,
                                     const std::string &gloss)
{
    if (db_path.empty() || key.empty() || gloss.empty() || !ensure_schema(db_path))
        return false;

    sqlite3 *database = nullptr;
    const int status =
        sqlite3_open_v2(db_path.c_str(), &database, SQLITE_OPEN_READWRITE | SQLITE_OPEN_NOMUTEX, nullptr);
    metasequoia::SqliteDatabase connection(database);
    if (status != SQLITE_OK)
        return false;
    sqlite3_busy_timeout(connection.get(), 250);
    sqlite3_stmt *statement = nullptr;
    const char *sql = chinese_to_english ? "INSERT OR REPLACE INTO zh_en_glosses(chinese,english_gloss) VALUES(?1,?2)"
                                         : "INSERT OR REPLACE INTO en_zh_glosses(english,chinese_gloss) VALUES(?1,?2)";
    const bool prepared = sqlite3_prepare_v2(connection.get(), sql, -1, &statement, nullptr) == SQLITE_OK;
    bool ok = false;
    {
        Statement guard(statement);
        ok = prepared && sqlite3_bind_text(statement, 1, key.c_str(), -1, SQLITE_TRANSIENT) == SQLITE_OK &&
             sqlite3_bind_text(statement, 2, gloss.c_str(), -1, SQLITE_TRANSIENT) == SQLITE_OK &&
             sqlite3_step(statement) == SQLITE_DONE;
    }
    return ok;
}

void EnglishDictionary::load_custom_translations()
{
    custom_en_zh_.clear();
    custom_zh_en_.clear();
    if (db_path_.empty())
        return;

    const auto sidecar = translations_path_.empty() ? metasequoia::path_from_utf8(db_path_.c_str()).parent_path() /
                                                          metasequoia::assets::translations
                                                    : metasequoia::path_from_utf8(translations_path_.c_str());
    std::ifstream input(sidecar, std::ios::binary);
    if (!input)
        return;

    std::string text((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    if (text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xEF &&
        static_cast<unsigned char>(text[1]) == 0xBB && static_cast<unsigned char>(text[2]) == 0xBF)
    {
        text.erase(0, 3);
    }

    size_t begin = 0;
    while (begin < text.size())
    {
        size_t end = text.find_first_of("\r\n", begin);
        if (end == std::string::npos)
            end = text.size();
        std::string line = text.substr(begin, end - begin);
        if (end < text.size() && text[end] == '\r' && end + 1 < text.size() && text[end + 1] == '\n')
            begin = end + 2;
        else
            begin = end == text.size() ? text.size() : end + 1;

        while (!line.empty() && std::isspace(static_cast<unsigned char>(line.front())))
            line.erase(line.begin());
        while (!line.empty() && std::isspace(static_cast<unsigned char>(line.back())))
            line.pop_back();
        if (line.empty() || line.front() == '#')
            continue;

        const auto tab = line.find('\t');
        if (tab == std::string::npos || tab == 0 || tab + 1 >= line.size())
            continue;
        std::string source = line.substr(0, tab);
        std::string gloss = line.substr(tab + 1);
        while (!source.empty() && std::isspace(static_cast<unsigned char>(source.back())))
            source.pop_back();
        while (!gloss.empty() && std::isspace(static_cast<unsigned char>(gloss.front())))
            gloss.erase(gloss.begin());
        while (!gloss.empty() && std::isspace(static_cast<unsigned char>(gloss.back())))
            gloss.pop_back();
        if (source.empty() || gloss.empty())
            continue;

        const bool chinese_source =
            std::any_of(source.begin(), source.end(), [](unsigned char ch) { return ch >= 0x80; });
        if (chinese_source)
            custom_zh_en_[std::move(source)] = std::move(gloss);
        else
            custom_en_zh_[std::move(source)] = std::move(gloss);
    }
}

bool EnglishDictionary::ensure_schema(const std::string &db_path)
{
    sqlite3 *database = nullptr;
    const int status = sqlite3_open_v2(db_path.c_str(), &database, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr);
    metasequoia::SqliteDatabase connection(database);
    if (status != SQLITE_OK)
        return false;

    bool has_weight = false;
    bool composite_primary_key = false;
    bool has_table = false;
    {
        sqlite3_stmt *columns = nullptr;
        if (sqlite3_prepare_v2(connection.get(), "PRAGMA table_info(english_words)", -1, &columns, nullptr) ==
            SQLITE_OK)
        {
            Statement guard(columns);
            int primary_key_columns = 0;
            while (sqlite3_step(columns) == SQLITE_ROW)
            {
                has_table = true;
                const auto *name = reinterpret_cast<const char *>(sqlite3_column_text(columns, 1));
                has_weight = has_weight || (name != nullptr && std::string(name) == "weight");
                if (sqlite3_column_int(columns, 5) > 0)
                    ++primary_key_columns;
            }
            composite_primary_key = primary_key_columns == 2;
        }
    }

    bool english_words_ok = false;
    if (!has_table)
    {
        english_words_ok = sqlite3_exec(connection.get(),
                                        "CREATE TABLE english_words("
                                        "word TEXT COLLATE BINARY NOT NULL,display TEXT NOT NULL,"
                                        "weight INTEGER NOT NULL DEFAULT 0,PRIMARY KEY(word,display)) WITHOUT ROWID;",
                                        nullptr, nullptr, nullptr) == SQLITE_OK;
    }
    else if (has_weight && composite_primary_key)
    {
        english_words_ok = true;
    }
    else
    {
        const char *copy_sql = has_weight ? "INSERT OR IGNORE INTO english_words_new(word,display,weight) "
                                            "SELECT word,display,weight FROM english_words;"
                                          : "INSERT OR IGNORE INTO english_words_new(word,display,weight) "
                                            "SELECT word,display,0 FROM english_words;";
        english_words_ok =
            sqlite3_exec(connection.get(), "BEGIN IMMEDIATE", nullptr, nullptr, nullptr) == SQLITE_OK &&
            sqlite3_exec(connection.get(),
                         "CREATE TABLE english_words_new("
                         "word TEXT COLLATE BINARY NOT NULL,display TEXT NOT NULL,"
                         "weight INTEGER NOT NULL DEFAULT 0,PRIMARY KEY(word,display)) WITHOUT ROWID",
                         nullptr, nullptr, nullptr) == SQLITE_OK &&
            sqlite3_exec(connection.get(), copy_sql, nullptr, nullptr, nullptr) == SQLITE_OK &&
            sqlite3_exec(connection.get(), "DROP TABLE english_words", nullptr, nullptr, nullptr) == SQLITE_OK &&
            sqlite3_exec(connection.get(), "ALTER TABLE english_words_new RENAME TO english_words", nullptr, nullptr,
                         nullptr) == SQLITE_OK;
        sqlite3_exec(connection.get(), english_words_ok ? "COMMIT" : "ROLLBACK", nullptr, nullptr, nullptr);
    }

    const bool gloss_tables_ok =
        english_words_ok &&
        sqlite3_exec(connection.get(),
                     "CREATE TABLE IF NOT EXISTS en_zh_glosses("
                     "english TEXT COLLATE BINARY PRIMARY KEY,chinese_gloss TEXT NOT NULL) WITHOUT ROWID;"
                     "CREATE TABLE IF NOT EXISTS zh_en_glosses("
                     "chinese TEXT COLLATE BINARY PRIMARY KEY,english_gloss TEXT NOT NULL) WITHOUT ROWID;"
                     "PRAGMA user_version=3;",
                     nullptr, nullptr, nullptr) == SQLITE_OK;
    return gloss_tables_ok;
}

bool EnglishDictionary::ensure_query_statement()
{
    if (query_statement_ != nullptr)
    {
        return true;
    }

    if (!db_)
    {
        auto opened = metasequoia::sqlite_open_database(db_path_, SQLITE_OPEN_READONLY | SQLITE_OPEN_NOMUTEX);
        if (!opened)
        {
            close_database();
            return false;
        }
        db_ = std::move(opened);
    }

    constexpr const char *query_sql =
        "SELECT word,display,weight FROM english_words "
        "WHERE word >= ?1 AND word < ?2 "
        "ORDER BY CASE WHEN word = ?1 THEN 0 ELSE 1 END, weight DESC, length(word), word, display "
        "LIMIT ?3";
    auto query = metasequoia::sqlite_prepare_statement(db_.get(), query_sql);
    if (!query)
    {
        close_database();
        return false;
    }
    query_statement_ = std::move(query);
    return true;
}

bool EnglishDictionary::ensure_gloss_statements()
{
    if (en_zh_statement_ != nullptr && zh_en_statement_ != nullptr)
        return true;
    if (!ensure_query_statement())
        return false;
    auto en_zh =
        metasequoia::sqlite_prepare_statement(db_.get(), "SELECT chinese_gloss FROM en_zh_glosses WHERE english=?1");
    if (!en_zh)
    {
        close_database();
        return false;
    }
    en_zh_statement_ = std::move(en_zh);
    auto zh_en =
        metasequoia::sqlite_prepare_statement(db_.get(), "SELECT english_gloss FROM zh_en_glosses WHERE chinese=?1");
    if (!zh_en)
    {
        close_database();
        return false;
    }
    zh_en_statement_ = std::move(zh_en);
    return true;
}

// 缓存文件是用户目录下的,可能还不存在(一次都没联网补过释义)。这里只读打开、不创建:
// 没有就当没有,查询照常回落到空结果。
bool EnglishDictionary::ensure_cache_statements()
{
    if (cache_en_zh_statement_ != nullptr && cache_zh_en_statement_ != nullptr)
        return true;
    if (gloss_cache_path_.empty())
        return false;
    if (!cache_db_)
    {
        auto opened = metasequoia::sqlite_open_database(gloss_cache_path_, SQLITE_OPEN_READONLY | SQLITE_OPEN_NOMUTEX);
        if (!opened)
        {
            close_cache();
            return false;
        }
        cache_db_ = std::move(opened);
    }
    auto cache_en_zh = metasequoia::sqlite_prepare_statement(
        cache_db_.get(), "SELECT chinese_gloss FROM en_zh_glosses WHERE english=?1");
    if (!cache_en_zh)
    {
        close_cache();
        return false;
    }
    cache_en_zh_statement_ = std::move(cache_en_zh);
    auto cache_zh_en = metasequoia::sqlite_prepare_statement(
        cache_db_.get(), "SELECT english_gloss FROM zh_en_glosses WHERE chinese=?1");
    if (!cache_zh_en)
    {
        close_cache();
        return false;
    }
    cache_zh_en_statement_ = std::move(cache_zh_en);
    return true;
}

void EnglishDictionary::close_cache()
{
    cache_en_zh_statement_.reset();
    cache_zh_en_statement_.reset();
    cache_db_.reset();
}

void EnglishDictionary::close_database()
{
    close_cache();
    en_zh_statement_.reset();
    zh_en_statement_.reset();
    query_statement_.reset();
    db_.reset();
}
