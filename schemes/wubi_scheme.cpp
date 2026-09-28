#include "wubi_scheme.h"
#include "../common/string_utils.h"
#include <algorithm>

namespace
{
bool is_wubi_letter(char lower, bool mixed_pinyin_allowed, bool z_wildcard)
{
    return (lower >= 'a' && lower <= 'y') || (lower == 'z' && (mixed_pinyin_allowed || z_wildcard));
}

bool is_wubi_vk(ImeKeyCode vk, bool mixed_pinyin_allowed, bool z_wildcard)
{
    return vk >= 'A' && vk <= 'Z' &&
           is_wubi_letter(static_cast<char>(vk + ('a' - 'A')), mixed_pinyin_allowed, z_wildcard);
}

std::string normalize_wubi_code(const std::string &input, size_t max_length, bool mixed_pinyin_allowed, bool z_wildcard)
{
    std::string normalized;
    normalized.reserve((std::min)(input.size(), max_length));
    for (const unsigned char ch : input)
    {
        const char lower = CommonUtils::lowercase_ascii_char(ch);
        if (!is_wubi_letter(lower, mixed_pinyin_allowed, z_wildcard))
        {
            continue;
        }
        normalized.push_back(lower);
        if (normalized.size() == max_length)
        {
            break;
        }
    }
    return normalized;
}
} // namespace

void WubiScheme::reset()
{
    raw_input_.clear();
    key_strokes_.clear();
    extended_length_allowed_ = false;
}

void WubiScheme::handle_key(ImeKeyCode vk, ImeModifierMask modifiers_down, ImeCharacter wch)
{
    if (vk == ImeKey::Backspace)
    {
        if (!raw_input_.empty())
        {
            raw_input_.pop_back();
        }
        if (!key_strokes_.empty())
        {
            key_strokes_.pop_back();
        }
        return;
    }

    if (vk == ImeKey::Escape || vk == ImeKey::Return)
    {
        reset();
        return;
    }

    if (!is_wubi_vk(vk, mixed_pinyin_allowed_, z_wildcard_) || raw_input_.size() >= max_code_length())
    {
        return;
    }

    key_strokes_.push_back(KeyStroke{vk, modifiers_down, wch});
    raw_input_.push_back(static_cast<char>(vk + ('a' - 'A')));
}

void WubiScheme::set_raw_input(const std::string &raw_input, const std::string &raw_input_with_cases)
{
    raw_input_ = normalize_wubi_code(raw_input_with_cases.empty() ? raw_input : raw_input_with_cases, max_code_length(),
                                     mixed_pinyin_allowed_, z_wildcard_);
    key_strokes_.clear();
}

void WubiScheme::set_extended_length_allowed(bool allowed)
{
    extended_length_allowed_ = allowed;
}

void WubiScheme::set_mixed_pinyin_allowed(bool allowed)
{
    mixed_pinyin_allowed_ = allowed;
}

void WubiScheme::set_z_wildcard(bool enabled)
{
    z_wildcard_ = enabled;
}

size_t WubiScheme::max_code_length() const
{
    return extended_length_allowed_ ? kMaxMixedCodeLength : kMaxCodeLength;
}

QueryRequest WubiScheme::build_request() const
{
    QueryRequest request;
    request.scheme = type();
    request.raw_input = raw_input_;
    request.raw_input_with_cases = raw_input_;
    request.normalized_input = raw_input_;
    request.raw_segmentation = raw_input_;
    request.normalized_segmentation = raw_input_;
    request.segmentation = raw_input_;
    request.key_strokes = key_strokes_;
    request.wubi_z_wildcard = z_wildcard_ && raw_input_.find('z') != std::string::npos;
    request.valid = !raw_input_.empty();
    return request;
}

std::string WubiScheme::get_preedit() const
{
    return raw_input_;
}

SchemeType WubiScheme::type() const
{
    return SchemeType::Wubi;
}
