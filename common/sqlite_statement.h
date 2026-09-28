#pragma once

#include <sqlite3.h>

#include <memory>
#include <string>
#include <unordered_map>

namespace metasequoia
{
// A prepared statement owns no database connection, but it must always be finalized before the
// connection shared by a query or dictionary operation is released. Keeping this deleter in the
// common layer avoids each module carrying its own copy of the same lifetime rule.
struct SqliteStatementCloser
{
    void operator()(sqlite3_stmt *statement) const
    {
        if (statement != nullptr)
            sqlite3_finalize(statement);
    }
};

using SqliteStatement = std::unique_ptr<sqlite3_stmt, SqliteStatementCloser>;
using SqliteStatementCache = std::unordered_map<std::string, SqliteStatement>;

inline SqliteStatement sqlite_prepare_statement(sqlite3 *database, const char *sql)
{
    if (database == nullptr || sql == nullptr)
        return {};

    sqlite3_stmt *raw = nullptr;
    if (sqlite3_prepare_v2(database, sql, -1, &raw, nullptr) != SQLITE_OK)
        return {};
    return SqliteStatement(raw);
}

inline SqliteStatement sqlite_prepare_statement(sqlite3 *database, const std::string &sql)
{
    return sqlite_prepare_statement(database, sql.c_str());
}
} // namespace metasequoia
