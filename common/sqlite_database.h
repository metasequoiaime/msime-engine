#pragma once

#include <sqlite3.h>

#include <memory>
#include <string>

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

inline SqliteDatabase sqlite_open_database(const std::string &path, int flags)
{
    sqlite3 *raw = nullptr;
    if (sqlite3_open_v2(path.c_str(), &raw, flags, nullptr) != SQLITE_OK)
        return {};
    return SqliteDatabase(raw);
}
} // namespace metasequoia
