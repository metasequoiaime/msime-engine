#pragma once

// Bridges the word lattice's n-best paths to the neural sentence model.
//
// word_lattice.* knows nothing about the model and engine/neural knows nothing about lattices; this
// is the one place that speaks both. Quanpin and shuangpin share it because their sentence pipelines
// are the same lattice over the same syllables.

#include "../neural/neural_decoder.h"
#include "word_lattice.h"

#include <string>

namespace quanpin
{

// A reranker for merge_lattice_candidates that reorders paths by the neural model, or an empty
// std::function when `model` is null (the model file is absent, or the feature is switched off).
// An empty reranker is not an error: the lattice's own order stands.
//
// `context` is the text already committed before the composition, used as the model's conditioning
// prefix. Empty is fine and simply scores each sentence on its own. Pass the whole history: it is
// trimmed here to the last `options.context_chars` characters, which is what actually gets scored.
LatticeReranker make_neural_reranker(const neural::SentenceModel *model, const std::string &context,
                                     const neural::RerankOptions &options = {});

} // namespace quanpin
