#pragma once

#include "../core/runtime_paths.h"
#include "../core/word_item.h"
#include "ngram_table.h"
#include "quanpin_utils.h"

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace quanpin
{

// Phrase-graph + Viterbi beam search over dictionary spans.
// Algorithm follows libpinyin PinyinLookup2 (unigram path score as a product of
// P(word), beam per syllable step) and sunpinyin's lattice columns.
// The unigram normalizer supplies the usual "fewer tokens win" bias.
//
// When WordLatticeOptions::bigram is supplied, each transition also earns
// bigram_weight * log(P(next|previous) / P(next)) from the table (see
// ngram_table.h). Without it every path spelling the same syllables is judged
// on its words' frequencies alone, which is why 配置于权限 used to beat
// 配置与权限: 于 is the commoner character and nothing else had an opinion.
// The term is a bonus rather than a replacement, so an absent pair leaves the
// path exactly where the unigram score put it.
//
// A trigram cannot be searched the same way without carrying two words of
// history in every beam entry, so WordLatticeOptions::trigram is applied after
// the search instead: the n best paths are rescored with what the third word of
// context adds over the second, then reordered. This is the reason to decode
// more paths than are shown. `emit` caps how many reach the candidate list, so
// a caller can search six and display one - the extra five exist to be
// reordered, not to fill the page with near-duplicate sentences.
//
// Ranking when merging into an existing candidate list:
//   1. Exact SQLite full-key hits (CandidateSource::Database / UserDatabase)
//   2. Lattice full-cover sentences (CandidateSource::Generated)
//   3. Google-pinyin Fallback, prefixes, and other remaining items
// Lattice never displaces a leading exact Database/UserDatabase full-cover
// (e.g. 高碳钢 for gktjgh). Fallback must not block lattice (e.g. 高碳钢镊子
// ahead of 高谈刚捏子).
//
// merge_lattice_candidates only runs at 3+ complete syllables. One- and
// two-syllable keys are already covered by exact SQLite lookup. Abbreviated
// quanpin segments (g'k't) are rejected so WordItem.canonical_pinyin stays a
// complete pronunciation.
//
// Lattice WordItem.weight is log_prob * 1000 and is often negative. List
// order is the ranking; do not sort these rows by weight.

struct LatticeLexeme
{
    std::string key;
    std::string value;
    std::int64_t weight = 0;
};

struct LatticePath
{
    std::string sentence;
    std::string key;
    double log_prob = 0;
    std::vector<std::string> words;
};

struct WordLatticeOptions
{
    int beam = 32;
    int nbest = 5;
    bool include_lattice_best = true;
    bool show_next_on_duplicate = false;
    // Cap on lexemes per span (injected lookups). DB lookup already applies
    // the same cap in query_segments_keyed_flat.
    int span_limit = 32;
    int max_phrase_syllables = 7;
    // Heuristic unigram normalizer vs phrase-length bonus. Single-char
    // msime.db weights are corpus counts; phrase weights are a smaller scale.
    //
    // The bonus was 3.0, which was never measured against the eval sets - it was a plausible number
    // for a term whose only job was to stop the decoder spelling a sentence out character by
    // character. Swept through the client's convert_eval with the ngram tables present, whole-sentence
    // top-1 rises monotonically up to 20 and stops moving after it: sentences-v1 0.850 -> 0.900,
    // sentences-v2 0.125 -> 0.189, quanpin-words-v1 unchanged at 0.768. Raising it further trades
    // sentences-v1 away for nothing.
    double unigram_z = 1e6;
    double phrase_length_bonus = 20.0;
    // Borrowed, not owned: one table is shared by every session and outlives them.
    const NgramTable *bigram = nullptr;
    const NgramTable *trigram = nullptr;
    // How much each context term is allowed to move a path. See tests/src/eval_sentences.cpp, and the
    // sweep recorded above: at bonus 20 the bigram term is worth doubling - sentences-v2 top-1 0.173
    // -> 0.189 and MRR 0.393 -> 0.401, sentences-v1 unchanged - while 3.0 starts trading page
    // coverage for it. The trigram weight moved nothing at any value tried, which is what a term that
    // only fires on a third word of history looks like on sets this short; it stays at 1.0 rather
    // than being tuned to a number the measurement cannot support.
    double bigram_weight = 2.0;
    double trigram_weight = 1.0;
    // How many of the decoded paths reach the candidate list. 0 emits all of
    // them, which is what a caller measuring the decoder wants; a caller
    // feeding a candidate page wants 1.
    int emit = 0;
    // How far, per swapped syllable, a repaired sentence has to beat the one it repaired. Swept on the
    // eval sets with the frame margin below at 4.0: at 4.0 repairs fire too readily and sentences-v1
    // drops 70.0% -> 66.7%, at 8.0 and above the repair that fixes the reported sentence stops firing.
    // 6.0 leaves sentences-v1 untouched, takes sentences-v2 46.2% -> 46.5% and its page 56.1% -> 57.7%,
    // and is what puts 我们的目标是做成开源社区的扛把子 in front of 抗八字.
    double repair_margin = 6.0;
    // How far, per syllable, the lattice's sentence has to beat the Google-Pinyin one before it takes
    // the leading seat. See WholeSentenceComparison::lattice_outranks_fallback.
    //
    // Swept on the eval sets: 0 hands the seat over whenever the graph can spell the fallback at all,
    // which costs sentences-v1 71.7% -> 58.3%; past 5 the lattice stops winning anything. Between 3.5
    // and 4 sentences-v1 is untouched and sentences-v2 goes 34.6% -> 45.8%, measured on the decoder
    // harness where neither number is softened by the reranker.
    double fallback_margin = 4.0;
};

using WordLatticeLookup = std::function<std::vector<LatticeLexeme>(const Segments &span)>;

using LatticeReranker = std::function<bool(std::vector<LatticePath> &paths)>;

struct SourcedLatticeReranker
{
    LatticeReranker rerank;
    CandidateSource source = CandidateSource::Generated;
};

std::vector<LatticePath> decode_word_lattice(const Segments &syllables, const WordLatticeLookup &lookup,
                                             const WordLatticeOptions &options = {});

// What the merge learned about the two whole-sentence sources, so the caller can order them without
// decoding a second time. `fallback_score` is empty when the dictionary cannot spell that sentence at
// all, which is the usual reason the Google-Pinyin answer exists in the first place.
struct WholeSentenceComparison
{
    bool decoded = false;
    double lattice_score = 0.0;
    std::optional<double> fallback_score;

    // Syllables the two sentences cover, so the margin below can be read per syllable.
    size_t syllables = 0;

    // The best sentence assembled from one source's frame and the other's disputed span, when one of
    // those beat both originals. Empty when the two sources agree, when neither can be spelled, or
    // when no swap scored better than what it was made from.
    std::string best_hybrid;
    std::optional<double> best_hybrid_score;
    // Syllables the repair replaced, which is what its margin is charged on.
    size_t best_hybrid_span = 0;

    // Whether the lattice's own sentence is enough better than the fallback to take its place.
    //
    // A bare `>` would always be true: the lattice score is the maximum over every path, and a
    // fallback the graph can spell is one of those paths, so it can never score higher. What the
    // comparison can say is by how much, and `margin_per_syllable` is where that becomes a decision -
    // see WordLatticeOptions::fallback_margin for what it is worth.
    bool lattice_outranks_fallback(double margin_per_syllable) const
    {
        return decoded && fallback_score.has_value() &&
               lattice_score > *fallback_score + margin_per_syllable * static_cast<double>(syllables);
    }

    // A repair earns the leading seat by beating the sentence it repaired, charged on the span it
    // replaced. Comparing it to the lattice score instead would never fire: that score is the maximum
    // over every path, and a repair the graph can spell is one of them.
    bool hybrid_leads(double margin_per_syllable) const
    {
        return decoded && best_hybrid_score.has_value() && fallback_score.has_value() &&
               *best_hybrid_score > *fallback_score + margin_per_syllable * static_cast<double>(best_hybrid_span);
    }
};

void merge_lattice_candidates(std::vector<WordItem> &candidates, const Segments &syllables,
                              const WordLatticeLookup &lookup, const std::string &typed_pinyin,
                              const WordLatticeOptions &options = {},
                              // Scored against the decoded paths on the same terms when given.
                              const std::string &fallback_sentence = {}, WholeSentenceComparison *comparison = nullptr,
                              const std::vector<SourcedLatticeReranker> &rerankers = {});

// Index of the first row a synthesised whole sentence may take, which is after the leading run of
// exact full-cover Database/UserDatabase hits. Both whole-sentence sources share it so neither can
// displace a dictionary entry that already answers the whole key, per the ranking above.
size_t whole_sentence_insert_position(const std::vector<WordItem> &candidates, size_t n_syllables);

// The one place the sentence decoder is configured.
//
// Quanpin and shuangpin decode sentences identically and used to each build this block themselves.
// That duplication is how the two paths drift: engine PR #156 found a ranking fix that had been
// applied to one and missed in the other for weeks. Configuring them from here means a change to
// either reaches both, or reaches neither.
//
// `alternatives` is what a host asks for when it intends to reorder the readings itself. The
// default answers with one, which is what a candidate page wants: the other readings are near
// duplicates of it, and no host crops them, so emitting them unasked would push the short
// candidates a user actually wants off the first page.
WordLatticeOptions make_sentence_lattice_options(const metasequoia::RuntimePaths &paths, bool alternatives = false);

} // namespace quanpin
