#include "word_lattice.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <map>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace quanpin
{
namespace
{

constexpr double kNegInf = -std::numeric_limits<double>::infinity();

std::string join_span(const Segments &span)
{
    std::string key;
    for (size_t i = 0; i < span.size(); ++i)
    {
        if (i)
            key.push_back('\'');
        key += span[i];
    }
    return key;
}

size_t syllable_count_from_key(const std::string &key)
{
    if (key.empty())
        return 0;
    size_t n = 1;
    for (char c : key)
    {
        if (c == '\'')
            ++n;
    }
    return n;
}

struct LatticeEdge
{
    size_t end = 0;
    std::string word;
    std::string key;
    std::int64_t weight = 0;
    double log_prob = 0;
};

double edge_log_prob(std::int64_t weight, size_t syllables, const WordLatticeOptions &options)
{
    const double w = weight > 0 ? static_cast<double>(weight) : 1.0;
    const double z = options.unigram_z > 1.0 ? options.unigram_z : 1e6;
    const double lp = std::log(w);
    // Single-character rows in msime.db are raw corpus counts (often 1e6+);
    // multi-syllable rows are phrase weights on a much smaller scale.
    // libpinyin stores comparable log-probabilities; we approximate that by
    // down-projecting unigrams and giving dictionary phrases a length bonus.
    if (syllables <= 1)
        return lp - std::log(z);
    return lp + options.phrase_length_bonus * static_cast<double>(syllables);
}

struct Hyp
{
    double score = kNegInf;
    int prev_pos = -1;
    int prev_idx = -1;
    std::string word;
    std::string key;
};

void keep_beam(std::vector<Hyp> &column, int beam)
{
    if (static_cast<int>(column.size()) <= beam)
        return;
    std::partial_sort(column.begin(), column.begin() + beam, column.end(),
                      [](const Hyp &a, const Hyp &b) { return a.score > b.score; });
    column.resize(static_cast<size_t>(beam));
}

std::vector<std::vector<LatticeEdge>> build_graph(const Segments &syllables, const WordLatticeLookup &lookup,
                                                  const WordLatticeOptions &options)
{
    const size_t n = syllables.size();
    std::vector<std::vector<LatticeEdge>> graph(n);
    const size_t max_len = static_cast<size_t>(std::max(1, options.max_phrase_syllables));
    std::unordered_map<std::string, std::vector<LatticeLexeme>> span_cache;
    for (size_t start = 0; start < n; ++start)
    {
        const size_t max_end = (std::min)(n, start + max_len);
        for (size_t end = start + 1; end <= max_end; ++end)
        {
            Segments span(syllables.begin() + static_cast<std::ptrdiff_t>(start),
                          syllables.begin() + static_cast<std::ptrdiff_t>(end));
            const std::string span_key = join_span(span);
            auto cached = span_cache.find(span_key);
            if (cached == span_cache.end())
            {
                cached = span_cache.emplace(span_key, lookup(span)).first;
            }
            const auto &rows = cached->second;
            const size_t take = (std::min)(rows.size(), static_cast<size_t>(std::max(0, options.span_limit)));
            for (size_t i = 0; i < take; ++i)
            {
                const auto &row = rows[i];
                if (row.value.empty())
                    continue;
                LatticeEdge edge;
                edge.end = end;
                edge.word = row.value;
                edge.key = row.key.empty() ? span_key : row.key;
                edge.weight = row.weight;
                edge.log_prob = edge_log_prob(edge.weight, end - start, options);
                graph[start].push_back(std::move(edge));
            }
        }
    }
    return graph;
}

size_t utf8_codepoints(const std::string &text)
{
    size_t n = 0;
    for (unsigned char c : text)
    {
        if ((c & 0xC0) != 0x80)
            ++n;
    }
    return n;
}

// The beam carries one word of history, so a third word of context cannot be searched without widening every
// hypothesis into (position, last two words) - and that multiplies the beam by the vocabulary of a column. The
// standard answer, and qingjian's, is to search with the shorter context and rescore the survivors: the paths are
// already built here, and eight transitions across six sentences is nothing next to the search that produced them.
//
// Each entry holds what the third word adds over the second, so summing it onto a path that already carries its
// bigram score is the whole model, not a second opinion competing with the first.
void rescore_with_trigram(std::vector<LatticePath> &paths, const WordLatticeOptions &options)
{
    if (!options.trigram || options.trigram_weight == 0.0 || paths.size() < 2)
        return;
    const std::string &start = NgramTable::sentence_start();
    for (auto &path : paths)
    {
        double bonus = 0;
        for (size_t i = 0; i < path.words.size(); ++i)
        {
            const std::string &before = i >= 2 ? path.words[i - 2] : start;
            const std::string &previous = i >= 1 ? path.words[i - 1] : start;
            bonus += options.trigram->bonus(before, previous, path.words[i]);
        }
        path.log_prob += options.trigram_weight * bonus;
    }
    std::stable_sort(paths.begin(), paths.end(),
                     [](const LatticePath &a, const LatticePath &b) { return a.log_prob > b.log_prob; });
}

// Best score the lattice can give one specific sentence, on the same terms it scores its own paths.
//
// The Google-Pinyin fallback and the lattice each produce a whole sentence, and they used to be ordered by
// which source they came from rather than by how good they are. Scoring the fallback over the same graph
// makes the two comparable: same edge weights, same phrase bonus, same context terms. Returns nothing when
// the dictionary cannot spell that sentence at all, which is often exactly why the fallback exists.
std::optional<double> score_known_sentence(const std::vector<std::vector<LatticeEdge>> &graph,
                                           const std::string &sentence, const WordLatticeOptions &options)
{
    if (sentence.empty() || graph.empty())
        return std::nullopt;
    // One hypothesis per (syllable, characters spelled so far). Two segmentations can reach the same
    // syllable having produced different numbers of characters, which is why the offset is part of the
    // key; in practice a column holds one or two of these, so a flat vector beats a map.
    struct Hypothesis
    {
        size_t consumed = 0;
        double score = kNegInf;
        int previous_position = -1; // column the hypothesis came from
        int previous_index = -1;    // and where in it
        const std::string *word = nullptr;
    };
    const size_t n = graph.size();
    std::vector<std::vector<Hypothesis>> columns(n + 1);
    columns[0].push_back(Hypothesis{0, 0.0, -1, -1, nullptr});
    for (size_t pos = 0; pos < n; ++pos)
    {
        for (size_t hi = 0; hi < columns[pos].size(); ++hi)
        {
            const Hypothesis &hypothesis = columns[pos][hi];
            if (hypothesis.score == kNegInf)
                continue;
            const std::string &previous = hypothesis.word == nullptr ? NgramTable::sentence_start() : *hypothesis.word;
            for (const auto &edge : graph[pos])
            {
                if (edge.word.size() > sentence.size() - hypothesis.consumed ||
                    sentence.compare(hypothesis.consumed, edge.word.size(), edge.word) != 0)
                    continue;
                double score = hypothesis.score + edge.log_prob;
                if (options.bigram)
                    score += options.bigram_weight * options.bigram->bonus(previous, edge.word);
                const size_t consumed = hypothesis.consumed + edge.word.size();
                auto &column = columns[edge.end];
                auto slot = std::find_if(column.begin(), column.end(),
                                         [&](const Hypothesis &h) { return h.consumed == consumed; });
                const Hypothesis next{consumed, score, static_cast<int>(pos), static_cast<int>(hi), &edge.word};
                if (slot == column.end())
                    column.push_back(next);
                else if (score > slot->score)
                    *slot = next;
            }
        }
    }
    const auto &final_column = columns[n];
    const auto complete = std::find_if(final_column.begin(), final_column.end(), [&](const Hypothesis &h) {
        return h.consumed == sentence.size() && h.score != kNegInf;
    });
    if (complete == final_column.end())
        return std::nullopt;
    double score = complete->score;
    if (!options.trigram || options.trigram_weight == 0.0)
        return score;
    // Only the surviving path is walked back, so the search itself never carries a word list around.
    std::vector<const std::string *> words;
    for (const Hypothesis *hypothesis = &*complete; hypothesis != nullptr && hypothesis->word != nullptr;)
    {
        words.push_back(hypothesis->word);
        if (hypothesis->previous_position < 0)
            break;
        hypothesis = &columns[static_cast<size_t>(hypothesis->previous_position)]
                             [static_cast<size_t>(hypothesis->previous_index)];
    }
    std::reverse(words.begin(), words.end());
    const std::string &start = NgramTable::sentence_start();
    double bonus = 0;
    for (size_t i = 0; i < words.size(); ++i)
    {
        const std::string &before = i >= 2 ? *words[i - 2] : start;
        const std::string &previous = i >= 1 ? *words[i - 1] : start;
        bonus += options.trigram->bonus(before, previous, *words[i]);
    }
    return score + options.trigram_weight * bonus;
}

// Characters of `text`, one entry per codepoint, so a sentence can be addressed by syllable.
std::vector<std::string> codepoints_of(const std::string &text)
{
    std::vector<std::string> out;
    for (size_t i = 0; i < text.size();)
    {
        const unsigned char lead = static_cast<unsigned char>(text[i]);
        size_t width = 1;
        if ((lead & 0xF8) == 0xF0)
            width = 4;
        else if ((lead & 0xF0) == 0xE0)
            width = 3;
        else if ((lead & 0xE0) == 0xC0)
            width = 2;
        width = (std::min)(width, text.size() - i);
        out.push_back(text.substr(i, width));
        i += width;
    }
    return out;
}

// The fallback's sentence with one of the lattice's words spliced into it, once per span the two
// disagree on, paired with how many syllables that span covers.
//
// The two sources fail differently: the fallback reads the frame of a long sentence and misses the rare
// word in it, the lattice finds the word and mangles the frame around it. The repair keeps the frame
// that is usually right and borrows only the span in dispute. The reverse direction is not offered -
// taking the lattice's frame is what lattice_outranks_fallback already decides, with its own margin.
//
// One syllable is one character here. A span where that does not hold, which is where a dictionary
// entry carries punctuation or a latin tail, is skipped rather than guessed at.
std::vector<std::pair<std::string, size_t>> span_swapped_sentences(const LatticePath &path, const std::string &fallback,
                                                                   size_t syllables)
{
    const auto fallback_chars = codepoints_of(fallback);
    const auto lattice_chars = codepoints_of(path.sentence);
    if (fallback_chars.size() != syllables || lattice_chars.size() != syllables)
        return {};
    std::vector<std::pair<std::string, size_t>> hybrids;
    size_t start = 0;
    for (const auto &word : path.words)
    {
        const size_t width = codepoints_of(word).size();
        if (width == 0 || start + width > syllables)
            break;
        std::string lattice_span;
        std::string fallback_span;
        for (size_t i = start; i < start + width; ++i)
        {
            lattice_span += lattice_chars[i];
            fallback_span += fallback_chars[i];
        }
        if (lattice_span != fallback_span)
        {
            std::string repaired;
            for (size_t i = 0; i < syllables; ++i)
                repaired += (i >= start && i < start + width) ? lattice_chars[i] : fallback_chars[i];
            hybrids.emplace_back(std::move(repaired), width);
        }
        start += width;
    }
    return hybrids;
}

bool covers_all_syllables(const WordItem &item, size_t n_syllables)
{
    if (n_syllables == 0)
        return false;
    if (utf8_codepoints(item.word) == n_syllables)
        return true;
    if (!item.canonical_pinyin.empty() && syllable_count_from_key(item.canonical_pinyin) == n_syllables)
        return true;
    return false;
}

std::vector<LatticePath> decode_from_graph(const std::vector<std::vector<LatticeEdge>> &graph,
                                           const WordLatticeOptions &options)
{
    const size_t n = graph.size();
    std::vector<std::vector<Hyp>> columns(n + 1);
    columns[0].push_back(Hyp{0.0, -1, -1, {}, {}});

    for (size_t pos = 0; pos < n; ++pos)
    {
        keep_beam(columns[pos], options.beam);
        if (columns[pos].empty())
            continue;
        for (int hi = 0; hi < static_cast<int>(columns[pos].size()); ++hi)
        {
            const Hyp &hyp = columns[pos][static_cast<size_t>(hi)];
            // Column 0 has no predecessor, so the start token carries whatever the corpus knows about how
            // sentences open; every later column uses the word the hypothesis arrived on.
            const std::string &previous = pos == 0 ? NgramTable::sentence_start() : hyp.word;
            for (const auto &edge : graph[pos])
            {
                Hyp next;
                next.score = hyp.score + edge.log_prob;
                if (options.bigram)
                    next.score += options.bigram_weight * options.bigram->bonus(previous, edge.word);
                next.prev_pos = static_cast<int>(pos);
                next.prev_idx = hi;
                next.word = edge.word;
                next.key = edge.key;
                columns[edge.end].push_back(std::move(next));
            }
        }
    }

    auto &final_col = columns[n];
    if (final_col.empty())
        return {};
    // The loop above only beam-prunes columns [0, n), so the terminal column still holds every hypothesis that reached
    // the end. Prune it too before the full sort; no other column traces back through columns[n], so truncating it
    // cannot break traceback.
    keep_beam(final_col, (std::max)(options.beam, options.nbest));
    std::sort(final_col.begin(), final_col.end(), [](const Hyp &a, const Hyp &b) { return a.score > b.score; });
    const int take = (std::min)(options.nbest, static_cast<int>(final_col.size()));

    std::vector<LatticePath> paths;
    paths.reserve(static_cast<size_t>(take));
    std::unordered_set<std::string> seen;
    for (int i = 0; i < static_cast<int>(final_col.size()) && static_cast<int>(paths.size()) < take; ++i)
    {
        LatticePath path;
        path.log_prob = final_col[static_cast<size_t>(i)].score;
        int pos = static_cast<int>(n);
        int idx = i;
        std::vector<std::string> keys;
        while (pos > 0 && idx >= 0)
        {
            const Hyp &h = columns[static_cast<size_t>(pos)][static_cast<size_t>(idx)];
            path.words.push_back(h.word);
            keys.push_back(h.key);
            pos = h.prev_pos;
            idx = h.prev_idx;
        }
        std::reverse(path.words.begin(), path.words.end());
        std::reverse(keys.begin(), keys.end());
        for (const auto &w : path.words)
            path.sentence += w;
        for (size_t k = 0; k < keys.size(); ++k)
        {
            if (k)
                path.key.push_back('\'');
            path.key += keys[k];
        }
        if (path.sentence.empty() || !seen.insert(path.sentence).second)
            continue;
        paths.push_back(std::move(path));
    }
    rescore_with_trigram(paths, options);
    return paths;
}

} // namespace

std::vector<LatticePath> decode_word_lattice(const Segments &syllables, const WordLatticeLookup &lookup,
                                             const WordLatticeOptions &options)
{
    if (syllables.empty() || !lookup)
        return {};
    return decode_from_graph(build_graph(syllables, lookup, options), options);
}

void merge_lattice_candidates(std::vector<WordItem> &candidates, const Segments &syllables,
                              const WordLatticeLookup &lookup, const std::string &typed_pinyin,
                              const WordLatticeOptions &options, const std::string &fallback_sentence,
                              WholeSentenceComparison *comparison, const std::vector<SourcedLatticeReranker> &rerankers)
{
    if (!lookup || syllables.size() < 3)
        return;
    if (!has_only_complete_pinyin_segments(syllables))
        return;

    // One graph for both: decoding the lattice and scoring the fallback have to see the same edges, and
    // building it twice would double the dictionary lookups this path is mostly made of.
    const auto graph = build_graph(syllables, lookup, options);
    auto paths = decode_from_graph(graph, options);
    if (paths.empty())
        return;
    if (comparison != nullptr)
    {
        comparison->decoded = true;
        comparison->lattice_score = paths.front().log_prob;
        comparison->syllables = syllables.size();
        // Nothing to arbitrate when the two sources already agree, and that is the common case on the
        // short compositions most keystrokes produce. Scoring it anyway spent a constrained decode per
        // keystroke to re-derive a tie.
        if (!fallback_sentence.empty() && fallback_sentence != paths.front().sentence)
        {
            comparison->fallback_score = score_known_sentence(graph, fallback_sentence, options);
            // A sentence that takes the frame from one source and the disputed span from the other can
            // beat both, and on a long input it usually is the answer: the fallback reads the frame and
            // misses the rare word, the lattice finds the word and mangles the frame.
            std::unordered_set<std::string> seen{paths.front().sentence, fallback_sentence};
            for (auto &[hybrid, span] : span_swapped_sentences(paths.front(), fallback_sentence, syllables.size()))
            {
                if (!seen.insert(hybrid).second)
                    continue;
                const auto score = score_known_sentence(graph, hybrid, options);
                if (!score.has_value())
                    continue;
                // Charged per swapped syllable rather than per sentence: a one-word repair to a long
                // sentence should not have to clear the bar a whole rewrite does.
                if (!comparison->best_hybrid_score.has_value() || *score > *comparison->best_hybrid_score)
                {
                    comparison->best_hybrid_score = score;
                    comparison->best_hybrid = hybrid;
                    comparison->best_hybrid_span = span;
                }
            }
        }
    }
    std::unordered_set<std::string> already;
    for (const auto &item : candidates)
        already.insert(item.word);

    if (!rerankers.empty())
    {
        std::optional<std::vector<LatticePath>> desktop_paths;
        std::optional<std::vector<LatticePath>> keyboard_paths;
        bool keyboard_enabled = false;
        for (const SourcedLatticeReranker &entry : rerankers)
        {
            if (entry.source == CandidateSource::NeuralKeyboard)
                keyboard_enabled = true;
            if (!entry.rerank)
                continue;
            std::vector<LatticePath> reranked = paths;
            if (!entry.rerank(reranked))
                continue;
            if (entry.source == CandidateSource::NeuralDesktop)
                desktop_paths = std::move(reranked);
            else if (entry.source == CandidateSource::NeuralKeyboard)
                keyboard_paths = std::move(reranked);
        }

        const auto first_distinct = [&](const std::vector<LatticePath> &ranked) -> const LatticePath * {
            for (const LatticePath &path : ranked)
            {
                if (already.find(path.sentence) != already.end())
                {
                    if (options.show_next_on_duplicate)
                        continue;
                    break;
                }
                return &path;
            }
            return nullptr;
        };
        const auto take = [&](const std::vector<LatticePath> &ranked, CandidateSource source) {
            const LatticePath *path = first_distinct(ranked);
            if (path == nullptr)
                return std::optional<WordItem>{};
            already.insert(path->sentence);
            WordItem item(typed_pinyin, path->sentence, static_cast<std::int64_t>(path->log_prob * 1000.0), source,
                          path->key);
            item.sentence_association = true;
            return std::optional<WordItem>(std::move(item));
        };

        std::optional<WordItem> lattice_item;
        if (options.include_lattice_best)
            lattice_item = take(paths, CandidateSource::Generated);
        std::optional<WordItem> keyboard_item;
        std::optional<WordItem> desktop_item;
        bool keyboard_before_desktop = false;
        if (keyboard_paths && desktop_paths)
        {
            const LatticePath *keyboard_best = first_distinct(*keyboard_paths);
            const LatticePath *desktop_best = first_distinct(*desktop_paths);
            keyboard_before_desktop = keyboard_best != nullptr && desktop_best != nullptr &&
                                      keyboard_best->sentence == desktop_best->sentence;
            if (keyboard_before_desktop)
            {
                keyboard_item = take(*keyboard_paths, CandidateSource::NeuralKeyboard);
                desktop_item = take(*desktop_paths, CandidateSource::NeuralDesktop);
            }
            else
            {
                desktop_item = take(*desktop_paths, CandidateSource::NeuralDesktop);
                keyboard_item = take(*keyboard_paths, CandidateSource::NeuralKeyboard);
            }
        }
        else if (keyboard_paths)
        {
            keyboard_item = take(*keyboard_paths, CandidateSource::NeuralKeyboard);
        }
        else if (desktop_paths && !keyboard_enabled)
        {
            desktop_item = take(*desktop_paths, CandidateSource::NeuralDesktop);
        }

        std::vector<WordItem> extra;
        if (lattice_item)
            extra.push_back(std::move(*lattice_item));
        if (keyboard_before_desktop)
        {
            if (keyboard_item)
                extra.push_back(std::move(*keyboard_item));
            if (desktop_item)
                extra.push_back(std::move(*desktop_item));
        }
        else
        {
            if (desktop_item)
                extra.push_back(std::move(*desktop_item));
            if (keyboard_item)
                extra.push_back(std::move(*keyboard_item));
        }
        if (extra.empty())
            return;
        const size_t insert_at = whole_sentence_insert_position(candidates, syllables.size());
        candidates.insert(candidates.begin() + static_cast<std::ptrdiff_t>(insert_at), extra.begin(), extra.end());
        return;
    }

    // Searching several paths and showing one is the point of `emit`: the alternatives exist so the trigram has
    // something to reorder, not so the candidate page fills with near-duplicate sentences.
    if (options.emit > 0 && paths.size() > static_cast<size_t>(options.emit))
        paths.resize(static_cast<size_t>(options.emit));

    std::vector<WordItem> extra;
    for (const auto &path : paths)
    {
        if (!already.insert(path.sentence).second)
            continue;
        // Often negative (log-space). Ranking is insert order, not weight.
        const auto weight = static_cast<std::int64_t>(path.log_prob * 1000.0);
        WordItem item(typed_pinyin, path.sentence, weight, CandidateSource::Generated, path.key);
        item.sentence_association = true;
        extra.push_back(std::move(item));
    }
    if (extra.empty())
        return;

    const size_t insert_at = whole_sentence_insert_position(candidates, syllables.size());
    candidates.insert(candidates.begin() + static_cast<std::ptrdiff_t>(insert_at), extra.begin(), extra.end());
}

size_t whole_sentence_insert_position(const std::vector<WordItem> &candidates, size_t n_syllables)
{
    size_t insert_at = 0;
    while (insert_at < candidates.size() && covers_all_syllables(candidates[insert_at], n_syllables) &&
           (candidates[insert_at].source == CandidateSource::Database ||
            candidates[insert_at].source == CandidateSource::UserDatabase))
        ++insert_at;
    return insert_at;
}

WordLatticeOptions make_sentence_lattice_options(const metasequoia::RuntimePaths &paths, bool alternatives)
{
    WordLatticeOptions options;
    // Six are searched and rescored either way; `emit` only decides how many of the results already
    // computed are handed back.
    options.nbest = 6;
    options.emit = alternatives ? 0 : 1;
    options.bigram = NgramTable::shared(paths.dictionary(kBigramFileName));
    options.trigram = NgramTable::shared(paths.dictionary(kTrigramFileName));
    return options;
}

} // namespace quanpin
