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
