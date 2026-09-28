#include "neural_decoder.h"

#include "sentence_model.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <memory>
#include <mutex>
#include <numeric>

namespace neural
{
namespace
{

// The lattice reports log10 (kenlm's unit); the model reports natural log. Interpolating the two
// without this would silently scale lambda by 2.3.
constexpr double kLog10ToNats = 2.302585092994046;

} // namespace

std::string last_characters(const std::string &text, std::size_t count)
{
    if (count == 0)
    {
        return {};
    }
    std::size_t seen = 0;
    std::size_t start = text.size();
    for (std::size_t i = text.size(); i-- > 0;)
    {
        if ((static_cast<unsigned char>(text[i]) & 0xC0) == 0x80)
        {
            continue; // continuation byte
        }
        start = i;
        if (++seen == count)
        {
            break;
        }
    }
    return text.substr(start);
}

const SentenceModel *shared_sentence_model(const std::string &path)
{
    static std::mutex mutex;
    // Value may be null: an entry records that loading `path` was attempted, so a missing file is
    // not re-read on every keystroke.
    static std::map<std::string, std::unique_ptr<SentenceModel>> cache;

    std::lock_guard<std::mutex> lock(mutex);
    auto it = cache.find(path);
    if (it == cache.end())
    {
        it = cache.emplace(path, SentenceModel::load_file(path)).first;
    }
    return it->second.get();
}

std::vector<std::size_t> rerank_order(const SentenceModel &model, const std::string &context,
                                      const std::vector<std::string> &sentences,
                                      const std::vector<double> &static_scores, const RerankOptions &options)
{
    if (sentences.size() < 2 || sentences.size() != static_scores.size() || options.lambda == 0.0)
    {
        return {};
    }

    const std::size_t scored_count = std::min(sentences.size(), options.max_paths);
    if (scored_count < 2)
    {
        return {};
    }

    const std::vector<std::string> texts(sentences.begin(),
                                         sentences.begin() + static_cast<std::ptrdiff_t>(scored_count));
    const std::vector<double> neural = model.score_sentences(last_characters(context, options.context_chars), texts);
    if (neural.size() != scored_count)
    {
        // The model failed or was inconsistent. Half a reranking is worse than none, so the caller
        // keeps its own order.
        return {};
    }

    std::vector<double> combined(scored_count, 0.0);
    for (std::size_t i = 0; i < scored_count; ++i)
    {
        const double statik = static_scores[i] * kLog10ToNats;
        combined[i] = statik + options.lambda * (neural[i] - statik);
    }

    std::vector<std::size_t> order(sentences.size());
    std::iota(order.begin(), order.end(), std::size_t{0});
    // Stable so that sentences the two models score identically keep the lattice's ordering, and so
    // that the unscored tail past max_paths stays as it was.
    std::stable_sort(order.begin(), order.begin() + static_cast<std::ptrdiff_t>(scored_count),
                     [&](std::size_t a, std::size_t b) { return combined[a] > combined[b]; });
    return order;
}

} // namespace neural
