#include "../contracts/assets/assets.h"
#include "kaomoji_query.h"
#include "local_database.h"

#include "../core/data_path.h"

#include <sqlite3.h>

namespace metasequoia::local_modes
{
LocalQueryResult query_kaomoji(const std::string &code, SchemeType scheme, int limit, const ShuangpinProfile &profile)
{
    return query_kaomoji(code, scheme, data_file_path(metasequoia::assets::other_dictionary), limit, profile);
}

LocalQueryResult query_kaomoji(const std::string &code, SchemeType scheme, const std::filesystem::path &database_path,
                               int limit, const ShuangpinProfile &profile)
{
    constexpr const char *kSql = "SELECT kaomoji,sort_order FROM kaomoji "
                                 "WHERE (pinyin>=?1 AND pinyin<?2) OR (jianpin>=?1 AND jianpin<?2) "
                                 "ORDER BY sort_order LIMIT ?3";
    return query_prefix_dictionary(code, scheme, database_path, limit, profile, kSql, CandidateSource::Kaomoji,
                                   "Kaomoji database is unavailable.", "Kaomoji database could not be queried.",
                                   read_text_sort_entry);
}
} // namespace metasequoia::local_modes
