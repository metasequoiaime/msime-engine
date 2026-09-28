// Per-keystroke latency of the engine against the real dictionaries.
//
// Not a test: it asserts nothing and is not registered with ctest. Run it by
// hand to see what one key costs under each sentence-association setting, e.g.
//   set METASEQUOIA_IME_DATA_DIR=<a copy of the data directory>
//   bench_input_session [repeats]
// It only types and resets; it never selects a candidate, so it does not write
// to the user journal. Still, point it at a copy of the data directory.
#include "core/input_session.h"
#include "core/sentence_association_options.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace
{
struct Setting
{
    const char *name;
    SentenceAssociationOptions options;
};

std::vector<Setting> settings()
{
    SentenceAssociationOptions off;
    SentenceAssociationOptions lattice = off;
    lattice.word_lattice = true;
    SentenceAssociationOptions lattice_google = lattice;
    lattice_google.google = true;
    SentenceAssociationOptions neural_keyboard = lattice_google;
    neural_keyboard.neural_keyboard = true;
    SentenceAssociationOptions neural_desktop = lattice_google;
    neural_desktop.neural_desktop = true;
    SentenceAssociationOptions all = lattice_google;
    all.neural_keyboard = true;
    all.neural_desktop = true;
    return {{"off", off},
            {"lattice", lattice},
            {"lattice+google", lattice_google},
            {"+neural_keyboard", neural_keyboard},
            {"+neural_desktop", neural_desktop},
            {"all", all}};
}

// Short words, common phrases and a sentence: the cost grows with the input.
const std::vector<std::string> kInputs = {
    "nihao", "zhongguo", "shuru", "womenyiqiqu", "jintiantianqibucuo", "zhegewentiyinggaizenmejiejue",
};

double percentile(std::vector<double> values, double p)
{
    if (values.empty())
        return 0.0;
    std::sort(values.begin(), values.end());
    const size_t index = (std::min)(values.size() - 1, static_cast<size_t>(p * (values.size() - 1) + 0.5));
    return values[index];
}
} // namespace

int main(int argc, char **argv)
{
    const int repeats = argc > 1 ? (std::max)(1, std::atoi(argv[1])) : 5;
    metasequoia::InputSession session(SchemeType::Quanpin);

    std::printf("%-18s %10s %10s %10s %10s   (ms per key, %d repeats)\n", "setting", "median", "p90", "max",
                "per-input", repeats);
    for (const Setting &setting : settings())
    {
        session.set_sentence_association(setting.options);
        std::vector<double> per_key;
        std::vector<double> per_input;
        // One warm-up pass so model loading and first-touch page faults do not
        // count against the setting.
        for (int round = -1; round < repeats; ++round)
        {
            for (const std::string &input : kInputs)
            {
                // Clear the candidate caches: real typing mostly queries prefixes the
                // engine has not seen yet, and a cached repeat would hide that cost.
                session.reset_state();
                session.reset_cache();
                double total = 0.0;
                for (const char character : input)
                {
                    const auto start = std::chrono::steady_clock::now();
                    session.handle_character(character);
                    const double ms =
                        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
                    total += ms;
                    if (round >= 0)
                        per_key.push_back(ms);
                }
                if (round >= 0)
                    per_input.push_back(total);
            }
        }
        std::printf("%-18s %10.2f %10.2f %10.2f %10.2f\n", setting.name, percentile(per_key, 0.5),
                    percentile(per_key, 0.9), percentile(per_key, 1.0), percentile(per_input, 0.5));
    }
    session.reset_state();
    return 0;
}
