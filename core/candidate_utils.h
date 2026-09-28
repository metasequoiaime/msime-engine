#pragma once

#include "word_item.h"

#include <algorithm>
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

inline void append_unique_candidates(std::vector<WordItem> &target, std::vector<WordItem> extra)
{
    std::unordered_set<std::string> seen_words;
    seen_words.reserve(target.size() + extra.size());
    for (const auto &item : target)
        seen_words.insert(item.word);
    for (auto &item : extra)
    {
        if (seen_words.insert(item.word).second)
            target.push_back(std::move(item));
    }
}
