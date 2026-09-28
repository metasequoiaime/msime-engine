#pragma once

#include "../core/word_item.h"

#include <optional>
#include <string>
#include <vector>

namespace metasequoia::local_modes
{
struct LocalQueryResult
{
    std::vector<WordItem> candidates;
    std::optional<std::string> diagnostic;
};

inline LocalQueryResult database_query_failure(const char *diagnostic)
{
    return {{}, std::string(diagnostic)};
}
} // namespace metasequoia::local_modes
