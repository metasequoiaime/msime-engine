#include "quanpin_scheme.h"
#include "input_scheme_utils.h"
#include "../common/string_utils.h"
#include "../common/helpcode_utils.h"
#include "../quanpin/quanpin_query.h"
#include "../quanpin/quanpin_utils.h"
#include "../shuangpin/shuangpin_query.h"

void QuanpinScheme::reset()
{
    input_scheme::reset_state(raw_input_, key_strokes_);
}

void QuanpinScheme::set_raw_input(const std::string &raw_input, const std::string &raw_input_with_cases)
{
    input_scheme::set_raw_input(raw_input_, key_strokes_, raw_input, raw_input_with_cases);
}

void QuanpinScheme::handle_key(ImeKeyCode vk, ImeModifierMask modifiers_down, ImeCharacter wch)
{
    input_scheme::handle_key(vk, modifiers_down, wch, raw_input_, key_strokes_);
}

QueryRequest QuanpinScheme::build_request() const
{
    QueryRequest request =
        input_scheme::make_query_request(type(), CommonUtils::lowercase_ascii(raw_input_), raw_input_, key_strokes_);

    const size_t helpcode_length =
        quanpin::detect_active_helpcode_length(request.raw_input, request.raw_input_with_cases);
    std::string normalized_source = quanpin::strip_active_helpcodes(request.raw_input, request.raw_input_with_cases);

    request.normalized_input.reserve(normalized_source.size());
    for (const char ch : normalized_source)
    {
        if (ch != '\'')
        {
            request.normalized_input.push_back(ch);
        }
    }
    if (!request.normalized_input.empty())
    {
        try
        {
            const auto segments = quanpin::cut_pinyin_by_mode(normalized_source, "correction");
            if (!segments.empty())
            {
                request.normalized_segmentation = quanpin::join_segments(segments.front());
            }
        }
        catch (...)
        {
        }
    }

    if (request.normalized_segmentation.empty())
    {
        request.normalized_segmentation = normalized_source;
    }

    std::string cased_source = request.raw_input_with_cases;
    if (helpcode_length > 0 && cased_source.size() >= helpcode_length)
    {
        cased_source = cased_source.substr(0, cased_source.size() - helpcode_length);
    }

    request.raw_segmentation = shuangpin::apply_segmentation_cases(request.normalized_segmentation, cased_source);
    if (!cased_source.empty() && cased_source.back() == '\'' &&
        (request.raw_segmentation.empty() || request.raw_segmentation.back() != '\''))
    {
        request.raw_segmentation.push_back('\'');
    }
    if (helpcode_length > 0)
    {
        request.raw_segmentation += "'";
        request.raw_segmentation +=
            request.raw_input_with_cases.substr(request.raw_input_with_cases.size() - helpcode_length, helpcode_length);
    }
    request.segmentation = request.normalized_segmentation;

    request.valid = !request.normalized_input.empty();
    return request;
}

std::string QuanpinScheme::get_preedit() const
{
    return raw_input_;
}

SchemeType QuanpinScheme::type() const
{
    return SchemeType::Quanpin;
}
