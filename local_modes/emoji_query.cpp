#include "../contracts/assets/assets.h"
#include "../common/string_utils.h"
#include "../common/sqlite_statement.h"
#include "emoji_query.h"
#include "local_database.h"
#include "query_prefixes.h"

#include "../core/data_path.h"

#include <sqlite3.h>

#include <algorithm>
#include <memory>
#include <unordered_set>
#include <utility>
#include <vector>

namespace metasequoia::local_modes
{
namespace
{
using Statement = metasequoia::SqliteStatement;

} // namespace

LocalQueryResult query_emoji(const std::string &code, SchemeType scheme, int limit, const ShuangpinProfile &profile)
{
    return query_emoji(code, scheme, data_file_path(metasequoia::assets::other_dictionary), limit, profile);
}

LocalQueryResult query_emoji(const std::string &code, SchemeType scheme, const std::filesystem::path &database_path,
                             int limit, const ShuangpinProfile &profile)
{
    if (!CommonUtils::is_ascii_letters_or_apostrophe(code) || limit <= 0)
    {
        return {};
    }
    if (database_path.empty())
    {
        return database_query_failure("Emoji database is unavailable.");
    }

    const std::vector<std::string> prefixes = normalized_query_prefixes(code, scheme, profile);
    const std::string &lower = prefixes.front();

    const std::shared_ptr<sqlite3> database = open_local_database(database_path);
    if (!database)
    {
        return database_query_failure("Emoji database is unavailable.");
    }

    struct Entry
    {
        std::string text;
        int sort_order;
    };
    std::vector<Entry> entries;
    std::unordered_set<std::string> seen;
    constexpr const char *kSql = "SELECT emoji,sort_order FROM emoji_pinyin WHERE key>=?1 AND key<?2 "
                                 "ORDER BY sort_order LIMIT ?3";
    for (const std::string &prefix : prefixes)
    {
        sqlite3_stmt *raw_statement = nullptr;
        if (sqlite3_prepare_v2(database.get(), kSql, -1, &raw_statement, nullptr) != SQLITE_OK)
        {
            return database_query_failure("Emoji database could not be queried.");
        }
        Statement statement(raw_statement);
        const std::string upper_bound = prefix_upper_bound(prefix);
        if (sqlite3_bind_text(statement.get(), 1, prefix.c_str(), -1, SQLITE_TRANSIENT) != SQLITE_OK ||
            sqlite3_bind_text(statement.get(), 2, upper_bound.c_str(), -1, SQLITE_TRANSIENT) != SQLITE_OK ||
            sqlite3_bind_int(statement.get(), 3, limit) != SQLITE_OK)
        {
            return database_query_failure("Emoji database could not be queried.");
        }

        int step_result = SQLITE_ROW;
        while ((step_result = sqlite3_step(statement.get())) == SQLITE_ROW)
        {
            const auto *text = reinterpret_cast<const char *>(sqlite3_column_text(statement.get(), 0));
            if (text != nullptr && seen.insert(text).second)
            {
                entries.push_back({text, sqlite3_column_int(statement.get(), 1)});
            }
        }
        if (step_result != SQLITE_DONE)
        {
            return database_query_failure("Emoji database could not be queried.");
        }
    }

    std::stable_sort(entries.begin(), entries.end(),
                     [](const Entry &left, const Entry &right) { return left.sort_order < right.sort_order; });
    const std::size_t count = std::min(static_cast<std::size_t>(limit), entries.size());
    LocalQueryResult result;
    result.candidates.reserve(count);
    for (std::size_t index = 0; index < count; ++index)
    {
        result.candidates.emplace_back(lower, entries[index].text, static_cast<std::int64_t>(count - index),
                                       CandidateSource::Emoji);
    }
    return result;
}
} // namespace metasequoia::local_modes
