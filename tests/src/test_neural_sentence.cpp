#include "core/sentence_association_options.h"
#include "neural/neural_decoder.h"
#include "neural/sentence_model.h"

#include <iostream>
#include <vector>

int main(int argc, char **argv)
{
    if (neural::last_characters("a\xE4\xBD\xA0\xE5\xA5\xBD", 2) != "\xE4\xBD\xA0\xE5\xA5\xBD")
    {
        std::cerr << "last_characters must count Unicode characters\n";
        return 1;
    }
    if (!neural::last_characters("你好", 0).empty())
    {
        std::cerr << "zero context must be empty\n";
        return 1;
    }
    if (neural::rerank_context("你好世界", 64, 10) != "你好世界" || !neural::rerank_context("", 64, 10).empty())
    {
        std::cerr << "short rerank contexts must be preserved\n";
        return 1;
    }
    const std::string long_context(200, 'x');
    std::vector<std::string> windows;
    for (std::size_t longest = 1; longest <= 40; ++longest)
    {
        windows.push_back(neural::rerank_context(long_context, 64, longest));
        const std::string &window = windows.back();
        if (window.size() + longest >= 64 || window.size() % neural::kRerankContextStep != 0 ||
            long_context.compare(long_context.size() - window.size(), window.size(), window) != 0)
        {
            std::cerr << "rerank context did not hold a stable suffix\n";
            return 1;
        }
    }
    if (windows[0] != windows[1] || windows[0] == windows.back())
    {
        std::cerr << "rerank context should change only at step boundaries\n";
        return 1;
    }
    const std::string unicode_context = "a中b文";
    std::string repeated_unicode;
    for (int i = 0; i < 12; ++i)
    {
        repeated_unicode += unicode_context;
    }
    const std::string unicode_window = neural::rerank_context(repeated_unicode, 64, 20);
    const bool is_suffix = repeated_unicode.compare(repeated_unicode.size() - unicode_window.size(),
                                                    unicode_window.size(), unicode_window) == 0;
    if (unicode_window.size() != 8 * unicode_context.size() || !is_suffix ||
        !neural::rerank_context(repeated_unicode, 64, 70).empty())
    {
        std::cerr << "rerank context must preserve UTF-8 boundaries and allow an empty suffix\n";
        return 1;
    }
    if (neural::SentenceModel::load({}) != nullptr)
    {
        std::cerr << "an empty model must be rejected\n";
        return 1;
    }
    SentenceAssociationOptions defaults;
    if (!defaults.word_lattice || !defaults.google || defaults.neural_desktop || defaults.neural_keyboard)
    {
        std::cerr << "sentence association defaults changed unexpectedly\n";
        return 1;
    }
    auto neural_only = defaults;
    neural_only.word_lattice = false;
    neural_only.google = false;
    neural_only.neural_keyboard = true;
    if (neural_only == defaults || neural_only != neural_only)
    {
        std::cerr << "sentence association equality is inconsistent\n";
        return 1;
    }
    if (argc > 1)
    {
        auto model = neural::SentenceModel::load_file(argv[1]);
        if (!model || model->context_length() == 0 || model->score_sentences("", {"你好", "你号"}).size() != 2)
        {
            std::cerr << "a released model must load and score a sentence batch\n";
            return 1;
        }
    }
    std::cout << "neural sentence helpers ok\n";
    return 0;
}
