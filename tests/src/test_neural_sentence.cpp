#include "core/sentence_association_options.h"
#include "neural/neural_decoder.h"
#include "neural/sentence_model.h"

#include <iostream>

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
