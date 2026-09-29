#pragma once

#include <cstddef>
#include <string>
#include <unordered_set>
#include <vector>

namespace quanpin
{
using Segments = std::vector<std::string>;

struct SyllableEdge
{
    size_t end = 0;
    std::string syllable;
};

struct SyllableGraph
{
    size_t input_length = 0;
    std::vector<std::vector<SyllableEdge>> edges;
};

const std::vector<std::string> &intact_pinyin_list();
const std::unordered_set<std::string> &intact_pinyin_set();
const std::unordered_set<std::string> &prefix_pinyin_set();
bool has_only_complete_pinyin_segments(const Segments &segments);
// Validates explicit canonical segmentation for a word: each segment must be
// complete pinyin input and the caller-supplied count must match the word's
// Han-character count.
bool has_expected_complete_pinyin_segments(const Segments &segments, std::size_t expected_count);
SyllableGraph build_syllable_graph(const std::string &pinyin);
std::vector<Segments> enumerate_complete_segmentations(const SyllableGraph &graph, size_t path_limit = 32);
std::vector<std::string> cut_one_piece_min_segments(const std::string &pinyin, bool intact_only);
bool is_complete_pinyin_input(const std::string &pinyin);
// Returns the initial token used by mixed jianpin matching. The retroflex
// initials zh/ch/sh consume two letters; every other syllable uses its first
// letter.
std::string initial_token(const std::string &syllable);
// Converts canonical v spellings to the forms accepted by the Google decoders.
// Manual apostrophe boundaries are preserved so callers can pass the result to
// an online or sentence decoder without losing the user's segmentation.
std::string to_google_spelling(const std::string &segmentation);
size_t detect_active_helpcode_length(const std::string &raw_input, const std::string &raw_input_with_cases);
std::string strip_active_helpcodes(const std::string &raw_input, const std::string &raw_input_with_cases);
std::string strip_active_helpcodes_with_cases(const std::string &raw_input, const std::string &raw_input_with_cases);
std::vector<Segments> sparse_pinyin_fallback_segments(const Segments &segments);

// Autocorrection type bits passed to autocorrect_cut. One mask lets the dictionary
// layer gate the whole feature with a single value while each type stays
// independently toggleable (quanpin.autocorrect_transposition / autocorrect_neighbor).
inline constexpr unsigned kAutocorrectTransposition = 1u << 0;
inline constexpr unsigned kAutocorrectNeighbor = 1u << 1;

// One segment of an autocorrect-aware cut. syllable is the canonical (possibly
// table-corrected) text used for dictionary lookups; raw_text/start describe the
// original letters the segment consumed so the preedit can keep showing what the
// user typed while separators follow the actual cut positions. Correction edges
// preserve length, so raw_text is always the same width as syllable.
struct AutocorrectCutSegment
{
    std::string syllable;
    std::string raw_text;
    size_t start = 0;
    bool corrected = false;
};

struct AutocorrectCut
{
    std::vector<AutocorrectCutSegment> segments;

    bool empty() const
    {
        return segments.empty();
    }
};

// Range-carrying variant of autocorrect_cut: identical gating (no type enabled,
// manual delimiters, overlong input) and identical cost model, but every segment
// also reports which raw letters it replaced.
AutocorrectCut autocorrect_cut_detail(const std::string &pinyin, unsigned autocorrect_types);

// Cuts the input into syllables, allowing at most kMaxAutocorrectEdges correction
// edges from the tables selected by autocorrect_types. Returns {} when no type is
// enabled, the input contains a manual delimiter, or no correction path exists.
Segments autocorrect_cut(const std::string &pinyin, unsigned autocorrect_types);

// True when the input reads as one or more legal syllables plus at most one trailing
// letter ("zheg" = zhe + g): a jianpin-intent shape the correction tables must not
// rewrite. Deliberately NOT true for all-consonant strings of 3+ letters: the engine
// has no multi-letter jianpin, so correction is the only useful reading of e.g.
// "bqng" -> bang. Inputs with manual delimiters return false; the correction path
// excludes them on its own.
bool looks_like_syllable_with_jianpin_tail(const std::string &pinyin);

} // namespace quanpin
