#pragma once

#include "sqlite_statement.h"

#include <sqlite3.h>

namespace metasequoia
{
// SQLite changes data_version on this connection only when another connection
// commits. The first successful read establishes the baseline without
// invalidating a cache; a failed read leaves that baseline untouched.
inline bool sqlite_data_version_changed(sqlite3 *database, sqlite3_int64 &last_version)
{
    if (database == nullptr)
        return false;

    auto statement = sqlite_prepare_statement(database, "PRAGMA data_version");
    if (!statement)
        return false;
    if (sqlite3_step(statement.get()) != SQLITE_ROW)
        return false;

    const sqlite3_int64 current_version = sqlite3_column_int64(statement.get(), 0);
    const bool changed = last_version >= 0 && current_version != last_version;
    last_version = current_version;
    return changed;
}

template <typename Reset>
inline void reset_if_sqlite_data_version_changed(sqlite3 *database, sqlite3_int64 &last_version, Reset reset)
{
    if (sqlite_data_version_changed(database, last_version))
        reset();
}
} // namespace metasequoia
