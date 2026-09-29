#pragma once

#include "word_lattice.h"
#include "../common/sqlite_statement.h"

#include <sqlite3.h>

#include <string>
#include <utility>
#include <vector>
#include <cstdint>

namespace quanpin
{

using Segments = std::vector<std::string>;

struct KeyedQueryItem
{
    std::string key;
    std::string value;
    std::int64_t weight = 0;
};

enum class QuerySource
{
    Quanpin,
    Shuangpin,
};

Segments cut_pinyin_greedy(const std::string &pinyin, bool intact_only = false);
std::vector<Segments> cut_pinyin_by_mode(const std::string &pinyin, const std::string &mode = "greedy");
Segments split_segments(const std::string &segmentation);
std::string join_segments(const Segments &segments, const std::string &delimiter = "'");
std::string build_table_name(const Segments &segments);
// Validates the pinyin, jianpin, and Han-character counts used by dictionary word writes.
// Shuangpin may additionally accept an unresolved two-key-per-character input.
bool has_valid_word_pinyin(const std::string &key, const std::string &jp, const std::string &word,
                           bool allow_shuangpin_fallback = false);
std::string build_sql_for_checking_word(const std::string &key, const std::string &value);
std::string build_sql_for_inserting_word(const std::string &key, const std::string &jp, const std::string &value);
std::string build_sql_for_updating_word(const Segments &segments, const std::string &key, const std::string &value);
std::string segments_to_jianpin(const Segments &segments);
std::string get_default_db_path();
// A learning write holds the dictionary lock only for a short commit. Readers
// wait briefly for that commit instead of returning an empty candidate page.
constexpr int kDictionaryBusyTimeoutMs = 250;
void warm_up(sqlite3 *db, metasequoia::SqliteStatementCache &statement_cache);
std::vector<KeyedQueryItem> query_initial(sqlite3 *db, const std::string &prefix, int limit);
std::vector<KeyedQueryItem> query_segments_keyed_flat(const Segments &segments, const std::string &db_path,
                                                      int limit = 8, QuerySource source = QuerySource::Quanpin);
std::vector<KeyedQueryItem> query_segments_keyed_flat(const Segments &segments, sqlite3 *db,
                                                      metasequoia::SqliteStatementCache &statement_cache, int limit = 8,
                                                      QuerySource source = QuerySource::Quanpin);
// 以打完的整串音为前缀,到音节更多的表里取词组: 打 ping'guo 时 苹果电脑/苹果公司 的权重远高于
// 同长度里剩下的 评过/平果,但它们按音节数分在别的表,等长查询看不到。
std::vector<KeyedQueryItem> query_longer_phrases_keyed(const Segments &segments, sqlite3 *db,
                                                       metasequoia::SqliteStatementCache &statement_cache,
                                                       std::size_t extra_syllables, int limit);

std::vector<KeyedQueryItem> query_exact_segmentations_keyed_flat(const std::vector<Segments> &segmentations,
                                                                 sqlite3 *db,
                                                                 metasequoia::SqliteStatementCache &statement_cache,
                                                                 int limit = 128);

WordLatticeLookup make_lattice_db_lookup(sqlite3 *db, metasequoia::SqliteStatementCache &statement_cache,
                                         QuerySource source, int span_limit);

} // namespace quanpin
