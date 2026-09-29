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
    const int status = sqlite3_open_v2(path.c_str(), &raw, flags, nullptr);
    // sqlite3_open_v2 usually allocates a handle even when it fails, and that handle still has to be closed.
    SqliteDatabase database(raw);
    if (status != SQLITE_OK)
        return {};
    return database;
}

inline std::shared_ptr<sqlite3> sqlite_open_shared_database(const std::string &path, int flags, int busy_timeout_ms)
{
    auto database = sqlite_open_database(path, flags);
    if (!database)
        return {};
    (void)sqlite3_busy_timeout(database.get(), busy_timeout_ms);
    return std::shared_ptr<sqlite3>(database.release(), SqliteDatabaseCloser{});
}
} // namespace metasequoia
