#pragma once
#include "../common/string_utils.h"
#include "../core/fuzzy_pinyin_options.h"
#include "quanpin_utils.h"
#include <algorithm>
#include <array>
#include <unordered_set>

namespace quanpin
{
inline std::vector<std::string> fuzzy_syllables(const std::string &syllable, metasequoia::FuzzyPinyinOptions options)
{
    using R = metasequoia::FuzzyPinyinRule;
    struct Pair
    {
        const char *a;
        const char *b;
        R rule;
    };
    static const std::array<Pair, 6> initials = {{{"z", "zh", R::Z_ZH},
                                                  {"c", "ch", R::C_CH},
                                                  {"s", "sh", R::S_SH},
                                                  {"n", "l", R::N_L},
                                                  {"f", "h", R::F_H},
                                                  {"r", "l", R::R_L}}};
    static const std::array<Pair, 5> finals = {{{"an", "ang", R::AN_ANG},
                                                {"en", "eng", R::EN_ENG},
                                                {"in", "ing", R::IN_ING},
                                                {"ian", "iang", R::IAN_IANG},
                                                {"uan", "uang", R::UAN_UANG}}};
    static const auto legal = [] {
        const auto &list = intact_pinyin_list();
        return std::unordered_set<std::string>(list.begin(), list.end());
    }();
    // Only complete syllables expand: incomplete prefixes retain normal completion behavior.
    if (!options.rules || !legal.count(syllable))
        return {syllable};
    std::string initial;
    if (CommonUtils::starts_with(syllable, "zh") || CommonUtils::starts_with(syllable, "ch") ||
        CommonUtils::starts_with(syllable, "sh"))
        initial = syllable.substr(0, 2);
    else if (std::string("bpmfdtnlgkhjqxrzcsyw").find(syllable[0]) != std::string::npos)
        initial = syllable.substr(0, 1);
    const std::string final = syllable.substr(initial.size());
    std::vector<std::string> starts{initial}, ends{final}, result{syllable};
    for (const auto &pair : initials)
        if (options.enabled(pair.rule))
        {
            if (initial == pair.a)
                starts.push_back(pair.b);
            else if (initial == pair.b)
                starts.push_back(pair.a);
        }
    for (const auto &pair : finals)
        if (options.enabled(pair.rule))
        {
            if (final == pair.a)
                ends.push_back(pair.b);
            else if (final == pair.b)
                ends.push_back(pair.a);
        }
    for (const auto &start : starts)
        for (const auto &end : ends)
        {
            const auto candidate = start + end;
            if (legal.count(candidate) && std::find(result.begin(), result.end(), candidate) == result.end())
                result.push_back(candidate);
        }
    return result;
}

// A bounded beam prevents exponential queries for phrases with many enabled rules.
inline std::vector<Segments> fuzzy_segmentations(const Segments &segments, metasequoia::FuzzyPinyinOptions options,
                                                 std::size_t limit = 64)
{
    if (!options.rules || segments.empty() || limit == 0)
        return {};
    std::vector<Segments> paths{{}};
    for (const auto &syllable : segments)
    {
        const auto alternatives = fuzzy_syllables(syllable, options);
        std::vector<Segments> next;
        for (const auto &path : paths)
            for (const auto &alternative : alternatives)
            {
                if (next.size() == limit)
                    break;
                auto copy = path;
                copy.push_back(alternative);
                next.push_back(std::move(copy));
            }
        paths = std::move(next);
    }
    paths.erase(std::remove(paths.begin(), paths.end(), segments), paths.end());
    return paths;
}
} // namespace quanpin
