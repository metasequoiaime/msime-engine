#pragma once
#include "../core/runtime_paths.h"
#include "../core/pinyin_decoder.h"
#include "../core/data_path.h"
#include "../contracts/assets/assets.h"

#include "../common/cache.h"
#include "../common/sqlite_database.h"
#include "../core/key_event.h"
#include "../core/word_item.h"
#include "quanpin_query.h"
#include "lattice_rerank.h"
#include "../core/sentence_association_options.h"
#include "../neural/neural_decoder.h"
#include "../core/fuzzy_pinyin_options.h"
#include <sqlite3.h>
#include <string>
#include <unordered_map>
#include <vector>
#include <optional>

class QuanpinDictionary
{
  public:
    static const int OK = 0;
    static const int ERROR_CODE = -1;

    explicit QuanpinDictionary(std::string db_path = {},
                               metasequoia::RuntimePaths paths = metasequoia::RuntimePaths::legacy());
    ~QuanpinDictionary();

    // Autocorrection is gated by a quanpin::kAutocorrect* type mask (0 = off).
    std::vector<WordItem> query(const std::string &raw_input, const std::string &segmentation = "",
                                unsigned autocorrect_types = 0, metasequoia::FuzzyPinyinOptions fuzzy = {});
    std::vector<WordItem> fuzzy_candidates(const std::string &segmentation, metasequoia::FuzzyPinyinOptions options);
    bool expand_initial_candidates(const std::string &code, std::vector<WordItem> &candidates);
    std::optional<WordItem> find_candidate(const std::string &key, const std::string &value);
    int handleVkCode(ImeKeyCode vk, ImeModifierMask modifiers_down, ImeCharacter wch = 0);

    // Ask the decoder for every whole-sentence reading it found rather than only its best. A host
    // wants this when it reorders the readings itself and crops the list before display; see
    // quanpin::make_sentence_lattice_options for why the default answers with one.
    //
    // A session-level setting rather than a query argument: it describes what the caller does with
    // the list, which does not change between keystrokes. Changing it clears the cached lists,
    // which were built under the previous answer.
    void set_sentence_alternatives(bool enabled);
    void set_sentence_association(const SentenceAssociationOptions &options);
    // The context invalidates cached ordering only when neural reranking is active.
    void set_rescoring_context(const std::string &context);

    int create_word(std::string pinyin, std::string word);
    int create_word_from_canonical_pinyin(std::string pinyin, std::string word);
    int update_weight_by_pinyin_and_word(std::string pinyin, std::string word);
    int delete_by_pinyin_and_word(std::string pinyin, std::string word);
    int insert_word_to_series_cache(const std::string &pinyin, const std::string &word, CandidateSource source);
    int insert_word_to_series_cache(const std::string &pinyin, const std::vector<std::string> &words,
                                    CandidateSource source);
    int insert_word_to_series_cache(const std::string &raw_input, const std::string &segmentation,
                                    unsigned autocorrect_types, const std::string &word, CandidateSource source);
    int insert_word_to_series_cache(const std::string &raw_input, const std::string &segmentation,
                                    unsigned autocorrect_types, const std::vector<std::string> &words,
                                    CandidateSource source);

    std::string search_sentence_from_ime_engine(const std::string &user_pinyin);

    void reset_state();
    void reset_cache();

    const std::string &get_pinyin_sequence() const
    {
        return pinyin_sequence_;
    }

    const std::string &get_pinyin_segmentation() const
    {
        return pinyin_segmentation_;
    }

    const std::vector<WordItem> &get_current_candidate_list() const
    {
        return current_candidate_list_;
    }

  private:
    std::vector<WordItem> query_exact(const std::string &raw_input, const std::string &segmentation,
                                      unsigned autocorrect_types);
    std::vector<WordItem> query_series(const std::string &raw_input, const std::string &segmentation,
                                       const quanpin::Segments &segments);
    // 打完整串音之后的续接词组,见 query_series 里的说明。
    std::vector<WordItem> append_longer_phrase_candidates(const std::string &segmentation,
                                                          const quanpin::Segments &segments);
    std::vector<WordItem> query_single_path(const std::string &raw_input, const std::string &segmentation,
                                            const quanpin::Segments &segments);
    quanpin::Segments resolve_segments(const std::string &raw_input, const std::string &segmentation);
    quanpin::Segments get_or_compute_segments(const std::string &raw_input);
    std::vector<WordItem> query_database(const quanpin::Segments &segments, const std::string &segmentation);
    std::vector<WordItem> query_initial(const std::string &code, int limit);
    std::vector<WordItem> append_ime_fallback(const std::string &raw_input, const std::string &segmentation,
                                              std::vector<WordItem> result);
    std::vector<WordItem> append_sparse_pinyin_fallbacks(const quanpin::Segments &segments,
                                                         std::vector<WordItem> result);
    std::vector<WordItem> merge_alternative_segmentations(
        const std::string &raw_input, const std::string &primary_segmentation,
        const quanpin::Segments &primary_segments, const std::vector<quanpin::Segments> &alternative_segmentations,
        std::vector<WordItem> result);
    void mark_autocorrect_candidates(std::vector<WordItem> &candidates, const std::string &raw_input);

    int check_data(const std::string &sql_str);
    int insert_data(const std::string &sql_str);
    int update_data(const std::string &sql_str);

    std::string build_sql_for_updating_word(const std::string &word);
    std::string build_sql_for_updating_word(std::string pinyin, const std::string &word);
    std::string build_sql_for_deleting_word(std::string pinyin, const std::string &word);
    void reset_cache_if_database_changed();
    int insert_word_to_series_cache_key(const std::string &cache_key, const std::string &pinyin,
                                        const std::vector<std::string> &words, CandidateSource source);
    int insert_word_to_series_cache_key(const std::string &cache_key, const std::string &pinyin,
                                        const std::string &word, CandidateSource source);

  private:
    CircularBuffer<std::string, std::vector<WordItem>> cache_;
    CircularBuffer<std::string, std::vector<WordItem>> series_cache_;
    CircularBuffer<std::string, quanpin::Segments> segmentation_cache_;
    metasequoia::SqliteDatabase db_;
    sqlite3_int64 data_version_ = -1;
    metasequoia::RuntimePaths paths_;
    bool sentence_alternatives_ = false;
    SentenceAssociationOptions sentence_association_;
    std::string rescoring_context_;
    metasequoia::PinyinDecoder decoder_;
    const neural::SentenceModel *neural_desktop_model_ = nullptr;
    const neural::SentenceModel *neural_keyboard_model_ = nullptr;
    metasequoia::SqliteStatementCache statement_cache_;
    std::string db_path_;

    std::string pinyin_sequence_;
    std::string pinyin_segmentation_;
    std::vector<WordItem> current_candidate_list_;
};
