#pragma once

#include "ime_session.h"
#include "input_session_types.h"
#include "candidate_queries.h"
#include "punctuation_policy.h"
#include "online_request_guard.h"
#include "../local_modes/date_time_query.h"
#include "../english/english_dictionary.h"
#include "word_item.h"
#include "../quanpin/engine.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace metasequoia
{
// Platform-neutral composition session shared by the native frontends. It owns an ImeSession and
// applies the key-handling and commit policy that each frontend would otherwise reimplement, so a
// frontend only has to translate platform key events into these calls.
class InputSession
{
  public:
    // Frontends pass their persisted options at session creation so every platform uses the same
    // engine configuration and commit policy.
    explicit InputSession(SchemeType scheme_type = SchemeType::Quanpin, unsigned quanpin_autocorrect_types = 0,
                          bool helpcode_enabled = true, bool chinese_punctuation_enabled = true,
                          bool candidate_learning_enabled = true, RuntimePaths paths = RuntimePaths::legacy());
    InputSession(SchemeType scheme_type, const ShuangpinProfile &shuangpin_profile,
                 RuntimePaths paths = RuntimePaths::legacy());

    // Feeds one lowercase ASCII letter or an in-composition apostrophe. Other input is rejected as
    // unhandled so the frontend can pass it through to the client application.
    KeyResult handle_character(char character, bool shift_only = false);
    // Applies a command. Every command is unhandled while no composition is active, which keeps
    // Backspace and Escape working normally in the client application.
    KeyResult handle_command(Command command);
    // Maps the visible 1-9 candidate keys and Chinese punctuation independently of platform UI.
    KeyResult handle_candidate_key(char character);
    KeyResult handle_punctuation(char character);
    // Commit the selected prefix and retain any unconsumed pinyin. Hosts insert
    // KeyResult::commit and then render the remaining preedit from this session.
    KeyResult select_candidate(std::size_t index);
    // Flush all remaining input for punctuation, scheme changes and host passthrough.
    KeyResult finish_composition(std::size_t first_index = 0);
    KeyResult select_candidate(const std::string &candidate);
    KeyResult select_candidate_edge(std::size_t index, CandidateEdge edge);
    KeyResult pin_candidate(std::size_t index);
    KeyResult remove_candidate(std::size_t index);
    const ShuangpinProfile &shuangpin_profile() const
    {
        return shuangpin_profile_;
    }
    KeyResult set_candidate_position(std::size_t index, int position);
    void enable_fixed_positions();
    void set_shuangpin_helpcode_enabled(bool enabled);
    void set_quanpin_helpcode_enabled(bool enabled);
    static bool is_supported_helpcode_schema(const std::string &schema);
    bool set_helpcode_schema(const std::string &schema);
    // Compatibility default for subsequently created sessions.
    static bool select_helpcode_schema(const std::string &schema);
    bool set_frequency_adjustment(FrequencyAdjustmentOptions options);
    void set_local_mode_options(LocalModeOptions options);
    const LocalModeOptions &local_mode_options() const;
    bool set_english_input_options(EnglishInputOptions options);
    const EnglishInputOptions &english_input_options() const;
    void set_mixed_expressive_options(MixedExpressiveOptions options);
    void set_wubi_input_options(metasequoia::WubiInputOptions options);
    const metasequoia::WubiInputOptions &wubi_input_options() const;
    void set_sentence_association(const SentenceAssociationOptions &options)
    {
        engine_.set_sentence_association(options);
    }
    void set_rescoring_context(std::string context)
    {
        engine_.set_rescoring_context(std::move(context));
    }
    const MixedExpressiveOptions &mixed_expressive_options() const;
    void set_dedicated_english_mode(bool enabled);
    bool dedicated_english_mode() const;
    LocalInputMode local_input_mode() const;
    void set_local_date_time_provider(std::function<local_modes::LocalDateTime()> provider);
    std::optional<OnlineQuery> online_query() const;
    bool apply_online_candidates(const OnlineQuery &query, const std::vector<std::string> &words,
                                 CandidateSource source);
    bool apply_online_candidate(const OnlineQuery &query, std::string candidate, CandidateSource source);

    SchemeType scheme_type() const;
    unsigned quanpin_autocorrect_types() const;
    bool helpcode_enabled() const;
    bool chinese_punctuation_enabled() const;
    bool candidate_learning_enabled() const;
    // Switching schemes discards the current composition. A frontend that promises to preserve
    // typed text must commit it before calling this method.
    void switch_scheme(SchemeType scheme_type);
    SchemeType scheme() const;

    bool has_composition() const;
    const std::string &preedit() const;
    std::string editing_text() const;
    std::size_t caret_position() const;
    const std::string &raw_segmentation() const;
    const std::string &normalized_segmentation() const;
    const std::vector<WordItem> &candidates() const;
    // Display annotations for the current candidate list (helpcodes or correction hints).
    std::vector<std::string> candidate_annotations() const;
    // True while the current composition is answered by the wubi mixed-pinyin fallback.
    bool answered_by_pinyin_fallback() const;

    // Advanced composition operations for hosts with their own asynchronous text insertion.
    // They share the same engine/configuration as the portable character/command API.
    struct SelectionTransition
    {
        bool continues_composition = false;
        std::string full_pure_pinyin;
        std::string current_segmentation;
        std::string current_segmentation_with_cases;
        std::string selected_canonical_pinyin;
        bool wubi_native = false;
    };

    struct CloudQueryState
    {
        bool should_query = false;
        std::string query_text;
        std::string cache_key;
        std::string committed_pinyin;
    };

    struct CreatingWordProgress
    {
        std::string pinyin;
        std::string word;
        std::string preedit;
        bool completed = false;
        bool can_store = false;
    };

    void handle_engine_key(ImeKeyCode vk, ImeModifierMask modifiers_down, ImeCharacter wch);
    void recompute_candidates();
    // Moves the caret without editing the raw composition. A missing value means the end of the
    // string; the position is clamped to editing_text().size(). Candidate decoding is refreshed
    // by the next recompute or key handling operation.
    void set_caret(std::optional<std::size_t> caret);
    // Raw offset consumed by the current candidate decode, floored to a complete pinyin unit.
    std::size_t prefix_end() const;
    // Original-cased raw input after prefix_end().
    std::string pending_suffix() const;
    SchemeType current_scheme_type() const;

    void reset_state();
    void reset_cache();

    const std::vector<WordItem> &get_candidates() const;
    bool expand_initial_candidates();
    std::optional<WordItem> find_candidate(const std::string &key, const std::string &value);

    const std::string &get_pinyin_sequence() const;
    const std::string &get_pinyin_sequence_with_cases() const;
    const std::string &get_pure_pinyin_sequence() const;
    const std::string &get_pinyin_segmentation() const;
    std::string get_pinyin_segmentation_with_cases() const;
    // Offsets in get_pinyin_sequence_with_cases() where one input unit starts,
    // always including 0 (when non-empty) and raw.size(). A unit is one syllable:
    // the `ma` of ni'hao'ma, one 1-2 key syllable in shuangpin. Schemes and
    // local modes without the unit model (wubi, japanese, U/K/E/M/J/Y/R and the
    // dedicated English scheme) return an empty vector, and hosts then fall back
    // to single-character editing. An active autocorrect/helpcode display is
    // mapped back to the raw spans the engine actually cut, so a deleted unit
    // can never leave half a segment or a stray separator behind. Consumed by
    // segment deletion (Ctrl+Backspace), so any later caret-movement feature
    // must use this same boundary set instead of re-deriving one.
    std::vector<std::size_t> segment_raw_boundaries() const;
    std::string get_quanpin() const;
    bool is_all_complete_pure_pinyin() const;
    bool wubi_unique_four_code() const;
    bool wubi_four_code_is_complete() const;
    bool has_active_helpcode() const;

    void set_pinyin_sequence(const std::string &pinyin_sequence);
    void set_pinyin_sequence_with_cases(const std::string &pinyin_sequence);

    int store_user_phrase(std::string pinyin, std::string word);
    int store_user_phrase_from_canonical_pinyin(std::string pinyin, std::string word);
    int pin_candidate(std::string pinyin, std::string word);
    int remove_candidate(std::string pinyin, std::string word);
    int cache_dynamic_candidate(const std::string &pinyin, const std::string &word, CandidateSource source);
    SelectionTransition advance_composition_after_selection(const std::string &selected_pinyin,
                                                            const std::string &selected_word,
                                                            const std::string &selected_canonical_pinyin,
                                                            SchemeType selected_scheme = SchemeType::Quanpin);
    // Whether selecting this candidate would finish the composition rather than leave input to
    // answer — that is, whether it covers the whole key.
    //
    // The same decision `advance_composition_after_selection` makes, lifted out of it so that it can
    // be asked about a candidate that has not been selected. That method now calls this rather than
    // repeating the condition, because two copies of a rule this load-bearing drift.
    //
    // It is exposed because a reranker needs it per candidate: only candidates that answer the same
    // key are alternatives to each other, and a prefix is not an alternative to a full answer. The
    // consumer cannot derive it — the candidate's own character count agrees with this only while a
    // key has one segmentation, and `xian` reads as both 现 and 西安.
    bool selection_completes_composition(const std::string &selected_pinyin, const std::string &selected_word,
                                         SchemeType selected_scheme = SchemeType::Quanpin) const;
    CloudQueryState get_cloud_query_state() const;
    CreatingWordProgress update_creating_word_progress(const std::string &current_pinyin,
                                                       const std::string &current_word,
                                                       const std::string &selected_word,
                                                       const SelectionTransition &selection_transition) const;

    void set_quanpin_autocorrect_types(unsigned autocorrect_types);
    void set_fuzzy_pinyin_options(metasequoia::FuzzyPinyinOptions options)
    {
        engine_.set_fuzzy_pinyin_options(options);
    }
    void set_chinese_punctuation_enabled(bool enabled)
    {
        chinese_punctuation_enabled_ = enabled;
    }
    void set_punctuation_lock(int lock)
    {
        punctuation_lock_ = lock;
    }
    void set_paired_punctuation_enabled(bool enabled)
    {
        punctuation_.set_paired_enabled(enabled);
    }
    void balance_paired_punctuation_after_auto_close(char opening)
    {
        punctuation_.balance_after_auto_close(opening);
    }
    void set_candidate_learning_enabled(bool enabled)
    {
        candidate_learning_enabled_ = enabled;
    }
    // Forwarded to the scheme session, which puts it on every QueryRequest it builds.
    void set_sentence_alternatives(bool enabled)
    {
        engine_.set_sentence_alternatives(enabled);
    }
    void set_shuangpin_preedit_uses_raw(bool enabled)
    {
        shuangpin_preedit_uses_raw_ = enabled;
    }

  private:
    const QueryRequest &request() const;
    const std::string *preedit_override() const;
    void apply_engine_input_options();
    bool is_shuangpin() const;
    bool is_wubi() const;
    // Wubi whose candidates came from the wubi table. A code answered by the quanpin
    // fallback carries pinyin words, so ranking, fixed positions and removal have to key
    // off the pinyin rather than off the code that produced them.
    bool wubi_candidates_are_native() const;
    std::size_t wubi_native_candidate_count() const;
    bool wubi_four_code_is_eligible() const;
    // The candidates on offer behave like pinyin: quanpin, shuangpin, or a wubi code the
    // table could not answer. Committing one of these commits a spelling out of a longer
    // one, so the rest of the composition has to survive the selection.
    bool candidates_follow_pinyin() const;
    bool is_japanese() const;
    static bool online_source_is_eligible(const OnlineQuery &query, CandidateSource source);
    void set_helpcode_enabled(SchemeType scheme_type, bool enabled);
    void clear_pending_sequence();
    void apply_pending_sequence();

    KeyResult commit(std::size_t index);
    bool try_enter_local_mode(char character, bool shift_only);
    KeyResult append_local_character(char character);
    KeyResult handle_local_character(char character);
    void refresh_after_sequence_change();
    KeyResult insert_at_caret(char character);
    KeyResult edit_at_caret(Command command);
    KeyResult replace_editing_text(std::string text, std::size_t caret);
    std::optional<std::size_t> caret_;
    // Candidates decoded for a caret prefix shorter than the live composition.
    std::vector<WordItem> prefix_candidates_;
    // Lowercased raw prefix used as the query/cache key.
    std::string prefix_query_input_;
    bool prefix_candidates_active_ = false;
    std::optional<std::string> update_local_candidates();
    void update_mixed_candidates();
    std::optional<std::string> refresh_after_candidate_change();
    bool online_query_matches(const OnlineQuery &query, CandidateSource source) const;
    bool finish_online_candidate_update(int result);
    std::size_t quantized_prefix_end() const;
    void refresh_prefix_candidates();
    void apply_candidate_positions(std::vector<WordItem> &items);
    std::string position_context(bool english, bool wubi = false) const;
    bool fixed_positions_enabled_ = false;
    void update_dedicated_english_candidates();
    void reset_composition();
    void discard_abandoned_phrase_progress();
    std::optional<std::string> learn_candidate(std::size_t index);
    std::optional<std::string> learn_sentence_candidate(const WordItem &selected);
    std::optional<std::string> adjust_candidate_frequency(std::size_t index, FrequencyAdjustmentOptions options,
                                                          bool force_top);

    CreatingWordProgress immediate_phrase_progress_;
    bool shuangpin_preedit_uses_raw_ = true;
    std::unique_ptr<QuanpinEngine> canonical_phrase_engine_;
    std::string pending_pinyin_sequence_;
    std::string pending_pinyin_sequence_with_cases_;
    bool has_pending_pinyin_sequence_ = false;
    bool has_pending_pinyin_sequence_with_cases_ = false;

    RuntimePaths paths_;
    CandidateQueries candidate_queries_;
    ImeSession engine_;
    // 位掩码（quanpin::kAutocorrect* 位），不是 bool：bool 会把邻键位截断丢失。
    unsigned quanpin_autocorrect_types_ = 0;
    bool quanpin_helpcode_enabled_ = true;
    bool shuangpin_helpcode_enabled_ = true;
    HelpcodeUtils::SharedKeymap helpcode_keymap_;
    bool chinese_punctuation_enabled_ = true;
    int punctuation_lock_ = 0;
    bool candidate_learning_enabled_ = true;
    PunctuationPolicy punctuation_;
    const ShuangpinProfile shuangpin_profile_;
    FrequencyAdjustmentOptions frequency_adjustment_;
    bool frequency_adjustment_configured_ = false;
    LocalModeOptions local_mode_options_;
    EnglishInputOptions english_input_options_;
    MixedExpressiveOptions mixed_expressive_options_;
    bool dedicated_english_mode_ = false;
    std::string dedicated_english_preedit_;
    std::vector<WordItem> dedicated_english_candidates_;
    std::vector<WordItem> mixed_candidates_;
    LocalInputMode local_input_mode_ = LocalInputMode::None;
    std::optional<SchemeType> temporary_original_scheme_;
    std::string local_preedit_;
    std::vector<WordItem> local_candidates_;
    std::function<local_modes::LocalDateTime()> local_date_time_provider_;
    OnlineRequestGuard online_requests_;
};
} // namespace metasequoia
