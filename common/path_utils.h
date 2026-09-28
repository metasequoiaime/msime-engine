#pragma once

#include <algorithm>
#include <filesystem>

namespace CommonUtils
{
inline bool paths_overlap(const std::filesystem::path &first, const std::filesystem::path &second)
{
    const auto common = std::mismatch(first.begin(), first.end(), second.begin(), second.end());
    return common.first == first.end() || common.second == second.end();
}
} // namespace CommonUtils
