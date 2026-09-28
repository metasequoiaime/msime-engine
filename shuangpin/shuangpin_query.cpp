#include "shuangpin_query.h"

#include "../common/string_utils.h"
#include "../common/helpcode_utils.h"
#include "shuangpin_utils.h"
#include <algorithm>

namespace shuangpin
{

namespace
{
std::string segment_chunk(const std::string &chunk, const ShuangpinProfile &profile)
{
    return chunk.empty() ? std::string{} : ShuangpinUtil::pinyin_segmentation(chunk, profile);
}
} // namespace

std::string segment_input(const std::string &raw_input, const ShuangpinProfile &profile)
{
    if (raw_input.empty())
    {
        return {};
    }

    std::string result;
    size_t segment_start = 0;
    bool first_segment = true;
    while (segment_start <= raw_input.size())
    {
        const size_t separator = raw_input.find('\'', segment_start);
        const std::string chunk = separator == std::string::npos
                                      ? raw_input.substr(segment_start)
                                      : raw_input.substr(segment_start, separator - segment_start);
        if (!chunk.empty())
        {
            if (!first_segment)
            {
                result.push_back('\'');
            }
            result += segment_chunk(chunk, profile);
            first_segment = false;
        }

        if (separator == std::string::npos)
        {
            break;
        }
        segment_start = separator + 1;
    }

    return result;
}

std::vector<std::size_t> segment_raw_boundaries(const std::string &raw_input, const ShuangpinProfile &profile)
{
    std::vector<std::size_t> boundaries;
    if (raw_input.empty())
    {
        return boundaries;
    }

    boundaries.push_back(0);
    std::size_t chunk_start = 0;
    while (chunk_start <= raw_input.size())
    {
        const std::size_t separator = raw_input.find('\'', chunk_start);
        const std::size_t chunk_end = separator == std::string::npos ? raw_input.size() : separator;
        std::size_t position = chunk_start;
        while (position < chunk_end)
        {
            // Same forward-greedy rule as pinyin_segmentation: take two keys
            // when they form an accepted syllable, otherwise one.
            const bool two_key = (chunk_end - position) >= 2 &&
                                 ShuangpinUtil::is_accepted_syllable_code(
                                     CommonUtils::lowercase_ascii(raw_input.substr(position, 2)), profile);
            position += two_key ? 2u : 1u;
            boundaries.push_back(position);
        }

        if (separator == std::string::npos)
        {
            break;
        }
        chunk_start = separator + 1;
        if (chunk_start < raw_input.size())
        {
            boundaries.push_back(chunk_start);
        }
    }

    // A trailing delimiter starts no unit, but the end of the spelling always
    // belongs to the last unit; collapse any duplicate a delimiter next to a
    // chunk start produced.
    if (boundaries.back() != raw_input.size())
    {
        boundaries.push_back(raw_input.size());
    }
    boundaries.erase(std::unique(boundaries.begin(), boundaries.end()), boundaries.end());
    return boundaries;
}

std::string to_quanpin_segmentation(const std::string &segmented_input, const ShuangpinProfile &profile)
{
    return ShuangpinUtil::convert_seg_shuangpin_to_seg_complete_pinyin(segmented_input, profile);
}

std::string normalize_input_with_delimiters(const std::string &raw_input, const ShuangpinProfile &profile)
{
    return to_quanpin_segmentation(segment_input(raw_input, profile), profile);
}

std::string remove_manual_delimiters(const std::string &text)
{
    return CommonUtils::remove_apostrophe_delimiters(text);
}

std::string normalize_input(const std::string &raw_input, const ShuangpinProfile &profile)
{
    return remove_manual_delimiters(normalize_input_with_delimiters(raw_input, profile));
}

size_t effective_input_length(const std::string &raw_input)
{
    return remove_manual_delimiters(raw_input).size();
}

size_t raw_length_for_effective_prefix(const std::string &raw_input, size_t effective_length)
{
    size_t raw_length = 0;
    size_t effective_count = 0;
    while (raw_length < raw_input.size() && effective_count < effective_length)
    {
        if (raw_input[raw_length] != '\'')
        {
            ++effective_count;
        }
        ++raw_length;
    }
    return raw_length;
}

std::string trim_trailing_letters_preserve_delimiters(const std::string &raw_input, size_t letter_count)
{
    if (letter_count == 0 || raw_input.empty())
    {
        return raw_input;
    }

    size_t remaining = letter_count;
    size_t pos = raw_input.size();
    while (pos > 0)
    {
        --pos;
        if (raw_input[pos] == '\'')
        {
            continue;
        }

        --remaining;
        if (remaining == 0)
        {
            return raw_input.substr(0, pos);
        }
    }

    return {};
}

size_t detect_active_double_helpcode_length(const std::string &raw_input, const std::string &raw_input_with_cases,
                                            const ShuangpinProfile &profile)
{
    const std::string effective_input = remove_manual_delimiters(raw_input);
    const std::string effective_input_with_cases =
        remove_manual_delimiters(raw_input_with_cases.empty() ? raw_input : raw_input_with_cases);
    if (!ShuangpinUtil::IsFullHelpMode(effective_input_with_cases, profile))
    {
        return 0;
    }

    const size_t base_length = effective_input.size() - 2;
    const size_t raw_base_length = raw_length_for_effective_prefix(raw_input, base_length);
    if (raw_base_length < raw_input.size() && raw_input[raw_base_length] == '\'')
    {
        return 0;
    }

    return is_complete_input(raw_input.substr(0, raw_base_length), profile) ? 2 : 0;
}

bool is_complete_input(const std::string &raw_input, const ShuangpinProfile &profile)
{
    if (raw_input.empty() || raw_input.front() == '\'' || raw_input.back() == '\'' ||
        raw_input.find("''") != std::string::npos)
    {
        return false;
    }

    size_t segment_start = 0;
    while (segment_start <= raw_input.size())
    {
        const size_t separator = raw_input.find('\'', segment_start);
        const std::string chunk = separator == std::string::npos
                                      ? raw_input.substr(segment_start)
                                      : raw_input.substr(segment_start, separator - segment_start);
        if (chunk.empty() ||
            !ShuangpinUtil::is_all_complete_pinyin(chunk, ShuangpinUtil::pinyin_segmentation(chunk, profile)))
        {
            return false;
        }
        if (separator == std::string::npos)
        {
            break;
        }
        segment_start = separator + 1;
    }
    return true;
}

std::string apply_segmentation_cases(const std::string &segmented_input, const std::string &raw_input_with_cases)
{
    if (segmented_input.empty() || raw_input_with_cases.empty())
    {
        return {};
    }

    std::string extracted_input;
    extracted_input.reserve(segmented_input.size());
    for (const char ch : segmented_input)
    {
        if (ch != '\'')
        {
            extracted_input.push_back(ch);
        }
    }

    if (extracted_input != CommonUtils::lowercase_ascii(remove_manual_delimiters(raw_input_with_cases)))
    {
        return segmented_input;
    }

    std::string result;
    result.reserve(segmented_input.size());
    size_t index = 0;
    for (const char ch : segmented_input)
    {
        if (ch == '\'')
        {
            result.push_back(ch);
            continue;
        }

        if (index >= raw_input_with_cases.size())
        {
            return segmented_input;
        }

        while (index < raw_input_with_cases.size() && raw_input_with_cases[index] == '\'')
        {
            ++index;
        }

        if (index >= raw_input_with_cases.size())
        {
            return segmented_input;
        }

        const char cased = raw_input_with_cases[index];
        if (ch == cased || ch == cased + ('a' - 'A'))
        {
            result.push_back(cased);
        }
        else
        {
            result.push_back(ch);
        }
        ++index;
    }

    return result;
}

std::string get_first_han_char(const std::string &words)
{
    return HelpcodeUtils::get_first_han_char(words);
}

std::string get_last_han_char(const std::string &words)
{
    return HelpcodeUtils::get_last_han_char(words);
}

std::string::size_type count_utf8_chars(const std::string &text)
{
    return CommonUtils::count_utf8_chars(text);
}

std::string::size_type count_han_chars(const std::string &text)
{
    return HelpcodeUtils::count_han_chars(text);
}

} // namespace shuangpin
