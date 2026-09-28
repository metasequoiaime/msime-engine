#pragma once
#include "../common/helpcode_utils.h"
#include "../core/runtime_paths.h"
#include "../core/pinyin_decoder.h"
#include "../core/data_path.h"
#include "../contracts/assets/assets.h"

#include "../common/cache.h"
#include "../common/sqlite_database.h"
#include "../core/key_event.h"
#include "../core/word_item.h"
#include "../quanpin/quanpin_query.h"
#include "shuangpin_profile.h"
#include "../core/sentence_association_options.h"
#include "../neural/neural_decoder.h"
#include "../quanpin/lattice_rerank.h"
#include <shared_mutex>
#include <array>
#include <vector>
#include <unordered_map>
#include <string>
#include <sqlite3.h>
#include <memory>
#include <optional>
#include <boost/algorithm/string.hpp>

class ShuangpinDictionary
{
  public:
    using WordItem = ::WordItem;

    static const int OK = 0;
    static const int ERROR_CODE = -1;

    std::vector<WordItem> generate(             //
        const std::string &pinyin_sequence,     //
        const std::string &pinyin_segmentation, //
        const std::string &cache_key = ""       //
    );
    // Mirrors QuanpinDictionary::set_sentence_alternatives. The two paths decode sentences with the
    // same code and must answer a host's request the same way; engine PR #156 is what happens when
    // one of them is changed and the other is not.
    void set_sentence_alternatives(bool enabled);

    std::vector<WordItem> generateSeries(       //
        const std::string &pinyin_sequence,     //
        const std::string &pinyin_segmentation, //
        const std::string &cache_key = ""       //
    );
    std::vector<WordItem> generate_with_helpcodes(   //
        const std::string &pure_pinyin,              //
        const std::string &pure_pinyin_segmentation, //
        const std::string &pinyin_sequence,          //
        const std::string &help_codes                //
    );
    bool expand_initial_candidates();
    bool expand_initial_candidates(const std::string &code, std::vector<WordItem> &candidates,
                                   const std::string &series_cache_key = {});
    std::optional<WordItem> find_candidate(const std::string &key, const std::string &value);
    int handleVkCode(ImeKeyCode vk, ImeModifierMask modifiers_down, ImeCharacter wch = 0);
    std::vector<WordItem> generate_for_creating_word(const std::string code);
    int create_word(std::string pinyin, std::string word);
    int create_word_from_quanpin(std::string pinyin, std::string word);
    // 一次到顶
    int update_weight_by_word(std::string word);
    // 一次到顶
    int update_weight_by_pinyin_and_word(std::string pinyin, std::string word);
    int delete_by_pinyin_and_word(std::string pinyin, std::string word);

    /*
      Return: list of complete item data of database table
    */
    std::vector<WordItem> generate_tuple(const std::string code);

    std::string search_sentence_from_ime_engine(const std::string &user_pinyin);

    explicit ShuangpinDictionary(const ShuangpinProfile &profile = GetXiaoheShuangpinProfile(),
                                 metasequoia::RuntimePaths paths = metasequoia::RuntimePaths::legacy());
    ~ShuangpinDictionary();
    void set_sentence_association(const SentenceAssociationOptions &options);
    // The context invalidates cached ordering only when neural reranking is active.
    void set_rescoring_context(const std::string &context);

    const ShuangpinProfile &profile() const
    {
        return profile_;
    }

  private:
    const ShuangpinProfile profile_;
    std::string quanpin_db_path_;
    metasequoia::SqliteDatabase quanpin_db_;
    sqlite3_int64 data_version_ = -1;
    metasequoia::RuntimePaths paths_;
    bool sentence_alternatives_ = false;
    SentenceAssociationOptions sentence_association_;
    std::string rescoring_context_;
    metasequoia::PinyinDecoder decoder_;
    const neural::SentenceModel *neural_desktop_model_ = nullptr;
    const neural::SentenceModel *neural_keyboard_model_ = nullptr;
    HelpcodeUtils::SharedKeymap helpcodes_;
    std::unordered_map<std::string, sqlite3_stmt *> quanpin_statement_cache_;
    void reset_cache_if_database_changed();

    void generate_for_single_char(std::vector<WordItem> &candidate_list, std::string code);
    void filter_with_single_helpcode(                //
        const std::vector<WordItem> &candidate_list, //
        std::vector<WordItem> &filtered_list,        //
        const std::string &help_code,                //
        const std::string &pinyin_sequence           //
    );
    void filter_with_double_helpcodes(               //
        const std::vector<WordItem> &candidate_list, //
        std::vector<WordItem> &filtered_list,        //
        const std::string &help_codes                //
    );
    std::vector<WordItem> select_complete_data(sqlite3 *target_db, const std::string &sql_str);
    int check_data(sqlite3 *target_db, const std::string &sql_str);
    int insert_data(sqlite3 *target_db, const std::string &sql_str);
    int update_data(sqlite3 *target_db, const std::string &sql_str);
    int delete_data(sqlite3 *target_db, const std::string &sql_str);
    std::vector<WordItem> query_from_quanpin_database(const std::string &pinyin_sequence,
                                                      const std::string &pinyin_segmentation);
    std::vector<WordItem> query_initial_from_quanpin_database(const std::string &code, int limit);
    std::string normalize_shuangpin_to_quanpin_segmentation(const std::string &pinyin) const;
    std::string normalize_shuangpin_to_quanpin_input(const std::string &pinyin) const;
    std::string build_quanpin_sql_for_creating_word(const std::string &pinyin) const;
    std::string build_quanpin_sql_for_checking_word(const std::string &key, const std::string &jp,
                                                    const std::string &value) const;
    std::string build_quanpin_sql_for_inserting_word(const std::string &key, const std::string &jp,
                                                     const std::string &value) const;
    std::string build_quanpin_sql_for_updating_word(const std::string &word) const;
    std::string build_quanpin_sql_for_updating_word(std::string pinyin, const std::string &word) const;
    // The caller has already resolved the input (raw shuangpin or canonical
    // quanpin) to a canonical quanpin key before reaching this helper.  Do
    // not normalize it as shuangpin again: canonical keys are database keys,
    // not user input.
    std::string build_quanpin_sql_for_deleting_canonical_word(const std::string &canonical_pinyin,
                                                              const std::string &word) const;
    bool do_validate(std::string key, std::string jp, std::string value) const;

  private:
    // Lock
    std::shared_mutex mutex_; // Read-write separation lock

    // Whether in full help mode
    bool _is_full_help_mode = false;
    // Localtion of starting position
    int _help_mode_raw_pos = 0;                 // Start from pos, e.g. 妮: ninv: 2
    std::string _pinyin_helpcodes = "";         // Help codes
    std::vector<ImeKeyCode> _kb_input_sequence; // Keyboard input sequence
    std::string _pinyin_sequence = "";          // Pinyin extracted from from keyboard sequence
    std::string _pinyin_sequence_with_cases =
        ""; // Pinyin extracted from from keyboard sequence, but with letters' original cases
    std::string _pure_pinyin_sequence = "";        // Pinyin without help code
    std::array<char, 2> _help_codes_sequence = {}; // Help code extracted from from keyboard sequence
    std::string _pinyin_segmentation = "";         // Segmentation pinyin
    std::string _preedit_pinyin = "";              // Preedit
    /* Current candidate list, computed by current kb_input_sequence */
    std::vector<WordItem> _cur_candidate_list;
    std::vector<WordItem> _cur_page_candidate_list; // Current candidate list
    // boost::circular_buffer<std::pair<std::string, std::vector<WordItem>>> _cached_buffer;
    CircularBuffer<std::string, std::vector<WordItem>> _cached_buffer;              // 缓存纯拼音的结果
    CircularBuffer<std::string, std::vector<WordItem>> _cached_buffer_sgl;          // 缓存单码辅助结果
    CircularBuffer<std::string, std::vector<WordItem>> _cached_buffer_sgl_reversed; // 缓存反向单码辅助结果
    CircularBuffer<std::string, std::vector<WordItem>> _cached_buffer_dbl;          // 缓存双码辅助结果
    CircularBuffer<std::string, std::vector<WordItem>> _cached_buffer_series; // 缓存拼音序列对应的所有结果

  public:
    // Getters and setters
    bool get_full_help_mode()
    {
        return this->_is_full_help_mode;
    }
    void set_full_help_mode(bool is_full_help_mode)
    {
        this->_is_full_help_mode = is_full_help_mode;
    }

    int get_help_mode_raw_pos()
    {
        return this->_help_mode_raw_pos;
    }
    void set_help_mode_raw_pos(int raw_pos)
    {
        this->_help_mode_raw_pos = raw_pos;
    }

    const std::string &get_pinyin_sequence()
    {
        return this->_pinyin_sequence;
    }

    void set_pinyin_sequence(const std::string &pinyin_sequence)
    {
        this->_pinyin_sequence = pinyin_sequence;
    }

    const std::string &get_pinyin_sequence_with_cases()
    {
        return this->_pinyin_sequence_with_cases;
    }

    void set_pinyin_sequence_with_cases(const std::string &pinyin_sequence)
    {
        this->_pinyin_sequence_with_cases = pinyin_sequence;
    }

    const std::string &get_pinyin_segmentation()
    {
        return this->_pinyin_segmentation;
    }

    const std::string &get_pure_pinyin_sequence()
    {
        return this->_pure_pinyin_sequence;
    }

    const std::vector<WordItem> &get_current_candidate_list() const
    {
        return this->_cur_candidate_list;
    }

    const std::vector<WordItem> &get_cur_candiate_list() const
    {
        return get_current_candidate_list();
    }

    int insert_word_to_cached_buffer_series(const std::string &pinyin, const std::vector<std::string> &words,
                                            CandidateSource source);
    int insert_word_to_cached_buffer_series(const std::string &pinyin, const std::string &word, CandidateSource source);
    int insert_word_to_active_helpcode_cache(const std::string &pinyin, const std::vector<std::string> &words,
                                             CandidateSource source, const std::string &double_helpcodes = {});
    int insert_word_to_active_helpcode_cache(const std::string &pinyin, const std::string &word, CandidateSource source,
                                             const std::string &double_helpcodes = {});

    bool is_all_complete_pinyin();
    bool is_all_complete_pure_pinyin();
    std::string get_pinyin_segmentation_with_cases();

    std::string get_quanpin() const;
    std::string get_quanpin_seg() const;

    void reset_state();
    void reset_cache();
    void set_helpcode_keymap(HelpcodeUtils::SharedKeymap table)
    {
        helpcodes_ = std::move(table);
        reset_cache();
    }
};

using DictionaryUlPb = ShuangpinDictionary;
