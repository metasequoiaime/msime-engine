#include "shuangpin_scheme.h"
#include "input_scheme_utils.h"
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
    input_scheme::reset_state(raw_input_, key_strokes_);
}

void ShuangpinScheme::set_raw_input(const std::string &raw_input, const std::string &raw_input_with_cases)
{
    input_scheme::set_raw_input(raw_input_, key_strokes_, raw_input, raw_input_with_cases);
}

void ShuangpinScheme::handle_key(ImeKeyCode vk, ImeModifierMask modifiers_down, ImeCharacter wch)
{
    const bool microsoft_ing_key = is_microsoft_ing_key(vk, wch, raw_input_, profile_);
    input_scheme::handle_key(vk, modifiers_down, wch, raw_input_, key_strokes_, microsoft_ing_key, ';');
}

QueryRequest ShuangpinScheme::build_request() const
{
    QueryRequest request =
        input_scheme::make_query_request(type(), CommonUtils::lowercase_ascii(raw_input_), raw_input_, key_strokes_);
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
