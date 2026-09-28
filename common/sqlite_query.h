#pragma once

#include "sqlite_statement.h"
#include "../core/word_item.h"

#include <sqlite3.h>

#include <optional>
#include <string>
#include <vector>

namespace metasequoia
{
inline bool sqlite_query_has_row(sqlite3 *database, const std::string &sql)
{
    if (database == nullptr)
        return false;

    sqlite3_stmt *raw = nullptr;
    if (sqlite3_prepare_v2(database, sql.c_str(), -1, &raw, nullptr) != SQLITE_OK)
        return false;
    SqliteStatement statement(raw);
    return sqlite3_step(statement.get()) == SQLITE_ROW;
}

inline std::optional<WordItem> sqlite_query_word_item(sqlite3 *database, const std::string &table,
                                                      const std::string &key, const std::string &value)
{
    if (database == nullptr || table.empty())
        return std::nullopt;

    sqlite3_stmt *raw = nullptr;
    const std::string sql = "SELECT weight FROM \"" + table + "\" WHERE key=?1 AND value=?2 LIMIT 1";
    if (sqlite3_prepare_v2(database, sql.c_str(), -1, &raw, nullptr) != SQLITE_OK)
        return std::nullopt;
    SqliteStatement statement(raw);
    if (sqlite3_bind_text(statement.get(), 1, key.c_str(), -1, SQLITE_TRANSIENT) != SQLITE_OK ||
        sqlite3_bind_text(statement.get(), 2, value.c_str(), -1, SQLITE_TRANSIENT) != SQLITE_OK ||
        sqlite3_step(statement.get()) != SQLITE_ROW)
        return std::nullopt;
    return WordItem(key, value, sqlite3_column_int64(statement.get(), 0), CandidateSource::Database, key);
}

inline std::vector<WordItem> sqlite_query_word_items(sqlite3 *database, const std::string &sql)
{
    std::vector<WordItem> result;
    if (database == nullptr)
        return result;

    sqlite3_stmt *raw = nullptr;
    if (sqlite3_prepare_v2(database, sql.c_str(), -1, &raw, nullptr) != SQLITE_OK)
        return result;
    SqliteStatement statement(raw);
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
