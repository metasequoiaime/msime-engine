#pragma once

#include <string>
#include <cstdint>
#include <utility>

enum class CandidateSource
{
    Database,
    UserDatabase,
    CloudSuggestion,
    AiSuggestion,
    EnglishDictionary,
    QuickPhrase,
    Emoji,
    Kaomoji,
    Generated,
    Fallback,
};

struct WordItem
{
    // The input code matched by this candidate.  This remains scheme/raw-input
    // oriented because composition advancement must consume exactly what the
    // user typed (including abbreviated pinyin).
    std::string pinyin;
    // The complete quanpin key read from the database.  It is deliberately
    // separate from pinyin: abbreviated quanpin and shuangpin candidates use
    // their typed code for advancement, but phrase creation must persist a
    // complete, unambiguous pronunciation.
    std::string canonical_pinyin;
    std::string word;
    std::int64_t weight = 0;
    CandidateSource source = CandidateSource::Database;
    int fixed_position = 0;
    bool fuzzy = false; // Matched typed code may differ from canonical pronunciation.
    // 非空表示该候选来自纠错解释（scheme 别名层或纠错表改写了输入字母），值为纠错前的
    // 原始输入字母串（对标 librime tips / 搜狗纠错标记）。填充规则见
    // QuanpinDictionary::mark_autocorrect_candidates；前端仅据非空与否附加轻标记。
    std::string corrected_from;
    // Only whole-sentence association rows set this flag. Generated and
    // fallback candidates are also used for raw passthrough, local modes and
    // other synthesized values, so consumers must not infer sentence labels
    // from CandidateSource alone.
    bool sentence_association = false;

    WordItem() = default;
    WordItem(std::string pinyin_value, std::string word_value, std::int64_t weight_value,
             CandidateSource source_value = CandidateSource::Database, std::string canonical_pinyin_value = {})
        : pinyin(std::move(pinyin_value)), canonical_pinyin(std::move(canonical_pinyin_value)),
          word(std::move(word_value)), weight(weight_value), source(source_value)
    {
    }
};
