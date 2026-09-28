#pragma once

#include "query_request.h"
#include "word_item.h"
#include <string>
#include <vector>

struct CompositionState
{
    std::string preedit;
    QueryRequest request;
    std::vector<WordItem> candidates;
    // Set when a wubi code the table could not answer was answered by quanpin instead. These
    // candidates are pinyin words, so ranking, fixed positions and removal have to treat them as
    // pinyin rather than as the wubi code that happened to produce them.
};
