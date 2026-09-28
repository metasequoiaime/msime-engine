#include "../contracts/assets/assets.h"
#include "../common/string_utils.h"
#include "kaomoji_query.h"
#include "local_database.h"
#include "query_prefixes.h"

#include "../core/data_path.h"

#include <sqlite3.h>

#include <memory>
#include <utility>
#include <vector>

namespace metasequoia::local_modes
{
LocalQueryResult query_kaomoji(const std::string &code, SchemeType scheme, int limit, const ShuangpinProfile &profile)
{
    return query_kaomoji(code, scheme, data_file_path(metasequoia::assets::other_dictionary), limit, profile);
}

LocalQueryResult query_kaomoji(const std::string &code, SchemeType scheme, const std::filesystem::path &database_path,
                               int limit, const ShuangpinProfile &profile)
{
    if (!CommonUtils::is_ascii_letters_or_apostrophe(code) || limit <= 0)
    {
        return {};
    }
    if (database_path.empty())
    {
        return database_query_failure("Kaomoji database is unavailable.");
    }

    const std::vector<std::string> prefixes = normalized_query_prefixes(code, scheme, profile);
    const std::string &lower = prefixes.front();

    const std::shared_ptr<sqlite3> database = open_local_database(database_path);
    if (!database)
    {
        return database_query_failure("Kaomoji database is unavailable.");
    }

    std::vector<LocalQueryEntry> entries;
    constexpr const char *kSql = "SELECT kaomoji,sort_order FROM kaomoji "
                                 "WHERE (pinyin>=?1 AND pinyin<?2) OR (jianpin>=?1 AND jianpin<?2) "
                                 "ORDER BY sort_order LIMIT ?3";
    if (!query_prefix_rows(database.get(), prefixes, kSql, limit, [&](sqlite3_stmt *statement) {
            const auto *text = reinterpret_cast<const char *>(sqlite3_column_text(statement, 0));
            if (text != nullptr)
            {
                entries.push_back({text, sqlite3_column_int(statement, 1)});
            }
        }))
    {
        return database_query_failure("Kaomoji database could not be queried.");
    }

    return build_local_query_result(lower, std::move(entries), limit, CandidateSource::Kaomoji);
}
} // namespace metasequoia::local_modes
