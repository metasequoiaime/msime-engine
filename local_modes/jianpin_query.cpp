#include "../contracts/assets/assets.h"
#include "jianpin_query.h"
#include "local_database.h"

#include "../core/data_path.h"
#include "../quanpin/quanpin_query.h"

#include <sqlite3.h>

#include <algorithm>
#include <memory>
#include <utility>
#include <vector>

namespace metasequoia::local_modes
{
namespace
{
struct StatementCloser
{
    void operator()(sqlite3_stmt *statement) const
    {
        if (statement != nullptr)
        {
            sqlite3_finalize(statement);
        }
    }
};

using Statement = std::unique_ptr<sqlite3_stmt, StatementCloser>;

std::string normalize_code(const std::string &code)
{
    std::string normalized;
    normalized.reserve(code.size());
    for (unsigned char character : code)
    {
        if (character >= 'A' && character <= 'Z')
        {
            character = static_cast<unsigned char>(character - 'A' + 'a');
        }
        if (character < 'a' || character > 'z')
        {
            return {};
        }
        normalized.push_back(static_cast<char>(character));
    }
    return normalized;
}

std::string decode_shuangpin_initial(char code, const ShuangpinProfile &profile)
{
    const std::string mapped_key(1, code);
    for (const auto &[initial, key] : profile.initials)
    {
        if (key == mapped_key)
        {
            return initial;
        }
    }
    return mapped_key;
}

quanpin::Segments expand_code(const std::string &normalized, SchemeType scheme, const ShuangpinProfile &profile)
{
    quanpin::Segments segments;
    segments.reserve(normalized.size());
    for (const char character : normalized)
    {
        segments.push_back(scheme == SchemeType::Shuangpin ? decode_shuangpin_initial(character, profile)
                                                           : std::string(1, character));
    }
    return segments;
}

std::vector<std::string> split_key(const std::string &key)
{
    std::vector<std::string> parts;
    std::size_t start = 0;
    while (true)
    {
        const std::size_t separator = key.find('\'', start);
        if (separator == std::string::npos)
        {
            parts.push_back(key.substr(start));
            return parts;
        }
        parts.push_back(key.substr(start, separator - start));
        start = separator + 1;
    }
}

std::string syllable_initial(const std::string &syllable)
{
    if (syllable.size() >= 2)
    {
        const std::string prefix = syllable.substr(0, 2);
        if (prefix == "zh" || prefix == "ch" || prefix == "sh")
        {
            return prefix;
        }
    }
    return syllable.empty() ? std::string{} : syllable.substr(0, 1);
}

bool key_matches_initials(const std::string &key, const quanpin::Segments &initials)
{
    const auto syllables = split_key(key);
    if (syllables.size() != initials.size())
    {
        return false;
    }
    for (std::size_t index = 0; index < initials.size(); ++index)
    {
        if (syllable_initial(syllables[index]) != initials[index])
        {
            return false;
        }
    }
    return true;
}

int scan_limit(int limit, bool filter_initials)
{
    if (!filter_initials || limit > 100000000)
    {
        return limit;
    }
    return std::max(limit * 32, 512);
}

LocalQueryResult query_failure(const char *diagnostic)
{
    return {{}, std::string(diagnostic)};
}
} // namespace

std::string jianpin_ranking_context(const std::string &code, SchemeType scheme, const ShuangpinProfile &profile)
{
    const std::string normalized = normalize_code(code);
    return normalized.empty() ? std::string{} : quanpin::join_segments(expand_code(normalized, scheme, profile));
}

LocalQueryResult query_jianpin(const std::string &code, SchemeType scheme, int limit, const ShuangpinProfile &profile)
{
    return query_jianpin(code, scheme, data_file_path(metasequoia::assets::main_dictionary), limit, profile);
}

LocalQueryResult query_jianpin(const std::string &code, SchemeType scheme, const std::filesystem::path &database_path,
                               int limit, const ShuangpinProfile &profile)
{
    if (limit <= 0)
    {
        return {};
    }
    const std::string normalized = normalize_code(code);
    if (normalized.empty())
    {
        return {};
    }

    const auto segments = expand_code(normalized, scheme, profile);
    const std::string table = quanpin::build_table_name(segments);
    if (table.empty())
    {
        return {};
    }

    const std::shared_ptr<sqlite3> database = open_local_database(database_path);
    if (!database)
    {
        return query_failure("Super-jianpin database is unavailable.");
    }

    const std::string jianpin = quanpin::segments_to_jianpin(segments);
    const bool filter_initials = scheme == SchemeType::Shuangpin;
    const std::string sql =
        "SELECT \"key\",\"value\",\"weight\" FROM \"" + table + "\" WHERE \"jp\"=?1 ORDER BY \"weight\" DESC LIMIT ?2";
    sqlite3_stmt *raw_statement = nullptr;
    if (sqlite3_prepare_v2(database.get(), sql.c_str(), -1, &raw_statement, nullptr) != SQLITE_OK)
    {
        return query_failure("Super-jianpin database could not be queried.");
    }
    Statement statement(raw_statement);
    if (sqlite3_bind_text(statement.get(), 1, jianpin.c_str(), -1, SQLITE_TRANSIENT) != SQLITE_OK ||
        sqlite3_bind_int(statement.get(), 2, scan_limit(limit, filter_initials)) != SQLITE_OK)
    {
        return query_failure("Super-jianpin database could not be queried.");
    }

    const std::string matched_code = quanpin::join_segments(segments);
    LocalQueryResult result;
    int step_result = SQLITE_ROW;
    while ((step_result = sqlite3_step(statement.get())) == SQLITE_ROW)
    {
        const auto *key = reinterpret_cast<const char *>(sqlite3_column_text(statement.get(), 0));
        const auto *value = reinterpret_cast<const char *>(sqlite3_column_text(statement.get(), 1));
        if (value == nullptr)
        {
            continue;
        }
        const std::string canonical = key == nullptr ? std::string{} : std::string(key);
        if (filter_initials && !key_matches_initials(canonical, segments))
        {
            continue;
        }
        result.candidates.emplace_back(matched_code, value, sqlite3_column_int64(statement.get(), 2),
                                       CandidateSource::Database, canonical);
        if (static_cast<int>(result.candidates.size()) >= limit)
        {
            break;
        }
    }
    if (step_result != SQLITE_DONE && step_result != SQLITE_ROW)
    {
        return query_failure("Super-jianpin database could not be queried.");
    }
    return result;
}
} // namespace metasequoia::local_modes
