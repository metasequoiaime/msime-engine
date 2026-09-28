#include "../contracts/assets/assets.h"
#include "../common/string_utils.h"
#include "../common/sqlite_statement.h"
#include "quick_phrase_query.h"
#include "local_database.h"

#include "../core/data_path.h"

#include <sqlite3.h>

#include <algorithm>
#include <memory>

namespace metasequoia::local_modes
{
namespace
{
using Statement = metasequoia::SqliteStatement;

bool valid_prefix(const std::string &prefix)
{
    return !prefix.empty() && std::all_of(prefix.begin(), prefix.end(), [](unsigned char character) {
        return CommonUtils::is_ascii_lowercase(character);
    });
}

} // namespace

QuickPhraseQueryResult query_quick_phrases(const std::string &prefix, int limit)
{
    return query_quick_phrases(prefix, data_file_path(metasequoia::assets::main_dictionary), limit);
}

QuickPhraseQueryResult query_quick_phrases(const std::string &prefix, const std::filesystem::path &database_path,
                                           int limit)
{
    if (!valid_prefix(prefix) || limit <= 0)
    {
        return {};
    }
    if (database_path.empty())
    {
        return database_query_failure("Quick phrase database is unavailable.");
    }

    const std::shared_ptr<sqlite3> database = open_local_database(database_path);
    if (!database)
    {
        return database_query_failure("Quick phrase database is unavailable.");
    }

    constexpr const char *kSql = "SELECT key,value,weight FROM quick_parases WHERE key>=?1 AND key<?2 "
                                 "ORDER BY weight DESC,key,value LIMIT ?3";
    sqlite3_stmt *raw_statement = nullptr;
    if (sqlite3_prepare_v2(database.get(), kSql, -1, &raw_statement, nullptr) != SQLITE_OK)
    {
        return database_query_failure("Quick phrase database could not be queried.");
    }
    Statement statement(raw_statement);
    const std::string upper_bound = prefix_upper_bound(prefix);
    if (sqlite3_bind_text(statement.get(), 1, prefix.c_str(), -1, SQLITE_TRANSIENT) != SQLITE_OK ||
        sqlite3_bind_text(statement.get(), 2, upper_bound.c_str(), -1, SQLITE_TRANSIENT) != SQLITE_OK ||
        sqlite3_bind_int(statement.get(), 3, limit) != SQLITE_OK)
    {
        return database_query_failure("Quick phrase database could not be queried.");
    }

    QuickPhraseQueryResult result;
    int step_result = SQLITE_ROW;
    while ((step_result = sqlite3_step(statement.get())) == SQLITE_ROW)
    {
        const auto *key = reinterpret_cast<const char *>(sqlite3_column_text(statement.get(), 0));
        const auto *value = reinterpret_cast<const char *>(sqlite3_column_text(statement.get(), 1));
        if (key != nullptr && value != nullptr)
        {
            result.candidates.emplace_back(key, value, sqlite3_column_int64(statement.get(), 2),
                                           CandidateSource::QuickPhrase);
        }
    }
    if (step_result != SQLITE_DONE)
    {
        return database_query_failure("Quick phrase database could not be queried.");
    }
    return result;
}
} // namespace metasequoia::local_modes
