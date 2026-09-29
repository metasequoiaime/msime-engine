#pragma once

#include "../core/query_request.h"

#include <string>
#include <vector>

namespace input_scheme
{
inline QueryRequest make_query_request(SchemeType scheme, const std::string &raw_input,
                                       const std::string &raw_input_with_cases,
                                       const std::vector<KeyStroke> &key_strokes)
{
    QueryRequest request;
    request.scheme = scheme;
    request.raw_input = raw_input;
    request.raw_input_with_cases = raw_input_with_cases;
    request.key_strokes = key_strokes;
    return request;
}

inline void reset_state(std::string &raw_input, std::vector<KeyStroke> &key_strokes)
{
    raw_input.clear();
    key_strokes.clear();
}

inline void set_raw_input(std::string &target, std::vector<KeyStroke> &key_strokes, const std::string &raw_input,
                          const std::string &raw_input_with_cases)
{
    target = CommonUtils::raw_input_with_cases_or_raw(raw_input, raw_input_with_cases);
    key_strokes.clear();
}

inline void set_segmentation_fields(QueryRequest &request, const std::string &raw_segmentation,
                                    const std::string &normalized_segmentation)
{
    request.raw_segmentation = raw_segmentation;
    request.normalized_segmentation = normalized_segmentation;
    request.segmentation = request.normalized_segmentation;
}

inline void handle_key(ImeKeyCode vk, ImeModifierMask modifiers_down, ImeCharacter wch, std::string &raw_input,
                       std::vector<KeyStroke> &key_strokes, bool accept_special_key = false, char special_key = '\0')
{
    if (vk == ImeKey::Backspace)
    {
        if (!raw_input.empty())
            raw_input.pop_back();
        if (!key_strokes.empty())
            key_strokes.pop_back();
        return;
    }

    if (vk == ImeKey::Escape || vk == ImeKey::Return)
    {
        reset_state(raw_input, key_strokes);
        return;
    }

    if (vk == ImeKey::Apostrophe)
    {
        if (raw_input.empty() || raw_input.back() != '\'')
        {
            key_strokes.push_back(KeyStroke{vk, modifiers_down, wch});
            raw_input.push_back('\'');
        }
        return;
    }

    if (!ImeKey::is_ascii_letter(vk) && !accept_special_key)
        return;

    key_strokes.push_back(KeyStroke{vk, modifiers_down, wch});
    if (wch >= L'A' && wch <= L'Z')
        raw_input.push_back(static_cast<char>(wch));
    else if (wch >= L'a' && wch <= L'z')
        raw_input.push_back(static_cast<char>(wch));
    else if (accept_special_key)
        raw_input.push_back(special_key);
    else
        raw_input.push_back(static_cast<char>(vk + ('a' - 'A')));
}
} // namespace input_scheme
