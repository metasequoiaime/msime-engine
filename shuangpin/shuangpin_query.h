#pragma once

#include "shuangpin_profile.h"
#include <cstddef>
#include <string>
#include <vector>

namespace shuangpin
{

std::string segment_input(const std::string &raw_input, const ShuangpinProfile &profile = GetXiaoheShuangpinProfile());
// Raw offsets where one shuangpin syllable starts, always including 0 (when
// non-empty) and raw_input.size(). A unit is one syllable: 1-2 keys decided by
// the same forward-greedy acceptance pinyin_segmentation uses, so the Microsoft
// ';' final and the profile's syllable set are honored. Manual delimiters start
// a new chunk. Empty input yields an empty vector.
std::vector<std::size_t> segment_raw_boundaries(const std::string &raw_input,
                                                const ShuangpinProfile &profile = GetXiaoheShuangpinProfile());
std::string to_quanpin_segmentation(const std::string &segmented_input,
                                    const ShuangpinProfile &profile = GetXiaoheShuangpinProfile());
std::string normalize_input(const std::string &raw_input,
                            const ShuangpinProfile &profile = GetXiaoheShuangpinProfile());
std::string normalize_input_with_delimiters(const std::string &raw_input,
                                            const ShuangpinProfile &profile = GetXiaoheShuangpinProfile());
std::string remove_manual_delimiters(const std::string &text);
size_t effective_input_length(const std::string &raw_input);
// Maps a length measured in delimiter-stripped space back onto the raw input, so callers can slice a raw prefix that
// holds exactly that many effective characters.
size_t raw_length_for_effective_prefix(const std::string &raw_input, size_t effective_length);
// Drops the trailing `letter_count` effective characters while keeping every delimiter that precedes them.
std::string trim_trailing_letters_preserve_delimiters(const std::string &raw_input, size_t letter_count);
size_t detect_active_double_helpcode_length(const std::string &raw_input, const std::string &raw_input_with_cases,
                                            const ShuangpinProfile &profile = GetXiaoheShuangpinProfile());
bool is_complete_input(const std::string &raw_input, const ShuangpinProfile &profile = GetXiaoheShuangpinProfile());
std::string apply_segmentation_cases(const std::string &segmented_input, const std::string &raw_input_with_cases);
} // namespace shuangpin
