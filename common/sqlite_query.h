#pragma once

#include "sqlite_statement.h"
#include "../core/word_item.h"

#include <sqlite3.h>

#include <optional>
#include <string>
#include <vector>

namespace metasequoia
{
namespace detail
{
inline std::optional<WordItem> sqlite_read_word_item_impl(sqlite3_stmt *statement, CandidateSource source,
                                                          const char *pinyin_override, bool require_key)
{
    if (statement == nullptr)
        return std::nullopt;
    const auto *key = reinterpret_cast<const char *>(sqlite3_column_text(statement, 0));
    const auto *value = reinterpret_cast<const char *>(sqlite3_column_text(statement, 1));
    if (value == nullptr || (require_key && key == nullptr))
        return std::nullopt;
    const char *pinyin = pinyin_override == nullptr ? key : pinyin_override;
    return WordItem(pinyin == nullptr ? "" : pinyin, value, sqlite3_column_int64(statement, 2), source,
                    key == nullptr ? "" : key);
}
} // namespace detail

inline bool sqlite_execute_statement(sqlite3 *database, const std::string &sql)
{
    if (database == nullptr)
        return false;

    auto statement = sqlite_prepare_statement(database, sql);
    if (!statement)
        return false;
    return sqlite3_step(statement.get()) == SQLITE_DONE;
}

inline bool sqlite_query_has_row(sqlite3 *database, const std::string &sql)
{
    if (database == nullptr)
        return false;

    auto statement = sqlite_prepare_statement(database, sql);
    if (!statement)
        return false;
    return sqlite3_step(statement.get()) == SQLITE_ROW;
}

inline std::optional<WordItem> sqlite_query_word_item(sqlite3 *database, const std::string &table,
                                                      const std::string &key, const std::string &value)
{
    if (database == nullptr || table.empty())
        return std::nullopt;

    const std::string sql = "SELECT weight FROM \"" + table + "\" WHERE key=?1 AND value=?2 LIMIT 1";
    auto statement = sqlite_prepare_statement(database, sql);
    if (!statement)
        return std::nullopt;
    if (sqlite3_bind_text(statement.get(), 1, key.c_str(), -1, SQLITE_TRANSIENT) != SQLITE_OK ||
        sqlite3_bind_text(statement.get(), 2, value.c_str(), -1, SQLITE_TRANSIENT) != SQLITE_OK ||
        sqlite3_step(statement.get()) != SQLITE_ROW)
        return std::nullopt;
    return WordItem(key, value, sqlite3_column_int64(statement.get(), 0), CandidateSource::Database, key);
}

inline std::optional<WordItem> sqlite_read_word_item(sqlite3_stmt *statement,
                                                     CandidateSource source = CandidateSource::Database,
                                                     bool include_canonical_pinyin = true)
{
    auto candidate = detail::sqlite_read_word_item_impl(statement, source, nullptr, true);
    if (candidate && !include_canonical_pinyin)
        candidate->canonical_pinyin.clear();
    return candidate;
}

inline std::optional<WordItem> sqlite_read_word_item_with_pinyin(sqlite3_stmt *statement, const std::string &pinyin,
                                                                 CandidateSource source = CandidateSource::Database)
{
    return detail::sqlite_read_word_item_impl(statement, source, pinyin.c_str(), false);
}

inline std::vector<WordItem> sqlite_query_word_items(sqlite3 *database, const std::string &sql)
{
    std::vector<WordItem> result;
    if (database == nullptr)
        return result;

    auto statement = sqlite_prepare_statement(database, sql);
    if (!statement)
        return result;
    while (sqlite3_step(statement.get()) == SQLITE_ROW)
    {
        const auto *key = reinterpret_cast<const char *>(sqlite3_column_text(statement.get(), 0));
        const auto *value = reinterpret_cast<const char *>(sqlite3_column_text(statement.get(), 2));
        result.emplace_back(std::string(key), std::string(value), sqlite3_column_int64(statement.get(), 3),
                            CandidateSource::Database, std::string(key));
    }
    return result;
}
} // namespace metasequoia
