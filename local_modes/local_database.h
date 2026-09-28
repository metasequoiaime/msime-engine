#pragma once

#include "local_query_result.h"

#include <sqlite3.h>

#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace metasequoia::local_modes
{
struct LocalQueryEntry
{
    std::string text;
    int sort_order = 0;
};

// Builds the ranked result shared by local modes whose dictionaries return a
// display string and catalog sort order.
LocalQueryResult build_local_query_result(const std::string &pinyin, std::vector<LocalQueryEntry> entries, int limit,
                                          CandidateSource source);

// Opens a read-only connection for a local-mode query (jianpin, quick phrases,
// emoji, kaomoji). These queries run on every keystroke of their mode, and a
// fresh connection re-parses the dictionary schema on its first prepare. For
// the shipped dictionaries of the active data directory the connection is
// therefore opened once and shared.
//
// Any other path (tests, tools) gets its own connection that closes with the
// last reference, so callers never retain a file handle unexpectedly.
// Returns null when the database cannot be opened.
std::shared_ptr<sqlite3> open_local_database(const std::filesystem::path &path);

// Exclusive upper bound for a bytewise prefix range over normalized local-mode keys.
std::string prefix_upper_bound(const std::string &prefix);

// Runs a normalized prefix-range query and invokes on_row for each returned row.
// The callback owns row extraction; false means prepare, bind, or step failed.
bool query_prefix_rows(sqlite3 *database, const std::vector<std::string> &prefixes, const char *sql, int limit,
                       const std::function<void(sqlite3_stmt *)> &on_row);

// Drops the shared connections so dictionary files can be deleted or replaced.
// Each connection closes once the last query using it returns.
void close_cached_local_databases();
} // namespace metasequoia::local_modes
