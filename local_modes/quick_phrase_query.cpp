#include "../contracts/assets/assets.h"
#include "../common/string_utils.h"
#include "quick_phrase_query.h"
#include "local_database.h"

#include "../core/data_path.h"

#include <sqlite3.h>

#include <memory>

namespace metasequoia::local_modes
{
QuickPhraseQueryResult query_quick_phrases(const std::string &prefix, int limit)
{
    return query_quick_phrases(prefix, data_file_path(metasequoia::assets::main_dictionary), limit);
}

QuickPhraseQueryResult query_quick_phrases(const std::string &prefix, const std::filesystem::path &database_path,
                                           int limit)
{
    if (!CommonUtils::is_ascii_lowercase_string(prefix) || limit <= 0)
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
    QuickPhraseQueryResult result;
    if (!query_prefix_rows(database.get(), {prefix}, kSql, limit, [&](sqlite3_stmt *statement) {
            const auto *key = reinterpret_cast<const char *>(sqlite3_column_text(statement, 0));
            const auto *value = reinterpret_cast<const char *>(sqlite3_column_text(statement, 1));
            if (key != nullptr && value != nullptr)
            {
                result.candidates.emplace_back(key, value, sqlite3_column_int64(statement, 2),
                                               CandidateSource::QuickPhrase);
            }
        }))
    {
        return database_query_failure("Quick phrase database could not be queried.");
    }
    return result;
}
} // namespace metasequoia::local_modes
