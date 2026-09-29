#pragma once

#include <string>
#include <utility>

// 整句联想的开关。现有词格和 Google 来源默认保持启用；神经来源由宿主显式开启。
//
// 整句候选最多四行：词格首选、Google 解码器，以及两个神经模型的首选各一条。
//
// 前两个各是一条独立来源的整句。
//
// 后两个不一样。神经模型不再自己造句，它只在词格内部解出的最多 12 条 n-best 里挑一条（见
// engine/neural/neural_decoder.h）。它们不依赖 word_lattice 的显示开关：只开神经时词格仍会在内部
// 生成备选，但只显示神经选中的句子；同时开 word_lattice 时才额外保留词格首选作对照。两个神经
// 开关都开时各自独立打分，各出一条首选；两边选中相同文本时合并成一条。
struct SentenceAssociationOptions
{
    bool word_lattice = true;            // kenlm 词格整句，CandidateSource::Generated
    bool google = true;                  // Google 解码器整句，CandidateSource::Fallback
    bool neural_desktop = false;         // 用高精度档的神经模型重排词格整句
    bool neural_keyboard = false;        // 用快速档的神经模型重排词格整句
    bool show_next_on_duplicate = false; // 某来源首选被去重时，显示该来源下一条不同结果

    bool neural_reranking_enabled(bool desktop_model_available, bool keyboard_model_available) const
    {
        return (neural_desktop && desktop_model_available) || (neural_keyboard && keyboard_model_available);
    }

    bool operator==(const SentenceAssociationOptions &other) const
    {
        return word_lattice == other.word_lattice && google == other.google && neural_desktop == other.neural_desktop &&
               neural_keyboard == other.neural_keyboard && show_next_on_duplicate == other.show_next_on_duplicate;
    }
    bool operator!=(const SentenceAssociationOptions &other) const
    {
        return !(*this == other);
    }

    bool update_if_changed(const SentenceAssociationOptions &other)
    {
        if (*this == other)
            return false;
        *this = other;
        return true;
    }
};

template <typename ClearCaches>
bool update_sentence_alternatives(bool &stored_value, bool enabled, ClearCaches &&clear_caches)
{
    if (stored_value == enabled)
        return false;
    stored_value = enabled;
    std::forward<ClearCaches>(clear_caches)();
    return true;
}

template <typename ResetCache>
bool update_sentence_association_options(SentenceAssociationOptions &stored_options,
                                         const SentenceAssociationOptions &options, ResetCache &&reset_cache)
{
    if (!stored_options.update_if_changed(options))
        return false;
    std::forward<ResetCache>(reset_cache)();
    return true;
}

template <typename ResetCache>
bool update_rescoring_context(std::string &stored_context, const std::string &context,
                              const SentenceAssociationOptions &options, bool desktop_model_available,
                              bool keyboard_model_available, ResetCache &&reset_cache)
{
    if (stored_context == context)
        return false;
    stored_context = context;
    if (options.neural_reranking_enabled(desktop_model_available, keyboard_model_available))
        std::forward<ResetCache>(reset_cache)();
    return true;
}
