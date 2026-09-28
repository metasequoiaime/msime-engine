#include "input_session.h"
#include "../common/string_utils.h"
#include "../shuangpin/shuangpin_query.h"
#include "../shuangpin/shuangpin_utils.h"
#include <algorithm>

namespace metasequoia
{
std::string InputSession::editing_text() const
{
    if (dedicated_english_mode_)
        return dedicated_english_preedit_;
    if (local_input_mode_ == LocalInputMode::TemporaryJapanese)
        return "R" + engine_.get_request().raw_input_with_cases;
    if (local_input_mode_ != LocalInputMode::None)
        return local_preedit_;
    const auto &value = engine_.get_request();
    return value.raw_input_with_cases.empty() ? value.raw_input : value.raw_input_with_cases;
}

std::size_t InputSession::caret_position() const
{
    const auto size = editing_text().size();
    return std::min(caret_.value_or(size), size);
}

namespace
{
// Unit boundaries of a quanpin spelling in raw coordinates. The visible preedit
// (BuildQuanpinAutocorrectDisplay) is always rebuilt from the raw letters -- the
// autocorrect cut and the alias layer only move or add separators -- so a
// separator in the display marks where the next raw unit starts. A display that
// cannot explain the raw letters yields no boundaries and the caller falls back
// to single-character editing.
std::vector<std::size_t> QuanpinRawBoundaries(const std::string &raw, const std::string &display)
{
    std::vector<std::size_t> boundaries;
    std::vector<std::size_t> raw_letter_offsets;
    raw_letter_offsets.reserve(raw.size());
    for (std::size_t index = 0; index < raw.size(); ++index)
    {
        if (raw[index] != '\'')
        {
            raw_letter_offsets.push_back(index);
        }
    }
    if (raw_letter_offsets.empty())
    {
        return boundaries;
    }

    boundaries.push_back(0);
    std::size_t letters_seen = 0;
    for (const char ch : display)
    {
        if (ch != '\'')
        {
            ++letters_seen;
            continue;
        }
        // The next unit starts at the raw offset of the next letter; a
        // trailing separator has no next letter and starts no unit.
        if (letters_seen < raw_letter_offsets.size())
        {
            boundaries.push_back(raw_letter_offsets[letters_seen]);
        }
    }
    if (letters_seen != raw_letter_offsets.size())
    {
        // The display no longer corresponds letter-for-letter: refuse to map
        // rather than delete an arbitrary span.
        return {};
    }
    boundaries.push_back(raw.size());
    boundaries.erase(std::unique(boundaries.begin(), boundaries.end()), boundaries.end());
    return boundaries;
}
} // namespace

std::vector<std::size_t> InputSession::segment_raw_boundaries() const
{
    // Local modes and the dedicated English scheme spell words, not syllables:
    // the host treats these keys exactly like a plain Backspace (PRD R4).
    if (dedicated_english_mode_ || local_input_mode_ != LocalInputMode::None)
    {
        return {};
    }

    const std::string raw_with_cases = get_pinyin_sequence_with_cases();
    const std::string raw = get_pinyin_sequence();
    if (raw_with_cases.empty())
    {
        return {};
    }

    if (current_scheme_type() == SchemeType::Shuangpin)
    {
        const std::size_t helpcode_length =
            shuangpin::detect_active_double_helpcode_length(raw, raw_with_cases, shuangpin_profile_);
        const std::string base =
            helpcode_length > 0 ? shuangpin::trim_trailing_letters_preserve_delimiters(raw_with_cases, helpcode_length)
                                : raw_with_cases;
        std::vector<std::size_t> boundaries = shuangpin::segment_raw_boundaries(base, shuangpin_profile_);
        if (helpcode_length > 0 && !boundaries.empty())
        {
            // The active double helpcode is one editable unit of its own, the
            // same boundary raw_segmentation draws before it.
            boundaries.push_back(base.size());
            boundaries.push_back(raw_with_cases.size());
            std::sort(boundaries.begin(), boundaries.end());
            boundaries.erase(std::unique(boundaries.begin(), boundaries.end()), boundaries.end());
        }
        return boundaries;
    }

    if (current_scheme_type() != SchemeType::Quanpin)
    {
        return {};
    }
    return QuanpinRawBoundaries(raw_with_cases, get_pinyin_segmentation_with_cases());
}

KeyResult InputSession::edit_at_caret(Command command)
{
    auto text = editing_text();
    auto caret = caret_position();
    // Local-mode markers are commands, not editable payload. Backspace at the
    // end of a marker-only composition retains the existing cancel behavior.
    const std::size_t begin = local_input_mode_ == LocalInputMode::None ? 0 : 1;
    switch (command)
    {
    case Command::MoveLeft:
        caret = caret > begin ? caret - 1 : begin;
        break;
    case Command::MoveRight:
        caret = std::min(caret + 1, text.size());
        break;
    case Command::MoveHome:
        caret = begin;
        break;
    case Command::MoveEnd:
        caret = text.size();
        break;
    case Command::Backspace:
        if (caret <= begin)
            return {true, std::nullopt, std::nullopt};
        text.erase(--caret, 1);
        return replace_editing_text(std::move(text), caret);
    case Command::DeleteForward:
        if (caret == text.size())
            return {true, std::nullopt, std::nullopt};
        text.erase(caret, 1);
        return replace_editing_text(std::move(text), caret);
    default:
        return {};
    }
    caret_ = caret;
    update_mixed_candidates();
    return {true, std::nullopt, std::nullopt};
}

KeyResult InputSession::insert_at_caret(char character)
{
    auto text = editing_text();
    const auto caret = caret_position();
    const auto unsigned_character = static_cast<unsigned char>(character);
    const bool lower = CommonUtils::is_ascii_lowercase(unsigned_character);
    const bool upper = CommonUtils::is_ascii_uppercase(unsigned_character);
    bool accepted = lower || upper;
    if (!dedicated_english_mode_)
    {
        switch (local_input_mode_)
        {
        case LocalInputMode::Unicode:
            accepted = CommonUtils::is_ascii_hex_digit(unsigned_character) ||
                       (character == '+' && caret == 1 && text.find('+') == std::string::npos);
            break;
        case LocalInputMode::QuickPhrase:
            accepted = lower;
            break;
        case LocalInputMode::DateTime:
            accepted = false;
            break;
        case LocalInputMode::None:
            accepted = lower || (upper && ((scheme() == SchemeType::Quanpin && quanpin_helpcode_enabled_) ||
                                           (scheme() == SchemeType::Shuangpin && shuangpin_helpcode_enabled_)));
            if (character == ';' && scheme() == SchemeType::Shuangpin && shuangpin_profile_.name == "microsoft")
            {
                const auto separator = caret == 0 ? std::string::npos : text.rfind('\'', caret - 1);
                const auto start = separator == std::string::npos ? 0 : separator + 1;
                accepted = (caret - start) % 2 == 1;
            }
            if (character == '\'' && scheme() != SchemeType::Wubi)
                accepted = caret > 0;
            break;
        case LocalInputMode::Emoji:
        case LocalInputMode::Kaomoji:
        case LocalInputMode::TemporaryJapanese:
            accepted = accepted || character == '\'';
            break;
        default:
            break;
        }
    }
    if (!accepted)
        return {};
    if (character == '\'' && ((caret > 0 && text[caret - 1] == '\'') || text[caret] == '\''))
        return {true, std::nullopt, std::nullopt};
    text.insert(caret, 1, character);
    return replace_editing_text(std::move(text), caret + 1);
}

KeyResult InputSession::replace_editing_text(std::string text, std::size_t caret)
{
    std::optional<std::string> diagnostic;
    if (dedicated_english_mode_)
    {
        dedicated_english_preedit_ = std::move(text);
        update_dedicated_english_candidates();
    }
    else if (local_input_mode_ != LocalInputMode::None && local_input_mode_ != LocalInputMode::TemporaryJapanese)
    {
        local_preedit_ = std::move(text);
        diagnostic = update_local_candidates();
    }
    else
    {
        const auto payload = local_input_mode_ == LocalInputMode::TemporaryJapanese ? text.substr(1) : text;
        auto normalized = CommonUtils::lowercase_ascii(payload);
        set_pinyin_sequence(normalized);
        set_pinyin_sequence_with_cases(payload);
        apply_pending_sequence();
        if (local_input_mode_ == LocalInputMode::TemporaryJapanese)
        {
            local_preedit_ = "R" + engine_.get_preedit();
            local_candidates_ = engine_.get_candidates();
        }
    }
    caret_ = caret;
    update_mixed_candidates();
    online_requests_.invalidate();
    discard_abandoned_phrase_progress();
    return {true, std::nullopt, std::move(diagnostic)};
}
} // namespace metasequoia
