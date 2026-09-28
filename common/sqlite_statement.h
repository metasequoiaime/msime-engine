#pragma once

#include <sqlite3.h>

#include <memory>

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
} // namespace metasequoia
