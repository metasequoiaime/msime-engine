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
