#pragma once

#include "word_item.h"

#include <algorithm>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

inline bool contains_candidate_word(const std::vector<WordItem> &candidates, std::string_view word)
{
    return std::any_of(candidates.begin(), candidates.end(),
                       [word](const WordItem &item) { return item.word == word; });
}

inline const std::string &candidate_canonical_pinyin(const WordItem &item)
{
    return item.canonical_pinyin.empty() ? item.pinyin : item.canonical_pinyin;
}

inline std::vector<CandidateSource> collect_candidate_sources(const std::vector<WordItem> &candidates)
{
    std::vector<CandidateSource> sources;
    sources.reserve(candidates.size());
    for (const auto &candidate : candidates)
        sources.push_back(candidate.source);
    return sources;
}

inline bool is_english_candidate_source(CandidateSource source)
{
    return source == CandidateSource::EnglishDictionary;
}

inline bool is_online_candidate_source(CandidateSource source)
{
    return source == CandidateSource::CloudSuggestion || source == CandidateSource::AiSuggestion;
}

inline bool is_generated_or_fallback_source(CandidateSource source)
{
    return source == CandidateSource::Generated || source == CandidateSource::Fallback;
}

inline bool is_dictionary_candidate_source(CandidateSource source)
{
    return source == CandidateSource::Database || source == CandidateSource::UserDatabase;
}

inline bool is_limited_initial_candidate(const WordItem &item, std::string_view code)
{
    return item.source == CandidateSource::Database && item.pinyin == code;
}

inline std::size_t count_limited_initial_candidates(const std::vector<WordItem> &candidates, std::string_view code)
{
    return static_cast<std::size_t>(std::count_if(candidates.begin(), candidates.end(), [code](const WordItem &item) {
        return is_limited_initial_candidate(item, code);
    }));
}

inline void sort_candidates_by_weight(std::vector<WordItem> &candidates)
{
    std::stable_sort(candidates.begin(), candidates.end(),
                     [](const WordItem &lhs, const WordItem &rhs) { return lhs.weight > rhs.weight; });
}

inline void erase_candidates_by_source(std::vector<WordItem> &candidates, CandidateSource source)
{
    candidates.erase(std::remove_if(candidates.begin(), candidates.end(),
                                    [source](const WordItem &item) { return item.source == source; }),
                     candidates.end());
}

inline void insert_unique_candidate_by_source_priority(std::vector<WordItem> &candidates, const std::string &pinyin,
                                                       const std::string &word, CandidateSource source)
{
    if (contains_candidate_word(candidates, word))
        return;
    const std::size_t index = candidates.empty()                        ? 0
                              : source == CandidateSource::AiSuggestion ? std::min<std::size_t>(2, candidates.size())
                                                                        : 1;
    candidates.insert(candidates.begin() + index, WordItem(pinyin, word, 1, source));
}

inline bool move_candidate_to_position(std::vector<WordItem> &candidates, std::string_view word, CandidateSource source,
                                       std::size_t position)
{
    const auto candidate = std::find_if(candidates.begin(), candidates.end(), [word, source](const WordItem &item) {
        return item.word == word && item.source == source;
    });
    if (candidate == candidates.end() || candidate - candidates.begin() <= static_cast<std::ptrdiff_t>(position))
        return false;
    WordItem moved = std::move(*candidate);
    candidates.erase(candidate);
    candidates.insert(candidates.begin() + std::min(position, candidates.size()), std::move(moved));
    return true;
}

inline void move_candidate_source_to_position(std::vector<WordItem> &candidates, CandidateSource source,
                                              std::size_t position)
{
    const auto candidate = std::find_if(candidates.begin(), candidates.end(),
                                        [source](const WordItem &item) { return item.source == source; });
    if (candidate == candidates.end())
        return;
    WordItem moved = std::move(*candidate);
    candidates.erase(candidate);
    candidates.insert(candidates.begin() + std::min(position, candidates.size()), std::move(moved));
}

inline void insert_cached_candidate(std::vector<WordItem> &candidates, const std::string &pinyin,
                                    const std::string &word, CandidateSource source)
{
    if (is_online_candidate_source(source))
    {
        erase_candidates_by_source(candidates, source);
    }

    insert_unique_candidate_by_source_priority(candidates, pinyin, word, source);

    if (source == CandidateSource::CloudSuggestion)
    {
        move_candidate_source_to_position(candidates, CandidateSource::AiSuggestion, 2);
    }
}

template <typename Query>
inline std::optional<std::vector<WordItem>> expand_limited_initial_candidates(std::string_view code,
                                                                              std::vector<WordItem> &candidates,
                                                                              Query query)
{
    if (code.size() != 1)
        return std::nullopt;

    const size_t limited_count = count_limited_initial_candidates(candidates, code);
    constexpr size_t kInitialCandidateLimit = 24;
    if (limited_count != kInitialCandidateLimit)
        return std::nullopt;

    auto expanded = query();
    if (expanded.size() <= limited_count)
        return std::nullopt;

    std::vector<WordItem> merged;
    merged.reserve(candidates.size() - limited_count + expanded.size());
    bool inserted = false;
    for (auto &item : candidates)
    {
        if (is_limited_initial_candidate(item, code))
        {
            if (!inserted)
            {
                merged.insert(merged.end(), expanded.begin(), expanded.end());
                inserted = true;
            }
            continue;
        }
        merged.push_back(std::move(item));
    }

    candidates = std::move(merged);
    return expanded;
}

template <typename Row>
inline std::vector<WordItem> make_database_candidates(const std::string &pinyin, const std::vector<Row> &rows)
{
    std::vector<WordItem> candidates;
    candidates.reserve(rows.size());
    for (const auto &row : rows)
    {
        candidates.emplace_back(pinyin, row.value, row.weight, CandidateSource::Database, row.key);
    }
    return candidates;
}

template <typename Row> inline std::vector<WordItem> make_database_candidates(const std::vector<Row> &rows)
{
    std::vector<WordItem> candidates;
    candidates.reserve(rows.size());
    for (const auto &row : rows)
    {
        candidates.emplace_back(row.key, row.value, row.weight, CandidateSource::Database, row.key);
    }
    return candidates;
}

inline std::vector<WordItem> take_unique_candidates(std::vector<WordItem> candidates,
                                                    std::unordered_set<std::string> &seen_words)
{
    std::vector<WordItem> unique;
    unique.reserve(candidates.size());
    for (auto &candidate : candidates)
    {
        if (seen_words.insert(candidate.word).second)
            unique.push_back(std::move(candidate));
    }
    return unique;
}

inline bool append_unique_candidate(std::vector<WordItem> &target, std::unordered_set<std::string> &seen_words,
                                    const std::string &pinyin, const std::string &word, std::int64_t weight,
                                    CandidateSource source, const std::string &canonical_pinyin)
{
    if (word.empty() || !seen_words.insert(word).second)
        return false;
    target.emplace_back(pinyin, word, weight, source, canonical_pinyin);
    return true;
}

inline bool append_unique_candidate(std::vector<WordItem> &target, std::unordered_set<std::string> &seen_words,
                                    const std::string &pinyin, const std::string &word, std::int64_t weight,
                                    CandidateSource source = CandidateSource::Database)
{
    return append_unique_candidate(target, seen_words, pinyin, word, weight, source, pinyin);
}

inline void append_unique_candidates(std::vector<WordItem> &target, std::vector<WordItem> extra)
{
    std::unordered_set<std::string> seen_words;
    seen_words.reserve(target.size() + extra.size());
    for (const auto &item : target)
        seen_words.insert(item.word);
    auto unique = take_unique_candidates(std::move(extra), seen_words);
    for (auto &item : unique)
        target.push_back(std::move(item));
}

inline void append_unique_candidates_copy(std::vector<WordItem> &target, const std::vector<WordItem> &extra)
{
    std::unordered_set<std::string> seen_words;
    seen_words.reserve(target.size() + extra.size());
    for (const auto &item : target)
        seen_words.insert(item.word);
    for (const auto &item : extra)
    {
        if (seen_words.insert(item.word).second)
            target.push_back(item);
    }
}

inline void deduplicate_candidates_by_word(std::vector<WordItem> &candidates)
{
    std::unordered_set<std::string> seen_words;
    seen_words.reserve(candidates.size());
    candidates.erase(
        std::remove_if(candidates.begin(), candidates.end(),
                       [&seen_words](const WordItem &item) { return !seen_words.insert(item.word).second; }),
        candidates.end());
}
