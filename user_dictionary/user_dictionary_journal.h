#pragma once

#include <string>
#include <vector>
#include <functional>
#include <optional>
#include "../core/word_item.h"

namespace user_dictionary
{
enum class DictionaryKind
{
    Pinyin,
    Wubi,
    QuickPhrase,
    English,
};

std::string default_user_db_path();
// Compatibility no-op; journal connections are operation-scoped.
void close_default_user_database();

bool record_upsert(const std::string &user_db_path, DictionaryKind kind, const std::string &key,
                   const std::string &value, std::int64_t weight, const std::string &display = {});
bool record_user_insert(const std::string &user_db_path, DictionaryKind kind, const std::string &key,
                        const std::string &value, std::int64_t weight, const std::string &display = {});
bool record_delete(const std::string &user_db_path, DictionaryKind kind, const std::string &key,
                   const std::string &value);
bool is_user_inserted(const std::string &user_db_path, DictionaryKind kind, const std::string &key,
                      const std::string &value);
bool ensure_user_database(const std::string &user_db_path);
bool record_pinyin_upsert_from_database(const std::string &main_db_path, const std::string &key,
                                        const std::string &value,
                                        const std::string &user_db_path = default_user_db_path());
// Promote an existing Wubi row above its code group and persist the new weight in the journal.
// The operation is transactional and never creates a missing row.
bool bump_wubi_weight(const std::string &main_db_path, const std::string &user_db_path, const std::string &key,
                      const std::string &value);

struct ReplayResult
{
    int applied = 0;
    int skipped = 0;
    int failed = 0;
    std::string error;
};

ReplayResult replay(const std::string &user_db_path, const std::string &main_db_path,
                    const std::string &english_db_path);

bool adjust_candidate_ranking(const std::string &main_db_path, const std::string &user_db_path,
                              const std::string &context_key, const std::vector<WordItem> &ordered_candidates,
                              const std::string &entry_key, const std::string &value, const std::string &mode,
                              int linear_step, int trigger_count, bool force_top, bool *ranking_changed = nullptr,
                              DictionaryKind kind = DictionaryKind::Pinyin);
bool adjust_english_candidate_ranking(const std::string &english_db_path, const std::string &user_db_path,
                                      const std::string &context_key, const std::vector<WordItem> &ordered_candidates,
                                      const std::string &entry_key, const std::string &value, const std::string &mode,
                                      int linear_step, int trigger_count, bool force_top,
                                      bool *ranking_changed = nullptr);
// Delete an exact dictionary key and persist its tombstone in one attached-database
// transaction. Supports Pinyin, Wubi and English; false preserves candidate rows and journal operations
// on statement/commit failures (not a cross-file power-loss guarantee in WAL mode).
bool delete_dictionary_candidate(const std::string &dictionary_db_path, const std::string &user_db_path,
                                 DictionaryKind kind, const std::string &entry_key, const std::string &value);
bool delete_english_candidate(const std::string &english_db_path, const std::string &user_db_path,
                              const std::string &entry_key, const std::string &value);
bool learn_entered_english_word(const std::string &english_db_path, const std::string &user_db_path,
                                const std::string &display, std::int64_t weight = 10);
bool set_fixed_position(const std::string &user_db_path, const std::string &context_key, const std::string &entry_key,
                        const std::string &value, int position);
bool clear_fixed_position(const std::string &user_db_path, const std::string &context_key, const std::string &entry_key,
                          const std::string &value);
bool set_or_clear_fixed_position(const std::string &user_db_path, const std::string &context_key,
                                 const std::string &entry_key, const std::string &value, int position);
bool is_fixed(const std::string &user_db_path, const std::string &context_key, const std::string &entry_key,
              const std::string &value);
// Cloud/AI suggestions are normally hoisted back to their fixed slots (cloud at
// index 1, AI at index 2). Set keep_dynamic_candidate_positions when the caller
// already decided where they belong, e.g. after helpcode filtering.
void apply_fixed_positions(
    const std::string &user_db_path, const std::string &context_key, std::vector<WordItem> &candidates,
    bool include_missing,
    const std::function<std::optional<WordItem>(const std::string &, const std::string &)> &find_candidate = {},
    bool keep_dynamic_candidate_positions = false);
} // namespace user_dictionary
