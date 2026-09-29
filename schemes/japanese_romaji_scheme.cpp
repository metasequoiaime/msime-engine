#include "japanese_romaji_scheme.h"
#include "input_scheme_utils.h"
#include "../common/string_utils.h"
#include "../japanese/romaji_converter.h"
#include <algorithm>
#include <cctype>

void JapaneseRomajiScheme::reset()
{
    raw_input_.clear();
    key_strokes_.clear();
}

void JapaneseRomajiScheme::handle_key(ImeKeyCode vk, ImeModifierMask modifiers_down, ImeCharacter wch)
{
    if (vk == ImeKey::Backspace)
    {
        if (!raw_input_.empty())
            raw_input_.pop_back();
        if (!key_strokes_.empty())
            key_strokes_.pop_back();
        return;
    }
    if (vk == ImeKey::Escape || vk == ImeKey::Return)
    {
        reset();
        return;
    }
    if (vk == ImeKey::Apostrophe && wch == u'\'')
    {
        raw_input_.push_back('\'');
        key_strokes_.push_back(KeyStroke{vk, modifiers_down, wch});
        return;
    }
    // The physical minus key spells the Japanese long-vowel mark. Treat it as
    // input rather than a generic symbol so native hosts can distinguish it
    // from their candidate-page shortcut.
    if (vk == ImeKey::Minus && wch == u'-')
    {
        raw_input_.push_back('-');
        key_strokes_.push_back(KeyStroke{vk, modifiers_down, wch});
        return;
    }
    if (!ImeKey::is_ascii_letter(vk))
        return;

    key_strokes_.push_back(KeyStroke{vk, modifiers_down, wch});
    if ((wch >= u'a' && wch <= u'z') || (wch >= u'A' && wch <= u'Z'))
        raw_input_.push_back(static_cast<char>(wch));
    else
        raw_input_.push_back(static_cast<char>(vk + ('a' - 'A')));
}

bool JapaneseRomajiScheme::cycle_last_kana_variant()
{
    const auto converted = japanese::ConvertRomaji(raw_input_);
    // 半截的罗马字尾巴还不是假名,没有可修改的对象。
    if (!converted.pending.empty() || converted.hiragana.empty())
        return false;

    const std::string last = CommonUtils::last_utf8_characters(converted.hiragana, 1);
    const std::string next = japanese::NextKanaVariant(last);
    if (next == last)
        return false;

    const std::string rewritten = converted.hiragana.substr(0, converted.hiragana.size() - last.size()) + next;
    const std::string romaji = japanese::HiraganaToRomaji(rewritten);
    if (romaji.empty())
        return false;
    raw_input_ = romaji;
    return true;
}

void JapaneseRomajiScheme::set_raw_input(const std::string &raw_input, const std::string &raw_input_with_cases)
{
    raw_input_ = CommonUtils::raw_input_with_cases_or_raw(raw_input, raw_input_with_cases);
    raw_input_.erase(std::remove_if(raw_input_.begin(), raw_input_.end(),
                                    [](unsigned char ch) { return !std::isalpha(ch) && ch != '\'' && ch != '-'; }),
                     raw_input_.end());
    key_strokes_.clear();
}

QueryRequest JapaneseRomajiScheme::build_request() const
{
    QueryRequest request =
        input_scheme::make_query_request(type(), CommonUtils::lowercase_ascii(raw_input_), raw_input_, key_strokes_);
    request.normalized_input = request.raw_input;
    const auto converted = japanese::ConvertRomaji(request.raw_input);
    input_scheme::set_segmentation_fields(request, request.raw_input_with_cases,
                                          converted.hiragana + converted.pending);
    request.valid = !request.raw_input.empty();
    return request;
}

std::string JapaneseRomajiScheme::get_preedit() const
{
    return raw_input_;
}

SchemeType JapaneseRomajiScheme::type() const
{
    return SchemeType::JapaneseRomaji;
}
