#pragma once

#include "../../core/input_session_types.h"
#include "../../core/runtime_paths.h"
#include "../../shuangpin/shuangpin_profile.h"
#include "../../core/fuzzy_pinyin_options.h"
#include "../../core/sentence_association_options.h"
#include "personal_dictionary.h"
#include <memory>

namespace metasequoia
{
struct SessionOptions
{
    RuntimePaths paths;
    SchemeType scheme = SchemeType::Quanpin;
    ShuangpinProfile shuangpin_profile = GetXiaoheShuangpinProfile();
    bool shuangpin_preedit_uses_raw = true;
    std::string helpcode_schema = "lantian";
    // Quanpin autocorrection type mask (quanpin::kAutocorrect* bits); 0 keeps the
    // user's spelling untouched, which is the default for a fresh install.
    unsigned autocorrect_types = 0;
    bool helpcode = true;
    bool chinese_punctuation = true;
    bool paired_punctuation = true;
    int punctuation_lock = 0;
    bool learning = true;
    // Hand back every whole-sentence reading the decoder found rather than only its best. Off by
    // default: a host that does not reorder and crop the readings itself would put several near
    // duplicate sentences on the first page of candidates.
    bool sentence_alternatives = false;
    // Whole-sentence sources. Google and the lattice remain enabled by default;
    // neural modes rerank the lattice n-best only when explicitly enabled.
    SentenceAssociationOptions sentence_association;
    std::string rescoring_context;
    FuzzyPinyinOptions fuzzy_pinyin;
    FrequencyAdjustmentOptions frequency;
    LocalModeOptions local_modes;
    EnglishInputOptions english;
    MixedExpressiveOptions expressive;
    WubiInputOptions wubi;
};

struct SessionSnapshot
{
    SchemeType scheme;
    LocalInputMode local_mode;
    std::string preedit;
    std::string raw_segmentation;
    std::string normalized_segmentation;
    std::vector<WordItem> candidates;
    bool dedicated_english = false;
    // ASCII source text and offset, separate from rendered preedit (e.g. Japanese kana).
    std::string editing_text;
    std::size_t caret_position = 0;
    std::vector<std::string> nine_key_spellings;
    // The candidates were produced by the wubi mixed-pinyin fallback rather than by the wubi table.
    // A host that acts on candidate counts needs this: four letters answered by one pinyin word is
    // not the "unique four-code wubi candidate" that auto-commit is looking for, and committing it
    // would take away the fifth letter the fallback exists to allow.
    bool answered_by_pinyin_fallback = false;
    std::string shuangpin_profile;
    // Candidate source in the same order as candidates. Hosts can render source-specific
    // affordances without inferring them from display text or local-mode names.
    std::vector<CandidateSource> candidate_sources;
    // Display annotations aligned with candidates (helpcodes or correction hints).
    std::vector<std::string> candidate_annotations;
    // Whether each candidate answers the whole key, in candidate order. False for a prefix or a
    // predictive completion that runs past the key — selecting one of those leaves input behind.
    //
    // A consumer that ranks candidates against each other needs this and cannot work it out:
    // candidates are alternatives only when they answer the same key, and character count says so
    // only while a key has one segmentation. `xian` reads as 现 and as 西安, both answering it. The
    // engine already decides this to advance the composition, so it is reported rather than
    // re-derived.
    std::vector<bool> candidate_answers_key;
};

// Stable platform entry point. One host serializes calls to its session; distinct sessions
// can run concurrently. Snapshots and online requests are values safe to hand to other threads.
// No SQLite, provider registry, raw key codes or mutable composition internals are exposed.
class Session
{
  public:
    explicit Session(SessionOptions options);
    ~Session();
    Session(const Session &) = delete;
    Session &operator=(const Session &) = delete;

    KeyResult character(char value, bool shift_only = false);
    // Call after finishing composition when changing keyboard layout. Only quanpin uses digits.
    void set_nine_key_enabled(bool enabled);
    KeyResult choose_nine_key_spelling(std::size_t index);
    KeyResult command(Command value);
    KeyResult candidate_key(char value);
    KeyResult punctuation(char value);
    // Live host mode override; preserves composition, caret and punctuation pairing.
    // When disabled punctuation() is unhandled; the host owns ASCII passthrough.
    void set_chinese_punctuation_enabled(bool enabled);
    // 0 follows the current mode, 1 forces Chinese punctuation, 2 forces ASCII.
    void set_punctuation_lock(int lock);
    void set_paired_punctuation_enabled(bool enabled);
    // Notify the engine that the host emitted the closing half of a paired punctuation mark.
    void balance_paired_punctuation_after_auto_close(char opening);
    KeyResult select(std::size_t index);
    KeyResult select_edge(std::size_t index, CandidateEdge edge);
    // Explicit user action: promote a dictionary candidate without committing input.
    // Invalid/unsupported candidates are unhandled; persistence failures carry a diagnostic.
    KeyResult pin(std::size_t index);
    // Remove a dictionary phrase without committing; single-character non-English
    // candidates are protected. Invalid/unsupported selections are unhandled.
    KeyResult remove(std::size_t index);
    // Fix a dictionary candidate to slot 1..5 in this input context, or clear it.
    KeyResult fix_position(std::size_t index, int position);
    KeyResult clear_position(std::size_t index);
    KeyResult finish();
    // Finish the whole composition, starting with the host-highlighted candidate.
    // Remaining segments use their leading candidate; an invalid index commits raw input.
    KeyResult finish(std::size_t first_index);
    void switch_scheme(SchemeType scheme);
    static bool is_supported_helpcode_schema(const std::string &schema);
    bool set_helpcode_schema(const std::string &schema);
    // Apply the same enable flag to quanpin and shuangpin, as SessionOptions::helpcode does.
    // Hosts with per-scheme preferences apply the selected preference after switching.
    void set_helpcode_enabled(bool enabled);
    void set_dedicated_english(bool enabled);
    // Answer an unmatched wubi code with quanpin candidates for the same letters.
    void set_wubi_mixed_pinyin(bool enabled);
    void reset_cache();
    void set_sentence_association(SentenceAssociationOptions options);
    void set_rescoring_context(std::string context);
    SessionSnapshot snapshot() const;
    // Authoritative offsets in snapshot().editing_text for pinyin-unit editing.
    // Empty for idle, local modes, non-pinyin schemes and active nine-key input.
    // A read-only query: no candidate generation, caret or composition changes.
    std::vector<std::size_t> segment_raw_boundaries() const;
    std::optional<OnlineQuery> online_query() const;
    bool apply_online_candidates(const OnlineQuery &query, const std::vector<std::string> &words,
                                 CandidateSource source);
    bool apply_online_candidate(const OnlineQuery &query, std::string candidate, CandidateSource source);

  private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace metasequoia
