#include "../contracts/assets/assets.h"
#include "../contracts/dictionary/format.h"
#include "quanpin_query.h"

#include "../common/helpcode_utils.h"
#include "../common/string_utils.h"
#include "../common/sqlite_database.h"
#include "../common/sqlite_statement.h"
#include "quanpin_utils.h"
#include "../shuangpin/shuangpin_utils.h"
#include <algorithm>
#include <climits>
#include <map>
#include <sqlite3.h>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace quanpin
{
namespace
{

using Statement = metasequoia::SqliteStatement;

constexpr size_t kCorrectionPathLimit = 32;

sqlite3_stmt *prepare_cached_statement(sqlite3 *db, metasequoia::SqliteStatementCache &statement_cache,
                                       const std::string &sql)
{
    const auto found = statement_cache.find(sql);
    if (found != statement_cache.end())
    {
        sqlite3_stmt *statement = found->second.get();
        sqlite3_reset(statement);
        sqlite3_clear_bindings(statement);
        return statement;
    }

    sqlite3_stmt *statement = nullptr;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &statement, nullptr) != SQLITE_OK)
    {
        return nullptr;
    }
    statement_cache.emplace(sql, metasequoia::SqliteStatement(statement));
    return statement;
}

using CorrectionAliases = std::unordered_map<std::string, std::vector<std::string>>;

const CorrectionAliases &pinyin_correction_aliases()
{
    static const CorrectionAliases kAliases = [] {
        CorrectionAliases aliases;
        const auto add_alias = [&](const std::string &typed, const std::string &canonical) {
            auto &canonical_syllables = aliases[typed];
            if (std::find(canonical_syllables.begin(), canonical_syllables.end(), canonical) ==
                canonical_syllables.end())
            {
                canonical_syllables.push_back(canonical);
            }
        };
        const auto add_suffix_aliases = [&](const std::string &canonical_suffix, const std::string &typed_suffix) {
            for (const auto &syllable : intact_pinyin_list())
            {
                if (syllable.size() < canonical_suffix.size() ||
                    syllable.compare(syllable.size() - canonical_suffix.size(), canonical_suffix.size(),
                                     canonical_suffix) != 0)
                {
                    continue;
                }

                const std::string prefix = syllable.substr(0, syllable.size() - canonical_suffix.size());
                add_alias(prefix + typed_suffix, syllable);
            }
        };

        add_suffix_aliases("iang", "aing");
        add_suffix_aliases("uang", "aung");
        add_suffix_aliases("ian", "ain");
        add_suffix_aliases("uan", "aun");
        add_suffix_aliases("iao", "aio");
        add_suffix_aliases("ing", "ihng");
        add_suffix_aliases("ang", "agn");
        add_suffix_aliases("eng", "egn");
        add_alias("egn", "eng");
        add_alias("jv", "ju");
        // Missing final g before the next syllable: zhonguo -> zhong guo.
        add_suffix_aliases("ong", "on");

        // Prefer a transposed h (ahng -> hang), while retaining the extra-h
        // interpretation as an alternative (ahng -> ang).
        add_suffix_aliases("hang", "ahng");
        add_suffix_aliases("ang", "ahng");

        // Prefer a transposed h (cehng -> cheng), while retaining the extra-h
        // interpretation as an alternative (cehng -> ceng).
        add_suffix_aliases("heng", "ehng");
        add_suffix_aliases("eng", "ehng");
        return aliases;
    }();
    return kAliases;
}

std::vector<Segments> cut_one_piece_with_corrections(const std::string &pinyin)
{
    struct RankedPath
    {
        Segments segments;
        std::vector<size_t> typed_lengths;
        std::vector<size_t> correction_ranks;
    };

    const auto &valid_pinyin = intact_pinyin_set();
    const auto &aliases = pinyin_correction_aliases();
    std::unordered_map<size_t, std::vector<RankedPath>> memo;

    const auto solve = [&](auto &&self, size_t index) -> std::vector<RankedPath> {
        if (index == pinyin.size())
        {
            return {RankedPath{}};
        }
        if (const auto found = memo.find(index); found != memo.end())
        {
            return found->second;
        }

        std::vector<RankedPath> paths;
        for (size_t end = pinyin.size(); end > index; --end)
        {
            const std::string typed = pinyin.substr(index, end - index);
            std::vector<std::string> canonical_syllables;
            if (const auto alias = aliases.find(typed); alias != aliases.end())
            {
                canonical_syllables = alias->second;
            }
            else if (valid_pinyin.find(typed) != valid_pinyin.end())
            {
                canonical_syllables.push_back(typed);
            }
            else
            {
                continue;
            }

            const auto suffix_paths = self(self, end);
            for (size_t canonical_index = 0; canonical_index < canonical_syllables.size(); ++canonical_index)
            {
                for (const auto &suffix : suffix_paths)
                {
                    RankedPath candidate;
                    candidate.segments.reserve(1 + suffix.segments.size());
                    candidate.segments.push_back(canonical_syllables[canonical_index]);
                    candidate.segments.insert(candidate.segments.end(), suffix.segments.begin(), suffix.segments.end());
                    candidate.typed_lengths.reserve(1 + suffix.typed_lengths.size());
                    candidate.typed_lengths.push_back(end - index);
                    candidate.typed_lengths.insert(candidate.typed_lengths.end(), suffix.typed_lengths.begin(),
                                                   suffix.typed_lengths.end());
                    candidate.correction_ranks.reserve(1 + suffix.correction_ranks.size());
                    candidate.correction_ranks.push_back(canonical_index);
                    candidate.correction_ranks.insert(candidate.correction_ranks.end(), suffix.correction_ranks.begin(),
                                                      suffix.correction_ranks.end());
                    paths.push_back(std::move(candidate));
                }
            }
        }

        std::stable_sort(paths.begin(), paths.end(), [](const RankedPath &lhs, const RankedPath &rhs) {
            if (lhs.segments.size() != rhs.segments.size())
            {
                return lhs.segments.size() < rhs.segments.size();
            }
            if (lhs.typed_lengths != rhs.typed_lengths)
            {
                return std::lexicographical_compare(lhs.typed_lengths.begin(), lhs.typed_lengths.end(),
                                                    rhs.typed_lengths.begin(), rhs.typed_lengths.end());
            }
            return std::lexicographical_compare(lhs.correction_ranks.begin(), lhs.correction_ranks.end(),
                                                rhs.correction_ranks.begin(), rhs.correction_ranks.end());
        });
        paths.erase(
            std::unique(paths.begin(), paths.end(),
                        [](const RankedPath &lhs, const RankedPath &rhs) { return lhs.segments == rhs.segments; }),
            paths.end());
        if (!paths.empty())
        {
            const size_t minimum_segments = paths.front().segments.size();
            paths.erase(std::find_if(paths.begin(), paths.end(),
                                     [&](const RankedPath &path) { return path.segments.size() != minimum_segments; }),
                        paths.end());
        }
        if (paths.size() > kCorrectionPathLimit)
        {
            paths.resize(kCorrectionPathLimit);
        }
        memo.emplace(index, paths);
        return paths;
    };

    const auto ranked_paths = solve(solve, 0);
    std::vector<Segments> paths;
    paths.reserve(ranked_paths.size());
    for (const auto &path : ranked_paths)
    {
        paths.push_back(path.segments);
    }
    return paths;
}

std::vector<Segments> cut_pinyin_with_corrections(const std::string &pinyin)
{
    std::vector<Segments> merged_paths = {Segments{}};
    for (const auto &part : CommonUtils::split_by_delimiter(pinyin, '\''))
    {
        const auto part_paths = cut_one_piece_with_corrections(part);
        if (part_paths.empty())
        {
            return {};
        }

        std::vector<Segments> combined;
        for (const auto &merged : merged_paths)
        {
            for (const auto &part_path : part_paths)
            {
                Segments candidate = merged;
                candidate.insert(candidate.end(), part_path.begin(), part_path.end());
                combined.push_back(std::move(candidate));
                if (combined.size() == kCorrectionPathLimit)
                {
                    break;
                }
            }
            if (combined.size() == kCorrectionPathLimit)
            {
                break;
            }
        }
        merged_paths = std::move(combined);
    }
    return merged_paths;
}

std::string build_table_name_impl(const Segments &segments)
{
    if (segments.empty() || segments.front().empty())
    {
        return "";
    }
    return metasequoia::dictionary_format::quanpin_table(segments.size(), segments.front().front());
}

std::string segments_to_jianpin_impl(const Segments &segments)
{
    std::string jp;
    for (const auto &segment : segments)
    {
        if (!segment.empty())
        {
            jp.push_back(segment.front());
        }
    }
    return jp;
}

std::string build_key_like_pattern(const Segments &segments)
{
    Segments parts;
    for (size_t i = 0; i < segments.size(); ++i)
    {
        const bool is_last = (i + 1 == segments.size());
        if (is_last || segments[i].size() == 1)
        {
            parts.push_back(segments[i] + "%");
        }
        else
        {
            parts.push_back(segments[i]);
        }
    }
    return join_segments(parts);
}

std::string build_key_prefix_upper_bound(const std::string &prefix)
{
    return prefix + "{";
}

bool is_pure_jianpin(const Segments &segments)
{
    return std::all_of(segments.begin(), segments.end(),
                       [](const std::string &segment) { return segment.size() == 1; });
}

bool is_shuangpin_initial_token(const std::string &segment)
{
    return segment == "zh" || segment == "ch" || segment == "sh";
}

bool needs_mixed_jianpin_query(const Segments &segments, QuerySource source)
{
    if (segments.size() <= 1)
    {
        return false;
    }

    return std::any_of(segments.begin(), segments.end() - 1, [source](const std::string &segment) {
        if (segment.size() == 1)
        {
            return true;
        }
        return source == QuerySource::Shuangpin && is_shuangpin_initial_token(segment);
    });
}

std::string extract_initial_token(const std::string &segment)
{
    if (segment.size() >= 2)
    {
        const auto prefix = segment.substr(0, 2);
        if (prefix == "zh" || prefix == "ch" || prefix == "sh")
        {
            return prefix;
        }
    }
    return segment.empty() ? "" : segment.substr(0, 1);
}

bool matches_mixed_segments(const std::string &key, const Segments &segments, QuerySource source)
{
    const auto key_segments = CommonUtils::split_by_delimiter(key, '\'');
    if (key_segments.size() != segments.size())
    {
        return false;
    }

    for (size_t i = 0; i < segments.size(); ++i)
    {
        const auto &expected = segments[i];
        const auto &actual = key_segments[i];

        if (expected.empty() || actual.empty())
        {
            return false;
        }

        const bool is_strict_shuangpin_initial =
            source == QuerySource::Shuangpin && is_shuangpin_initial_token(expected);
        if (expected.size() == 1 || is_strict_shuangpin_initial)
        {
            if (source == QuerySource::Shuangpin)
            {
                if (extract_initial_token(actual) != expected)
                {
                    return false;
                }
            }
            else if (actual.front() != expected.front())
            {
                return false;
            }
            continue;
        }

        if (actual != expected)
        {
            return false;
        }
    }

    return true;
}

int build_mixed_jianpin_scan_limit(int limit)
{
    if (limit > INT_MAX / 16)
    {
        return INT_MAX;
    }
    return std::max(limit * 16, 128);
}

class SqliteDb
{
  public:
    explicit SqliteDb(const std::string &db_path)
    {
        sqlite3 *raw = nullptr;
        const int status = sqlite3_open(db_path.c_str(), &raw);
        metasequoia::SqliteDatabase opened(raw);
        if (status != SQLITE_OK)
        {
            const std::string message = opened != nullptr ? sqlite3_errmsg(opened.get()) : "sqlite open failed";
            throw std::runtime_error(message);
        }
        // Keep ad-hoc query connections consistent with the dictionary
        // objects: a concurrent commit should be waited out, not reported as
        // an empty result.
        sqlite3_busy_timeout(opened.get(), kDictionaryBusyTimeoutMs);
        db_ = std::move(opened);
    }

    sqlite3 *get() const
    {
        return db_.get();
    }

  private:
    metasequoia::SqliteDatabase db_;
};

void append_keyed_query_rows(std::vector<KeyedQueryItem> &rows, sqlite3_stmt *statement)
{
    while (sqlite3_step(statement) == SQLITE_ROW)
    {
        const unsigned char *key = sqlite3_column_text(statement, 0);
        const unsigned char *value = sqlite3_column_text(statement, 1);
        rows.push_back(KeyedQueryItem{key == nullptr ? "" : reinterpret_cast<const char *>(key),
                                      value == nullptr ? "" : reinterpret_cast<const char *>(value),
                                      sqlite3_column_int64(statement, 2)});
    }
}

std::vector<KeyedQueryItem> run_keyed_query(sqlite3 *db, const std::string &sql, const std::string &value, int limit)
{
    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK)
    {
        return {};
    }
    Statement guard(stmt);

    sqlite3_bind_text(stmt, 1, value.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 2, limit);

    std::vector<KeyedQueryItem> rows;
    append_keyed_query_rows(rows, stmt);
    return rows;
}

std::vector<KeyedQueryItem> run_keyed_query(sqlite3 *db, metasequoia::SqliteStatementCache &statement_cache,
                                            const std::string &sql, const std::string &value, int limit)
{
    sqlite3_stmt *stmt = prepare_cached_statement(db, statement_cache, sql);
    if (stmt == nullptr)
        return {};

    sqlite3_bind_text(stmt, 1, value.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 2, limit);

    std::vector<KeyedQueryItem> rows;
    append_keyed_query_rows(rows, stmt);
    sqlite3_reset(stmt);
    sqlite3_clear_bindings(stmt);
    return rows;
}

std::vector<KeyedQueryItem> run_keyed_query(sqlite3 *db, const std::string &sql, const std::string &lower_bound,
                                            const std::string &upper_bound, int limit)
{
    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK)
    {
        return {};
    }
    Statement guard(stmt);

    sqlite3_bind_text(stmt, 1, lower_bound.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, upper_bound.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 3, limit);

    std::vector<KeyedQueryItem> rows;
    append_keyed_query_rows(rows, stmt);
    return rows;
}

std::vector<KeyedQueryItem> run_keyed_query(sqlite3 *db, metasequoia::SqliteStatementCache &statement_cache,
                                            const std::string &sql, const std::string &lower_bound,
                                            const std::string &upper_bound, int limit)
{
    sqlite3_stmt *stmt = prepare_cached_statement(db, statement_cache, sql);
    if (stmt == nullptr)
        return {};

    sqlite3_bind_text(stmt, 1, lower_bound.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, upper_bound.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 3, limit);

    std::vector<KeyedQueryItem> rows;
    append_keyed_query_rows(rows, stmt);
    sqlite3_reset(stmt);
    sqlite3_clear_bindings(stmt);
    return rows;
}

std::vector<KeyedQueryItem> run_keyed_batch_query(sqlite3 *db, metasequoia::SqliteStatementCache &statement_cache,
                                                  const std::string &table, const std::vector<std::string> &keys,
                                                  int limit)
{
    if (db == nullptr || table.empty() || keys.empty() || limit <= 0)
    {
        return {};
    }

    std::string placeholders;
    for (size_t index = 0; index < keys.size(); ++index)
    {
        if (index > 0)
        {
            placeholders += ',';
        }
        placeholders += '?';
    }
    const std::string sql = "SELECT \"key\", \"value\", \"weight\" FROM \"" + table + "\" WHERE \"key\" IN (" +
                            placeholders + ") ORDER BY \"weight\" DESC LIMIT ?";

    sqlite3_stmt *stmt = prepare_cached_statement(db, statement_cache, sql);
    if (stmt == nullptr)
        return {};

    for (size_t index = 0; index < keys.size(); ++index)
    {
        sqlite3_bind_text(stmt, static_cast<int>(index + 1), keys[index].c_str(), -1, SQLITE_TRANSIENT);
    }
    sqlite3_bind_int(stmt, static_cast<int>(keys.size() + 1), limit);

    std::vector<KeyedQueryItem> rows;
    append_keyed_query_rows(rows, stmt);
    sqlite3_reset(stmt);
    sqlite3_clear_bindings(stmt);
    return rows;
}

std::vector<KeyedQueryItem> filter_mixed_jianpin_rows(const std::vector<KeyedQueryItem> &rows, const Segments &segments,
                                                      int limit, QuerySource source)
{
    std::vector<KeyedQueryItem> matched;
    matched.reserve(static_cast<size_t>(std::min(limit, static_cast<int>(rows.size()))));
    for (const auto &row : rows)
    {
        if (!matches_mixed_segments(row.key, segments, source))
        {
            continue;
        }

        matched.push_back(row);
        if (static_cast<int>(matched.size()) >= limit)
        {
            break;
        }
    }
    return matched;
}

std::vector<QueryItem> without_keys(const std::vector<KeyedQueryItem> &rows)
{
    std::vector<QueryItem> result;
    result.reserve(rows.size());
    for (const auto &row : rows)
    {
        result.emplace_back(row.value, row.weight);
    }
    return result;
}

void deduplicate_keyed_items_by_value(std::vector<KeyedQueryItem> &items)
{
    std::unordered_set<std::string> seen;
    items.erase(std::remove_if(items.begin(), items.end(),
                               [&](const KeyedQueryItem &item) { return !seen.insert(item.value).second; }),
                items.end());
}

std::vector<KeyedQueryItem> query_single_cut_keyed(sqlite3 *db, const Segments &segments, int limit, QuerySource source)
{
    const auto table = build_table_name_impl(segments);
    if (table.empty())
    {
        return {};
    }

    const auto key = join_segments(segments);
    const auto jp = segments_to_jianpin_impl(segments);
    const auto mixed_query_limit = build_mixed_jianpin_scan_limit(limit);
    const auto needs_mixed_query = needs_mixed_jianpin_query(segments, source);
    const auto key_prefix_pattern = build_key_like_pattern(segments);
    const auto key_prefix = key_prefix_pattern.substr(0, key_prefix_pattern.size() - 1);
    const auto key_prefix_upper_bound = build_key_prefix_upper_bound(key_prefix);

    const auto exact_sql = "SELECT \"key\", \"value\", \"weight\" FROM \"" + table +
                           "\" WHERE \"key\" = ? ORDER BY \"weight\" DESC LIMIT ?";
    std::vector<KeyedQueryItem> rows;
    if (has_only_complete_pinyin_segments(segments))
    {
        rows = run_keyed_query(db, exact_sql, key, limit);
        if (!rows.empty())
        {
            return rows;
        }
    }

    const auto prefix_sql = "SELECT \"key\", \"value\", \"weight\" FROM \"" + table +
                            "\" WHERE \"key\" >= ? AND \"key\" < ? ORDER BY \"weight\" DESC LIMIT ?";
    rows = run_keyed_query(db, prefix_sql, key_prefix, key_prefix_upper_bound, limit);
    if (!rows.empty())
    {
        return rows;
    }

    if (needs_mixed_query)
    {
        const auto mixed_sql = "SELECT \"key\", \"value\", \"weight\" FROM \"" + table +
                               "\" WHERE \"jp\" = ? ORDER BY \"weight\" DESC LIMIT ?";
        rows =
            filter_mixed_jianpin_rows(run_keyed_query(db, mixed_sql, jp, mixed_query_limit), segments, limit, source);
        if (!rows.empty())
        {
            return rows;
        }
    }

    if (!is_pure_jianpin(segments))
    {
        return {};
    }

    const auto jp_sql = "SELECT \"key\", \"value\", \"weight\" FROM \"" + table +
                        "\" WHERE \"jp\" = ? ORDER BY \"weight\" DESC LIMIT ?";
    return run_keyed_query(db, jp_sql, jp, limit);
}

std::vector<KeyedQueryItem> query_single_cut_keyed(sqlite3 *db, metasequoia::SqliteStatementCache &statement_cache,
                                                   const Segments &segments, int limit, QuerySource source)
{
    const auto table = build_table_name_impl(segments);
    if (table.empty())
    {
        return {};
    }

    const auto key = join_segments(segments);
    const auto jp = segments_to_jianpin_impl(segments);
    const auto mixed_query_limit = build_mixed_jianpin_scan_limit(limit);
    const auto needs_mixed_query = needs_mixed_jianpin_query(segments, source);
    const auto key_prefix_pattern = build_key_like_pattern(segments);
    const auto key_prefix = key_prefix_pattern.substr(0, key_prefix_pattern.size() - 1);
    const auto key_prefix_upper_bound = build_key_prefix_upper_bound(key_prefix);

    const auto exact_sql = "SELECT \"key\", \"value\", \"weight\" FROM \"" + table +
                           "\" WHERE \"key\" = ? ORDER BY \"weight\" DESC LIMIT ?";
    std::vector<KeyedQueryItem> rows;
    if (has_only_complete_pinyin_segments(segments))
    {
        rows = run_keyed_query(db, statement_cache, exact_sql, key, limit);
        if (!rows.empty())
        {
            return rows;
        }
    }

    const auto prefix_sql = "SELECT \"key\", \"value\", \"weight\" FROM \"" + table +
                            "\" WHERE \"key\" >= ? AND \"key\" < ? ORDER BY \"weight\" DESC LIMIT ?";
    rows = run_keyed_query(db, statement_cache, prefix_sql, key_prefix, key_prefix_upper_bound, limit);
    if (!rows.empty())
    {
        return rows;
    }

    if (needs_mixed_query)
    {
        const auto mixed_sql = "SELECT \"key\", \"value\", \"weight\" FROM \"" + table +
                               "\" WHERE \"jp\" = ? ORDER BY \"weight\" DESC LIMIT ?";
        rows = filter_mixed_jianpin_rows(run_keyed_query(db, statement_cache, mixed_sql, jp, mixed_query_limit),
                                         segments, limit, source);
        if (!rows.empty())
        {
            return rows;
        }
    }

    if (!is_pure_jianpin(segments))
    {
        return {};
    }

    const auto jp_sql = "SELECT \"key\", \"value\", \"weight\" FROM \"" + table +
                        "\" WHERE \"jp\" = ? ORDER BY \"weight\" DESC LIMIT ?";
    return run_keyed_query(db, statement_cache, jp_sql, jp, limit);
}

void append_unique_query_items(std::vector<QueryItem> &target, const std::vector<QueryItem> &items,
                               std::unordered_set<std::string> &seen)
{
    for (const auto &item : items)
    {
        if (seen.insert(item.first).second)
            target.push_back(item);
    }
}

std::vector<QueryItem> sort_and_limit_query_items(std::vector<QueryItem> items, int limit)
{
    std::sort(items.begin(), items.end(),
              [](const QueryItem &lhs, const QueryItem &rhs) { return lhs.second > rhs.second; });
    if (static_cast<int>(items.size()) > limit)
        items.resize(static_cast<size_t>(limit));
    return items;
}

void stable_sort_keyed_query_items_by_weight(std::vector<KeyedQueryItem> &items)
{
    std::stable_sort(items.begin(), items.end(),
                     [](const KeyedQueryItem &lhs, const KeyedQueryItem &rhs) { return lhs.weight > rhs.weight; });
}

std::vector<KeyedQueryItem> sort_and_limit_keyed_query_items(std::vector<KeyedQueryItem> items, int limit)
{
    std::sort(items.begin(), items.end(),
              [](const KeyedQueryItem &lhs, const KeyedQueryItem &rhs) { return lhs.weight > rhs.weight; });
    if (static_cast<int>(items.size()) > limit)
        items.resize(static_cast<size_t>(limit));
    return items;
}

std::vector<QueryItem> query_single_cut(sqlite3 *db, const Segments &segments, int limit, QuerySource source)
{
    return without_keys(query_single_cut_keyed(db, segments, limit, source));
}

std::vector<QueryItem> query_single_cut(sqlite3 *db, metasequoia::SqliteStatementCache &statement_cache,
                                        const Segments &segments, int limit, QuerySource source)
{
    return without_keys(query_single_cut_keyed(db, statement_cache, segments, limit, source));
}

} // namespace

Segments cut_pinyin_greedy(const std::string &pinyin, bool intact_only)
{
    if (pinyin.empty())
    {
        return {};
    }

    if (pinyin.find('\'') == std::string::npos)
    {
        return cut_one_piece_min_segments(pinyin, intact_only);
    }

    Segments merged;
    for (const auto &part : CommonUtils::split_by_delimiter(pinyin, '\''))
    {
        auto cut = cut_one_piece_min_segments(part, intact_only);
        if (cut.empty())
        {
            if (intact_only && !part.empty())
            {
                return {};
            }
            if (!part.empty())
            {
                merged.push_back(part);
            }
            continue;
        }
        merged.insert(merged.end(), cut.begin(), cut.end());
    }
    return merged;
}

std::vector<Segments> cut_pinyin_by_mode(const std::string &pinyin, const std::string &mode)
{
    if (mode != "greedy" && mode != "correction")
    {
        throw std::invalid_argument("mode must be one of: greedy, correction");
    }

    if (mode == "greedy")
    {
        const auto greedy = cut_pinyin_greedy(pinyin, false);
        if (!greedy.empty())
        {
            return {greedy};
        }
        return {};
    }

    const auto corrected_paths = cut_pinyin_with_corrections(pinyin);
    if (!corrected_paths.empty())
    {
        return corrected_paths;
    }

    const auto greedy = cut_pinyin_greedy(pinyin, false);
    if (!greedy.empty())
    {
        return {greedy};
    }

    return {};
}

Segments split_segments(const std::string &segmentation)
{
    if (segmentation.empty())
    {
        return {};
    }

    return CommonUtils::split_by_delimiter(segmentation, '\'');
}

std::string join_segments(const Segments &segments, const std::string &delimiter)
{
    std::string joined;
    for (size_t i = 0; i < segments.size(); ++i)
    {
        if (i > 0)
        {
            joined += delimiter;
        }
        joined += segments[i];
    }
    return joined;
}

std::string build_table_name(const Segments &segments)
{
    return build_table_name_impl(segments);
}

bool has_valid_word_pinyin(const std::string &key, const std::string &jp, const std::string &word,
                           bool allow_shuangpin_fallback)
{
    const std::string pure_key = CommonUtils::remove_apostrophe_delimiters(key);
    if (pure_key.empty())
    {
        return false;
    }

    const size_t han_count = HelpcodeUtils::count_han_chars(word);
    if (jp.size() != han_count)
    {
        return false;
    }

    const auto cuts = cut_pinyin_by_mode(pure_key, "correction");
    if (!cuts.empty())
    {
        return cuts.front().size() == han_count;
    }

    return allow_shuangpin_fallback && pure_key.size() % 2 == 0 && pure_key.size() == han_count * 2;
}

std::string build_sql_for_creating_word(const std::string &pinyin)
{
    const auto cuts = cut_pinyin_by_mode(pinyin, "correction");
    if (cuts.empty())
    {
        return {};
    }

    std::string sql;
    for (size_t i = 1; i <= cuts.front().size(); ++i)
    {
        const Segments partial(cuts.front().begin(), cuts.front().begin() + i);
        const std::string key = join_segments(partial);
        const std::string table = build_table_name(partial);
        const std::string each =
            "select * from(select * from " + table + " where key = '" + key + "' order by weight desc)";
        sql = sql.empty() ? each : each + " union all " + sql;
    }
    return sql;
}

std::string build_sql_for_checking_word(const std::string &key, const std::string &value)
{
    const auto cuts = cut_pinyin_by_mode(key, "correction");
    if (cuts.empty())
    {
        return {};
    }

    const std::string table = build_table_name(cuts.front());
    return "select 1 from " + table + " where key = '" + CommonUtils::escape_sql_literal(key) + "' and value = '" +
           CommonUtils::escape_sql_literal(value) + "';";
}

std::string build_sql_for_inserting_word(const std::string &key, const std::string &jp, const std::string &value)
{
    const auto cuts = cut_pinyin_by_mode(key, "correction");
    if (cuts.empty())
    {
        return {};
    }

    const std::string table = build_table_name(cuts.front());
    return "insert into " + table + " (key, jp, value, weight) values ('" + CommonUtils::escape_sql_literal(key) +
           "', '" + CommonUtils::escape_sql_literal(jp) + "', '" + CommonUtils::escape_sql_literal(value) +
           "', '10000');";
}

std::string build_sql_for_updating_word(const Segments &segments, const std::string &key, const std::string &value)
{
    const std::string table = build_table_name(segments);
    const std::string escaped_key = CommonUtils::escape_sql_literal(key);
    return "update " + table + " set weight = ( select MAX(weight) + 1 from " + table + " AS sub where sub.key = '" +
           escaped_key + "') where key = '" + escaped_key + "' and value = '" + CommonUtils::escape_sql_literal(value) +
           "';";
}

std::string build_sql_for_deleting_word(const Segments &segments, const std::string &key, const std::string &value)
{
    return "delete from " + build_table_name(segments) + " where key = '" + CommonUtils::escape_sql_literal(key) +
           "' and value = '" + CommonUtils::escape_sql_literal(value) + "';";
}

std::string segments_to_jianpin(const Segments &segments)
{
    return segments_to_jianpin_impl(segments);
}

std::string get_default_db_path()
{
    return metasequoia::path_to_utf8(shuangpin::get_data_file_path(metasequoia::assets::main_dictionary));
}

void warm_up(sqlite3 *db, metasequoia::SqliteStatementCache &statement_cache)
{
    (void)intact_pinyin_set();
    (void)prefix_pinyin_set();

    if (db == nullptr)
    {
        return;
    }

    // Warm the most common single-syllable prefixes to hide first-query setup costs.
    (void)query_single_cut(db, statement_cache, Segments{"n"}, 1, QuerySource::Quanpin);
    (void)query_single_cut(db, statement_cache, Segments{"ni"}, 1, QuerySource::Quanpin);
}

std::vector<KeyedQueryItem> query_initial(sqlite3 *db, const std::string &prefix, int limit)
{
    if (db == nullptr || prefix.empty() || prefix.front() < 'a' || prefix.front() > 'z' || limit <= 0)
    {
        return {};
    }

    const std::string table = "tbl_1_" + std::string(1, prefix.front());
    const std::string sql = "SELECT \"key\", \"value\", \"weight\" FROM \"" + table +
                            "\" WHERE \"key\" >= ?1 AND \"key\" < ?2 ORDER BY \"weight\" DESC LIMIT ?3";
    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK)
    {
        return {};
    }
    Statement guard(stmt);

    const std::string upper_bound = prefix + "{";
    sqlite3_bind_text(stmt, 1, prefix.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, upper_bound.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 3, limit);

    std::vector<KeyedQueryItem> rows;
    append_keyed_query_rows(rows, stmt);
    return rows;
}

QueryResult query_words(const std::string &pinyin, const std::string &db_path, const std::string &mode, int limit)
{
    const auto cuts = cut_pinyin_by_mode(pinyin, mode);
    QueryResult result{pinyin, mode, {}};
    if (cuts.empty())
    {
        return result;
    }

    SqliteDb db(db_path);
    for (const auto &segments : cuts)
    {
        result.results.push_back(QueryResultEntry{
            segments,
            join_segments(segments),
            build_table_name(segments),
            query_single_cut(db.get(), segments, limit, QuerySource::Quanpin),
        });
    }
    return result;
}

QueryResult query_segments(const Segments &segments, const std::string &db_path, int limit, QuerySource source)
{
    QueryResult result{join_segments(segments), "precut", {}};
    if (segments.empty())
    {
        return result;
    }

    SqliteDb db(db_path);
    result.results.push_back(QueryResultEntry{
        segments,
        join_segments(segments),
        build_table_name(segments),
        query_single_cut(db.get(), segments, limit, source),
    });
    return result;
}

std::vector<QueryItem> query_words_flat(const std::string &pinyin, const std::string &db_path, const std::string &mode,
                                        int limit)
{
    const auto result = query_words(pinyin, db_path, mode, limit);
    std::vector<QueryItem> items;
    std::unordered_set<std::string> seen;
    for (const auto &entry : result.results)
        append_unique_query_items(items, entry.items, seen);

    return sort_and_limit_query_items(std::move(items), limit);
}

std::vector<QueryItem> query_segments_flat(const Segments &segments, const std::string &db_path, int limit,
                                           QuerySource source)
{
    const auto result = query_segments(segments, db_path, limit, source);
    std::vector<QueryItem> items;
    std::unordered_set<std::string> seen;
    for (const auto &entry : result.results)
        append_unique_query_items(items, entry.items, seen);

    return sort_and_limit_query_items(std::move(items), limit);
}

std::vector<QueryItem> query_segments_flat(const Segments &segments, sqlite3 *db,
                                           metasequoia::SqliteStatementCache &statement_cache, int limit,
                                           QuerySource source)
{
    if (db == nullptr || segments.empty())
    {
        return {};
    }

    std::vector<QueryItem> items;
    std::unordered_set<std::string> seen;
    append_unique_query_items(items, query_single_cut(db, statement_cache, segments, limit, source), seen);

    return sort_and_limit_query_items(std::move(items), limit);
}

std::vector<KeyedQueryItem> query_segments_keyed_flat(const Segments &segments, const std::string &db_path, int limit,
                                                      QuerySource source)
{
    if (segments.empty())
    {
        return {};
    }

    SqliteDb db(db_path);
    if (db.get() == nullptr)
    {
        return {};
    }

    auto items = query_single_cut_keyed(db.get(), segments, limit, source);
    deduplicate_keyed_items_by_value(items);
    return sort_and_limit_keyed_query_items(std::move(items), limit);
}

std::vector<KeyedQueryItem> query_segments_keyed_flat(const Segments &segments, sqlite3 *db,
                                                      metasequoia::SqliteStatementCache &statement_cache, int limit,
                                                      QuerySource source)
{
    if (db == nullptr || segments.empty())
    {
        return {};
    }

    auto items = query_single_cut_keyed(db, statement_cache, segments, limit, source);
    deduplicate_keyed_items_by_value(items);
    return sort_and_limit_keyed_query_items(std::move(items), limit);
}

std::vector<KeyedQueryItem> query_exact_segmentations_keyed_flat(const std::vector<Segments> &segmentations,
                                                                 sqlite3 *db,
                                                                 metasequoia::SqliteStatementCache &statement_cache,
                                                                 int limit)
{
    if (db == nullptr || segmentations.empty() || limit <= 0)
    {
        return {};
    }

    std::map<std::string, std::vector<std::string>> keys_by_table;
    std::unordered_set<std::string> seen;
    for (const auto &segments : segmentations)
    {
        if (!has_only_complete_pinyin_segments(segments))
        {
            continue;
        }
        const std::string table = build_table_name_impl(segments);
        const std::string key = join_segments(segments);
        if (table.empty() || key.empty() || !seen.insert(table + '\n' + key).second)
        {
            continue;
        }
        keys_by_table[table].push_back(key);
    }

    std::vector<KeyedQueryItem> result;
    for (const auto &[table, keys] : keys_by_table)
    {
        auto rows = run_keyed_batch_query(db, statement_cache, table, keys, limit);
        result.insert(result.end(), std::make_move_iterator(rows.begin()), std::make_move_iterator(rows.end()));
    }
    stable_sort_keyed_query_items_by_weight(result);
    return result;
}

std::vector<KeyedQueryItem> query_longer_phrases_keyed(const Segments &segments, sqlite3 *db,
                                                       metasequoia::SqliteStatementCache &statement_cache,
                                                       std::size_t extra_syllables, int limit)
{
    if (db == nullptr || segments.size() < 2 || extra_syllables == 0 || limit <= 0 ||
        !has_only_complete_pinyin_segments(segments))
    {
        return {};
    }

    // 键里音节之间是 '\'',所以前缀补一个 '\'' 才只命中「整音节的续接」: ping'guo' 命中 ping'guo'ji,
    // 不会命中别的拼写。上界沿用 build_key_prefix_upper_bound 的 '{',它排在 'z' 之后。
    const std::string prefix = join_segments(segments) + '\'';
    const std::string upper_bound = build_key_prefix_upper_bound(prefix);
    const char initial = segments.front().front();

    std::vector<KeyedQueryItem> result;
    for (std::size_t extra = 1; extra <= extra_syllables; ++extra)
    {
        const std::string table = metasequoia::dictionary_format::quanpin_table(segments.size() + extra, initial);
        if (table.empty())
        {
            continue;
        }
        const std::string sql = "SELECT \"key\", \"value\", \"weight\" FROM \"" + table +
                                "\" WHERE \"key\" >= ? AND \"key\" < ? ORDER BY \"weight\" DESC LIMIT ?";
        auto rows = run_keyed_query(db, statement_cache, sql, prefix, upper_bound, limit);
        result.insert(result.end(), std::make_move_iterator(rows.begin()), std::make_move_iterator(rows.end()));
    }

    deduplicate_keyed_items_by_value(result);
    stable_sort_keyed_query_items_by_weight(result);
    if (static_cast<int>(result.size()) > limit)
    {
        result.resize(static_cast<std::size_t>(limit));
    }
    return result;
}

WordLatticeLookup make_lattice_db_lookup(sqlite3 *db, metasequoia::SqliteStatementCache &statement_cache,
                                         QuerySource source, int span_limit)
{
    return [db, &statement_cache, source, span_limit](const Segments &span) {
        const auto rows = query_segments_keyed_flat(span, db, statement_cache, span_limit, source);
        std::vector<LatticeLexeme> lexemes;
        lexemes.reserve(rows.size());
        for (const auto &row : rows)
            lexemes.push_back({row.key, row.value, row.weight});
        return lexemes;
    };
}

} // namespace quanpin
