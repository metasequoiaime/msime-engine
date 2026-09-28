#pragma once

#include "sqlite_statement.h"

#include <sqlite3.h>

#include <string>

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
} // namespace metasequoia
