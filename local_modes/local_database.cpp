#include "../contracts/assets/assets.h"
#include "local_database.h"

#include "../common/sqlite_database.h"
#include "../common/sqlite_statement.h"
#include "../core/data_path.h"

#include <algorithm>
#include <mutex>
#include <unordered_set>
#include <vector>

namespace metasequoia::local_modes
{
namespace
{
std::shared_ptr<sqlite3> open_read_only(const std::filesystem::path &path)
{
    sqlite3 *raw = nullptr;
    // FULLMUTEX: a shared connection is used by the input worker and local-mode
    // query workers.
    const int status =
        sqlite3_open_v2(path_to_utf8(path).c_str(), &raw, SQLITE_OPEN_READONLY | SQLITE_OPEN_FULLMUTEX, nullptr);
    metasequoia::SqliteDatabase database(raw);
    if (status != SQLITE_OK)
        return {};
    sqlite3_busy_timeout(database.get(), 1000);
    return std::shared_ptr<sqlite3>(database.release(), metasequoia::SqliteDatabaseCloser{});
}

// Only the shipped dictionaries are shared. Other paths belong to tests or
// tools that may remove their temporary directory as soon as a query returns.
bool is_shared_dictionary(const std::filesystem::path &path)
{
    return path == data_file_path(metasequoia::assets::main_dictionary) ||
           path == data_file_path(metasequoia::assets::other_dictionary);
}

struct CachedConnection
{
    std::filesystem::path path;
    std::shared_ptr<sqlite3> connection;
};

std::mutex &cache_mutex()
{
    static std::mutex mutex;
    return mutex;
}

std::vector<CachedConnection> &cache()
{
    static std::vector<CachedConnection> connections;
    return connections;
}
} // namespace

LocalQueryResult build_local_query_result(const std::string &pinyin, std::vector<LocalQueryEntry> entries, int limit,
                                          CandidateSource source)
{
    if (limit <= 0)
    {
        return {};
    }

    std::unordered_set<std::string> seen;
    entries.erase(std::remove_if(entries.begin(), entries.end(),
                                 [&](const LocalQueryEntry &entry) { return !seen.insert(entry.text).second; }),
                  entries.end());
    std::stable_sort(entries.begin(), entries.end(), [](const LocalQueryEntry &left, const LocalQueryEntry &right) {
        return left.sort_order < right.sort_order;
    });

    const std::size_t count = std::min(static_cast<std::size_t>(limit), entries.size());
    LocalQueryResult result;
    result.candidates.reserve(count);
    for (std::size_t index = 0; index < count; ++index)
    {
        result.candidates.emplace_back(pinyin, entries[index].text, static_cast<std::int64_t>(count - index), source);
    }
    return result;
}

std::shared_ptr<sqlite3> open_local_database(const std::filesystem::path &path)
{
    if (path.empty())
    {
        return {};
    }
    if (!is_shared_dictionary(path))
    {
        return open_read_only(path);
    }

    const std::lock_guard<std::mutex> guard(cache_mutex());
    auto &connections = cache();
    for (const CachedConnection &cached : connections)
    {
        if (cached.path == path)
        {
            return cached.connection;
        }
    }
    auto connection = open_read_only(path);
    if (connection)
    {
        // Keep at most one connection per shipped dictionary name. A data
        // directory switch must release the previous generation here.
        connections.erase(
            std::remove_if(connections.begin(), connections.end(),
                           [&](const CachedConnection &cached) { return cached.path.filename() == path.filename(); }),
            connections.end());
        connections.push_back({path, connection});
    }
    return connection;
}

std::string prefix_upper_bound(const std::string &prefix)
{
    std::string result = prefix;
    result.push_back(static_cast<char>(0x7f));
    return result;
}

bool query_prefix_rows(sqlite3 *database, const std::vector<std::string> &prefixes, const char *sql, int limit,
                       const std::function<void(sqlite3_stmt *)> &on_row)
{
    if (database == nullptr || prefixes.empty() || sql == nullptr || limit <= 0 || !on_row)
    {
        return false;
    }
    for (const std::string &prefix : prefixes)
    {
        auto statement = metasequoia::sqlite_prepare_statement(database, sql);
        if (!statement)
        {
            return false;
        }
        const std::string upper_bound = prefix_upper_bound(prefix);
        if (sqlite3_bind_text(statement.get(), 1, prefix.c_str(), -1, SQLITE_TRANSIENT) != SQLITE_OK ||
            sqlite3_bind_text(statement.get(), 2, upper_bound.c_str(), -1, SQLITE_TRANSIENT) != SQLITE_OK ||
            sqlite3_bind_int(statement.get(), 3, limit) != SQLITE_OK)
        {
            return false;
        }
        int step_result = SQLITE_ROW;
        while ((step_result = sqlite3_step(statement.get())) == SQLITE_ROW)
        {
            on_row(statement.get());
        }
        if (step_result != SQLITE_DONE)
        {
            return false;
        }
    }
    return true;
}

void close_cached_local_databases()
{
    const std::lock_guard<std::mutex> guard(cache_mutex());
    cache().clear();
}
} // namespace metasequoia::local_modes
