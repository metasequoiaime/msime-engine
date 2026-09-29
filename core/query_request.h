#pragma once

#include "key_event.h"
#include "fuzzy_pinyin_options.h"
#include "scheme_type.h"
#include "sentence_association_options.h"
#include <string>
#include <vector>

struct KeyStroke
{
    ImeKeyCode vk = 0;
    ImeModifierMask modifiers_down = 0;
    ImeCharacter wch = 0;
};

struct QueryRequest
{
    SchemeType scheme = SchemeType::Quanpin;
    std::string raw_input;
    std::string raw_input_with_cases;
    std::string normalized_input;
    std::string raw_segmentation;
    std::string normalized_segmentation;
    std::string segmentation;
    bool enable_shuangpin_helpcode = false;
    bool enable_quanpin_helpcode = false;
    // Autocorrection is type-gated (bit0 transposition, bit1 neighbor in the session-level
    // mask); both default off, so a fresh install never rewrites the user's spelling.
    bool enable_quanpin_autocorrect_transposition = false;
    bool enable_quanpin_autocorrect_neighbor = false;
    std::vector<KeyStroke> key_strokes;
    metasequoia::FuzzyPinyinOptions fuzzy_pinyin;
    // Hand back every whole-sentence reading the lattice found, not only its best. A host asks for
    // this when it reorders the readings itself and crops the list before display; see
    // quanpin::make_sentence_lattice_options for why the default answers with one.
    bool sentence_alternatives = false;
    SentenceAssociationOptions sentence_association;
    std::string rescoring_context;
    // Wubi wildcard mode treats each z in the code as one arbitrary letter.
    bool wubi_z_wildcard = false;
    bool valid = false;
};

// Requests produced by schemes without case-preserving input leave this field empty. Consumers
// that need the user's original spelling use the raw input as the fallback.
inline const std::string &query_request_raw_input_with_cases(const QueryRequest &request)
{
    return request.raw_input_with_cases.empty() ? request.raw_input : request.raw_input_with_cases;
}
