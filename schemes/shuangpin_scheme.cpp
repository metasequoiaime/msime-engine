#include "shuangpin_scheme.h"
#include "../common/string_utils.h"
#include "../shuangpin/shuangpin_query.h"

namespace
{
bool is_microsoft_ing_key(ImeKeyCode vk, ImeCharacter wch, const std::string &raw_input,
                          const ShuangpinProfile &profile)
{
    if (profile.name != "microsoft" || vk != ImeKey::Semicolon || wch != u';')
    {
        return false;
    }
    const size_t separator = raw_input.find_last_of('\'');
    const size_t chunk_length = separator == std::string::npos ? raw_input.size() : raw_input.size() - separator - 1;
    return chunk_length % 2 == 1;
}
} // namespace

ShuangpinScheme::ShuangpinScheme(const ShuangpinProfile &profile) : profile_(profile)
{
}

void ShuangpinScheme::reset()
{
    raw_input_.clear();
    key_strokes_.clear();
}

void ShuangpinScheme::set_raw_input(const std::string &raw_input, const std::string &raw_input_with_cases)
{
    raw_input_ = raw_input_with_cases.empty() ? raw_input : raw_input_with_cases;
    key_strokes_.clear();
}

void ShuangpinScheme::handle_key(ImeKeyCode vk, ImeModifierMask modifiers_down, ImeCharacter wch)
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

    if (vk == ImeKey::Apostrophe)
    {
        if (raw_input_.empty() || raw_input_.back() != '\'')
        {
            key_strokes_.push_back(KeyStroke{vk, modifiers_down, wch});
            raw_input_.push_back('\'');
        }
        return;
    }

    const bool microsoft_ing_key = is_microsoft_ing_key(vk, wch, raw_input_, profile_);
    if (!ImeKey::is_ascii_letter(vk) && !microsoft_ing_key)
    {
        return;
    }

    key_strokes_.push_back(KeyStroke{vk, modifiers_down, wch});
    if (wch >= L'A' && wch <= L'Z')
    {
        raw_input_.push_back(static_cast<char>(wch));
    }
    else if (wch >= L'a' && wch <= L'z')
    {
        raw_input_.push_back(static_cast<char>(wch));
    }
    else if (microsoft_ing_key)
    {
        raw_input_.push_back(';');
    }
    else
    {
        raw_input_.push_back(static_cast<char>(vk + ('a' - 'A')));
    }
}

QueryRequest ShuangpinScheme::build_request() const
{
    QueryRequest request;
    request.scheme = type();
    request.raw_input_with_cases = raw_input_;
    request.raw_input = CommonUtils::lowercase_ascii(raw_input_);
    request.key_strokes = key_strokes_;
    request.valid = shuangpin::effective_input_length(request.raw_input) > 0;

    if (!request.valid)
    {
        return request;
    }

    const std::string raw_segmentation = shuangpin::segment_input(request.raw_input, profile_);
    request.raw_segmentation = shuangpin::apply_segmentation_cases(raw_segmentation, request.raw_input_with_cases);
    request.normalized_segmentation = shuangpin::to_quanpin_segmentation(raw_segmentation, profile_);
    request.segmentation = request.normalized_segmentation;
    request.normalized_input = shuangpin::normalize_input(request.raw_input, profile_);
    return request;
}

std::string ShuangpinScheme::get_preedit() const
{
    return raw_input_;
}

SchemeType ShuangpinScheme::type() const
{
    return SchemeType::Shuangpin;
}
