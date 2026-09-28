#pragma once

#include <sqlite3.h>

#include <filesystem>
#include <memory>

namespace metasequoia::local_modes
{
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

// Drops the shared connections so dictionary files can be deleted or replaced.
// Each connection closes once the last query using it returns.
void close_cached_local_databases();
} // namespace metasequoia::local_modes
