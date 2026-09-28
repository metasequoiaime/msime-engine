#pragma once

#include <sqlite3.h>

#include <memory>

namespace metasequoia
{
// A database connection owns the prepared statements created from it. The connection deleter is
// kept in the common layer so each module follows the same null-safe sqlite3_close rule.
struct SqliteDatabaseCloser
{
    void operator()(sqlite3 *database) const
    {
        if (database != nullptr)
            sqlite3_close(database);
    }
};

using SqliteDatabase = std::unique_ptr<sqlite3, SqliteDatabaseCloser>;
} // namespace metasequoia
