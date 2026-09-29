#include "../contracts/assets/assets.h"
#include "local_database.h"

#include "../common/sqlite_database.h"
#include "../common/sqlite_statement.h"
#include "../core/data_path.h"
#include "../common/string_utils.h"
#include "query_prefixes.h"

#include <algorithm>
#include <mutex>
#include <unordered_set>
#include <utility>
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

std::optional<LocalQueryEntry> read_text_sort_entry(sqlite3_stmt *statement)
{
    const auto *text = reinterpret_cast<const char *>(sqlite3_column_text(statement, 0));
    return text == nullptr ? std::nullopt : std::optional<LocalQueryEntry>{{text, sqlite3_column_int(statement, 1)}};
}

LocalQueryResult query_prefix_dictionary(const std::string &code, SchemeType scheme,
                                         const std::filesystem::path &database_path, int limit,
                                         const ShuangpinProfile &profile, const char *sql, CandidateSource source,
                                         const char *unavailable_diagnostic, const char *query_diagnostic,
                                         const LocalQueryEntryReader &read_entry)
{
    if (!CommonUtils::is_ascii_letters_or_apostrophe(code) || limit <= 0)
    {
        return {};
    }
    if (database_path.empty())
    {
        return database_query_failure(unavailable_diagnostic);
    }

    const std::vector<std::string> prefixes = normalized_query_prefixes(code, scheme, profile);
    const std::string &lower = prefixes.front();
    const std::shared_ptr<sqlite3> database = open_local_database(database_path);
    if (!database)
    {
        return database_query_failure(unavailable_diagnostic);
    }

    std::vector<LocalQueryEntry> entries;
    if (!query_prefix_rows(database.get(), prefixes, sql, limit, [&](sqlite3_stmt *statement) {
            if (const auto entry = read_entry(statement))
            {
                entries.push_back(std::move(*entry));
            }
        }))
    {
        return database_query_failure(query_diagnostic);
    }
    return build_local_query_result(lower, std::move(entries), limit, source);
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
        const std::string upper_bound = CommonUtils::ascii_prefix_upper_bound(prefix);
        if (!metasequoia::sqlite_bind_text_range_limit(statement.get(), prefix, upper_bound, limit))
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
