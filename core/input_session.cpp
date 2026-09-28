#include "online_candidate_batch.h"
#include "input_session.h"

#include "../common/helpcode_utils.h"
#include "../common/string_utils.h"
#include "../local_modes/date_time_query.h"
#include "../local_modes/emoji_query.h"
#include "../local_modes/jianpin_query.h"
#include "../local_modes/kaomoji_query.h"
#include "../local_modes/quick_phrase_query.h"
#include "../local_modes/unicode_query.h"
#include "../quanpin/quanpin_query.h"
#include "../quanpin/quanpin_utils.h"
#include "../shuangpin/shuangpin_query.h"
#include "../user_dictionary/user_dictionary_journal.h"
#include "data_path.h"

#include <algorithm>
#include <iterator>
#include <unordered_set>
#include <utility>

namespace metasequoia
{
namespace
{
const char *frequency_mode_name(FrequencyAdjustmentMode mode)
{
    switch (mode)
    {
    case FrequencyAdjustmentMode::Disabled:
        return "disabled";
    case FrequencyAdjustmentMode::Pin:
        return "pin";
    case FrequencyAdjustmentMode::Halve:
        return "halve";
    case FrequencyAdjustmentMode::Linear:
        return "linear";
    case FrequencyAdjustmentMode::Promote:
        return "promote";
    }
    return nullptr;
}

std::string online_identity(const QueryRequest &request)
{
    const std::string &input = request.raw_input_with_cases.empty() ? request.raw_input : request.raw_input_with_cases;
    return std::to_string(static_cast<int>(request.scheme)) + ":" + input;
}

} // namespace

InputSession::InputSession(SchemeType scheme_type, unsigned quanpin_autocorrect_types, bool helpcode_enabled,
                           bool chinese_punctuation_enabled, bool candidate_learning_enabled, RuntimePaths paths)
    : paths_(std::move(paths)), candidate_queries_(paths_, GetXiaoheShuangpinProfile()),
      engine_(scheme_type, GetXiaoheShuangpinProfile(), paths_), quanpin_autocorrect_types_(quanpin_autocorrect_types),
      quanpin_helpcode_enabled_(helpcode_enabled), shuangpin_helpcode_enabled_(helpcode_enabled),
      chinese_punctuation_enabled_(chinese_punctuation_enabled),
      candidate_learning_enabled_(candidate_learning_enabled), shuangpin_profile_(GetXiaoheShuangpinProfile())
{
    engine_.set_quanpin_autocorrect_types(quanpin_autocorrect_types_);
    engine_.set_quanpin_helpcode_enabled(quanpin_helpcode_enabled_);
    engine_.set_shuangpin_helpcode_enabled(shuangpin_helpcode_enabled_);
}

InputSession::InputSession(SchemeType scheme_type, const ShuangpinProfile &shuangpin_profile, RuntimePaths paths)
    : paths_(std::move(paths)), candidate_queries_(paths_, shuangpin_profile),
      engine_(scheme_type, shuangpin_profile, paths_), shuangpin_profile_(shuangpin_profile)
{
    engine_.set_quanpin_autocorrect_types(quanpin_autocorrect_types_);
    engine_.set_quanpin_helpcode_enabled(quanpin_helpcode_enabled_);
    engine_.set_shuangpin_helpcode_enabled(shuangpin_helpcode_enabled_);
}

KeyResult InputSession::handle_character(char character, bool shift_only)
{
    if (caret_position() < editing_text().size())
        return insert_at_caret(character);
    caret_.reset();
    if (dedicated_english_mode_)
    {
        const bool ascii_letter = CommonUtils::is_ascii_letter(static_cast<unsigned char>(character));
        if (!ascii_letter)
        {
            return {true, std::nullopt, std::nullopt};
        }
        dedicated_english_preedit_.push_back(character);
        update_dedicated_english_candidates();
        return {true, std::nullopt, std::nullopt};
    }
    if (local_input_mode_ != LocalInputMode::None)
    {
        return handle_local_character(character);
    }

    if (shift_only && character == 'U' && local_mode_options_.unicode && !has_composition() &&
        (scheme() == SchemeType::Quanpin || scheme() == SchemeType::Shuangpin))
    {
        local_input_mode_ = LocalInputMode::Unicode;
        local_preedit_ = "U";
        local_candidates_.clear();
        return {true, std::nullopt, std::nullopt};
    }
    if (shift_only && character == 'T' && local_mode_options_.date_time && !has_composition() &&
        (scheme() == SchemeType::Quanpin || scheme() == SchemeType::Shuangpin))
    {
        local_input_mode_ = LocalInputMode::DateTime;
        local_preedit_ = "T";
        local_candidates_.clear();
        return {true, std::nullopt, std::nullopt};
    }
    if (shift_only && character == 'K' && local_mode_options_.quick_phrase && !has_composition() &&
        (scheme() == SchemeType::Quanpin || scheme() == SchemeType::Shuangpin))
    {
        local_input_mode_ = LocalInputMode::QuickPhrase;
        local_preedit_ = "K";
        local_candidates_.clear();
        return {true, std::nullopt, std::nullopt};
    }
    if (shift_only && character == 'E' && local_mode_options_.emoji && !has_composition() &&
        (scheme() == SchemeType::Quanpin || scheme() == SchemeType::Shuangpin))
    {
        local_input_mode_ = LocalInputMode::Emoji;
        local_preedit_ = "E";
        local_candidates_.clear();
        return {true, std::nullopt, std::nullopt};
    }
    if (shift_only && character == 'M' && local_mode_options_.kaomoji && !has_composition() &&
        (scheme() == SchemeType::Quanpin || scheme() == SchemeType::Shuangpin))
    {
        local_input_mode_ = LocalInputMode::Kaomoji;
        local_preedit_ = "M";
        local_candidates_.clear();
        return {true, std::nullopt, std::nullopt};
    }
    if (shift_only && character == 'J' && local_mode_options_.super_jianpin && !has_composition() &&
        (scheme() == SchemeType::Quanpin || scheme() == SchemeType::Shuangpin))
    {
        local_input_mode_ = LocalInputMode::SuperJianpin;
        local_preedit_ = "J";
        local_candidates_.clear();
        return {true, std::nullopt, std::nullopt};
    }
    if (shift_only && character == 'Y' && local_mode_options_.temporary_english && !has_composition() &&
        (scheme() == SchemeType::Quanpin || scheme() == SchemeType::Shuangpin))
    {
        local_input_mode_ = LocalInputMode::TemporaryEnglish;
        local_preedit_ = "Y";
        local_candidates_.clear();
        return {true, std::nullopt, std::nullopt};
    }
    if (shift_only && character == 'R' && local_mode_options_.temporary_japanese && !has_composition() &&
        (scheme() == SchemeType::Quanpin || scheme() == SchemeType::Shuangpin))
    {
        temporary_original_scheme_ = scheme();
        engine_.switch_scheme(SchemeType::JapaneseRomaji);
        local_input_mode_ = LocalInputMode::TemporaryJapanese;
        local_preedit_ = "R";
        local_candidates_.clear();
        return {true, std::nullopt, std::nullopt};
    }

    const bool lowercase_letter = CommonUtils::is_ascii_lowercase(static_cast<unsigned char>(character));
    const bool microsoft_final =
        character == ';' && scheme() == SchemeType::Shuangpin && shuangpin_profile_.name == "microsoft";
    // 長音符。The romaji table has carried {"-", "ー"} all along and nothing could reach it: the
    // gate below never let '-' through. Every word Japanese borrowed from another language needs
    // it, so the keyboard had to commit the composition and drop a bare ー beside it instead.
    const bool japanese_long_vowel = character == '-' && scheme() == SchemeType::JapaneseRomaji;
    const bool active_helpcode = CommonUtils::is_ascii_uppercase(static_cast<unsigned char>(character)) &&
                                 has_composition() &&
                                 ((scheme() == SchemeType::Quanpin && quanpin_helpcode_enabled_) ||
                                  (scheme() == SchemeType::Shuangpin && shuangpin_helpcode_enabled_));
    if (!lowercase_letter && !active_helpcode && character != '\'' && !microsoft_final && !japanese_long_vowel)
    {
        return {};
    }
    if (character == '\'' && !has_composition())
    {
        return {};
    }

    const std::string previous_preedit = preedit();
    const auto unsigned_character = static_cast<unsigned char>(character);
    const ImeKeyCode key_code =
        character == '\''
            ? ImeKey::Apostrophe
            : (microsoft_final
                   ? ImeKey::Semicolon
                   : (japanese_long_vowel ? ImeKey::Minus : static_cast<ImeKeyCode>(std::toupper(unsigned_character))));
    engine_.handle_key(key_code, 0, static_cast<ImeCharacter>(unsigned_character));
    update_mixed_candidates();
    const bool handled = preedit() != previous_preedit;
    if (handled)
    {
        online_requests_.invalidate();
    }
    return {handled, std::nullopt, std::nullopt};
}

KeyResult InputSession::handle_candidate_key(char character)
{
    if (!has_composition() || character < '1' || character > '9')
    {
        return {};
    }
    return select_candidate(static_cast<std::size_t>(character - '1'));
}

KeyResult InputSession::handle_punctuation(char character)
{
    if (!chinese_punctuation_enabled_ || punctuation_lock_ == 2)
    {
        return {};
    }

    const auto punctuation = punctuation_.translate(character);
    if (!punctuation)
        return {};

    KeyResult result = finish_composition();
    result.handled = true;
    std::string text = result.commit.value_or("");
    text += punctuation;
    result.commit = std::move(text);
    return result;
}

KeyResult InputSession::handle_command(Command command)
{
    if (!has_composition())
    {
        return {};
    }

    switch (command)
    {
    case Command::MoveLeft:
    case Command::MoveRight:
    case Command::MoveHome:
    case Command::MoveEnd:
    case Command::DeleteForward:
        return edit_at_caret(command);
    case Command::CycleKanaVariant:
        if (!engine_.cycle_japanese_kana_variant())
            return {};
        update_mixed_candidates();
        return {true, std::nullopt, std::nullopt};
    case Command::Backspace:
        if (caret_position() < editing_text().size())
            return edit_at_caret(command);
        caret_.reset();
        if (dedicated_english_mode_)
        {
            dedicated_english_preedit_.pop_back();
            update_dedicated_english_candidates();
            return {true, std::nullopt, std::nullopt};
        }
        if (local_input_mode_ != LocalInputMode::None)
        {
            std::optional<std::string> diagnostic;
            if (local_preedit_.size() <= 1)
            {
                reset_composition();
            }
            else if (local_input_mode_ == LocalInputMode::TemporaryJapanese)
            {
                engine_.handle_key(ImeKey::Backspace);
                local_preedit_ = "R" + engine_.get_preedit();
                local_candidates_ = engine_.get_candidates();
            }
            else
            {
                local_preedit_.pop_back();
                diagnostic = update_local_candidates();
            }
            return {true, std::nullopt, std::move(diagnostic)};
        }
        engine_.handle_key(ImeKey::Backspace);
        update_mixed_candidates();
        discard_abandoned_phrase_progress();
        online_requests_.invalidate();
        return {true, std::nullopt, std::nullopt};
    case Command::CommitCandidate:
        return commit(0);
    case Command::CommitRaw: {
        std::string raw = preedit();
        if ((local_input_mode_ == LocalInputMode::TemporaryEnglish ||
             local_input_mode_ == LocalInputMode::TemporaryJapanese) &&
            !raw.empty())
        {
            raw.erase(raw.begin());
        }
        std::optional<std::string> diagnostic;
        if (dedicated_english_mode_ &&
            !user_dictionary::learn_entered_english_word(path_to_utf8(paths_.dictionary(assets::english_dictionary)),
                                                         path_to_utf8(paths_.user(assets::user_journal)), raw))
        {
            diagnostic = "English word could not be learned.";
        }
        reset_composition();
        return {true, std::move(raw), std::move(diagnostic)};
    }
    case Command::CommitReading: {
        // 只有日语有"读み"这个概念:其余方案的 normalized_segmentation 是拼音,不是要上屏的字。
        if (scheme() != SchemeType::JapaneseRomaji || !has_composition())
            return {};
        std::string reading = normalized_segmentation();
        if (reading.empty())
            return {};
        reset_composition();
        return {true, std::move(reading), std::nullopt};
    }
    case Command::Cancel:
        reset_composition();
        return {true, std::nullopt, std::nullopt};
    }
    return {};
}

KeyResult InputSession::select_candidate(std::size_t index)
{
    if (!has_composition() || index >= candidates().size())
    {
        return {};
    }
    return commit(index);
}

KeyResult InputSession::finish_composition(std::size_t first_index)
{
    KeyResult result;
    while (has_composition())
    {
        auto part = commit(first_index);
        first_index = 0;
        result.handled = true;
        if (part.commit)
        {
            if (!result.commit)
            {
                result.commit = std::string{};
            }
            *result.commit += *part.commit;
        }
        if (part.diagnostic)
        {
            result.diagnostic = std::move(part.diagnostic);
        }
    }
    return result;
}

KeyResult InputSession::select_candidate(const std::string &candidate)
{
    const auto found = std::find_if(candidates().begin(), candidates().end(),
                                    [&](const WordItem &item) { return item.word == candidate; });
    if (found == candidates().end())
    {
        return {};
    }
    return commit(static_cast<std::size_t>(std::distance(candidates().begin(), found)));
}

KeyResult InputSession::select_candidate_edge(std::size_t index, CandidateEdge edge)
{
    if (!has_composition() || index >= candidates().size())
    {
        return {};
    }

    const std::string &candidate = candidates()[index].word;
    std::string character = edge == CandidateEdge::FirstHan ? HelpcodeUtils::get_first_han_char(candidate)
                                                            : HelpcodeUtils::get_last_han_char(candidate);
    if (character.empty())
    {
        return {};
    }

    reset_composition();
    return {true, std::move(character), std::nullopt};
}

void InputSession::set_shuangpin_helpcode_enabled(bool enabled)
{
    if (shuangpin_helpcode_enabled_ == enabled)
    {
        return;
    }
    shuangpin_helpcode_enabled_ = enabled;
    engine_.set_shuangpin_helpcode_enabled(enabled);
    // The engine reads the flag only while querying, so a live composition has to be asked again instead of re-wrapped.
    recompute_candidates();
    online_requests_.invalidate();
}

void InputSession::set_quanpin_helpcode_enabled(bool enabled)
{
    if (quanpin_helpcode_enabled_ == enabled)
    {
        return;
    }
    quanpin_helpcode_enabled_ = enabled;
    engine_.set_quanpin_helpcode_enabled(enabled);
    // Same as the shuangpin toggle: the flag alone does not refresh a composition that is already on screen.
    recompute_candidates();
    online_requests_.invalidate();
}

bool InputSession::is_supported_helpcode_schema(const std::string &schema)
{
    return HelpcodeUtils::is_supported_helpcode_schema(schema);
}

bool InputSession::set_helpcode_schema(const std::string &schema)
{
    if (!HelpcodeUtils::is_supported_helpcode_schema(schema))
        return false;
    helpcode_keymap_ = HelpcodeUtils::load_helpcode_keymap(paths_.resources, schema);
    engine_.set_helpcode_keymap(helpcode_keymap_);
    update_mixed_candidates();
    return true;
}

bool InputSession::select_helpcode_schema(const std::string &schema)
{
    return HelpcodeUtils::select_helpcode_schema(schema);
}

bool InputSession::set_frequency_adjustment(FrequencyAdjustmentOptions options)
{
    if (frequency_mode_name(options.mode) == nullptr || options.trigger_count < 1 || options.trigger_count > 10 ||
        options.linear_step < 1 || options.linear_step > 10)
    {
        return false;
    }
    frequency_adjustment_ = options;
    frequency_adjustment_configured_ = true;
    return true;
}

const FrequencyAdjustmentOptions &InputSession::frequency_adjustment() const
{
    return frequency_adjustment_;
}

void InputSession::set_local_mode_options(LocalModeOptions options)
{
    local_mode_options_ = options;
    if ((local_input_mode_ == LocalInputMode::Unicode && !local_mode_options_.unicode) ||
        (local_input_mode_ == LocalInputMode::DateTime && !local_mode_options_.date_time) ||
        (local_input_mode_ == LocalInputMode::QuickPhrase && !local_mode_options_.quick_phrase) ||
        (local_input_mode_ == LocalInputMode::Emoji && !local_mode_options_.emoji) ||
        (local_input_mode_ == LocalInputMode::Kaomoji && !local_mode_options_.kaomoji) ||
        (local_input_mode_ == LocalInputMode::SuperJianpin && !local_mode_options_.super_jianpin) ||
        (local_input_mode_ == LocalInputMode::TemporaryEnglish && !local_mode_options_.temporary_english) ||
        (local_input_mode_ == LocalInputMode::TemporaryJapanese && !local_mode_options_.temporary_japanese))
    {
        reset_composition();
    }
}

const LocalModeOptions &InputSession::local_mode_options() const
{
    return local_mode_options_;
}

bool InputSession::set_english_input_options(EnglishInputOptions options)
{
    if (options.minimum_prefix < 1 || options.minimum_prefix > 8)
    {
        return false;
    }
    english_input_options_ = options;
    update_mixed_candidates();
    return true;
}

const EnglishInputOptions &InputSession::english_input_options() const
{
    return english_input_options_;
}

void InputSession::set_mixed_expressive_options(MixedExpressiveOptions options)
{
    mixed_expressive_options_ = options;
    update_mixed_candidates();
}

void InputSession::set_wubi_input_options(metasequoia::WubiInputOptions options)
{
    const auto &current = engine_.wubi_input_options();
    if (current.mixed_pinyin == options.mixed_pinyin && current.z_wildcard == options.z_wildcard)
        return;
    engine_.set_wubi_input_options(options);
    // The setting decides which dictionary answers the code in hand, so a live composition has to be
    // asked again. Leaving it alone shows the previous answer: the fallback candidates stay on screen
    // after the setting is switched off, and switching it on leaves an unmatched code empty until the
    // next keystroke.
    if (is_wubi() && !dedicated_english_mode_ && local_input_mode_ == LocalInputMode::None)
    {
        recompute_candidates();
    }
}

const metasequoia::WubiInputOptions &InputSession::wubi_input_options() const
{
    return engine_.wubi_input_options();
}

const MixedExpressiveOptions &InputSession::mixed_expressive_options() const
{
    return mixed_expressive_options_;
}

void InputSession::set_dedicated_english_mode(bool enabled)
{
    if (dedicated_english_mode_ == enabled)
    {
        return;
    }
    reset_composition();
    dedicated_english_mode_ = enabled;
}

bool InputSession::dedicated_english_mode() const
{
    return dedicated_english_mode_;
}

LocalInputMode InputSession::local_input_mode() const
{
    return local_input_mode_;
}

void InputSession::set_local_date_time_provider(std::function<local_modes::LocalDateTime()> provider)
{
    local_date_time_provider_ = std::move(provider);
}

std::optional<OnlineQuery> InputSession::online_query() const
{
    if (dedicated_english_mode_ || local_input_mode_ != LocalInputMode::None || !has_composition())
    {
        return std::nullopt;
    }

    const QueryRequest &request = engine_.get_request();
    OnlineQuery query;
    query.scheme = request.scheme;
    online_requests_.stamp(query);
    query.identity = online_identity(request);

    const auto state = get_cloud_query_state();
    if (!state.should_query)
        return std::nullopt;
    query.query_text = state.query_text;
    query.cache_key = state.cache_key;
    if (request.scheme == SchemeType::JapaneseRomaji)
    {
        query.cloud_eligible = true;
        return query;
    }

    if (query.query_text.empty())
    {
        return std::nullopt;
    }
    query.pinyin_segments = quanpin::split_segments(
        request.normalized_segmentation.empty() ? query.query_text : request.normalized_segmentation);
    query.cloud_eligible = true;
    query.ai_eligible =
        !query.pinyin_segments.empty() &&
        std::all_of(query.pinyin_segments.begin(), query.pinyin_segments.end(),
                    [](const std::string &segment) { return quanpin::is_complete_pinyin_input(segment); });
    return query;
}

bool InputSession::apply_online_candidate(const OnlineQuery &query, std::string candidate, CandidateSource source)
{
    if (candidate.empty() || (source != CandidateSource::CloudSuggestion && source != CandidateSource::AiSuggestion))
    {
        return false;
    }
    const auto current = online_query();
    if (!current.has_value() || !online_requests_.matches(*current, query) ||
        (source == CandidateSource::CloudSuggestion && !current->cloud_eligible) ||
        (source == CandidateSource::AiSuggestion && !current->ai_eligible) ||
        std::any_of(candidates().begin(), candidates().end(),
                    [&](const WordItem &item) { return item.word == candidate; }))
    {
        return false;
    }
    if (engine_.apply_dynamic_candidate(candidate, source) != 0)
    {
        return false;
    }
    update_mixed_candidates();
    return std::any_of(candidates().begin(), candidates().end(),
                       [&](const WordItem &item) { return item.word == candidate && item.source == source; });
}

void InputSession::switch_scheme(SchemeType scheme_type)
{
    reset_composition();
    engine_.switch_scheme(scheme_type);
    update_mixed_candidates();
}

SchemeType InputSession::scheme() const
{
    return temporary_original_scheme_.value_or(engine_.current_scheme_type());
}

bool InputSession::has_composition() const
{
    if (dedicated_english_mode_)
    {
        return !dedicated_english_preedit_.empty();
    }
    return !preedit().empty();
}

const std::string &InputSession::preedit() const
{
    if (dedicated_english_mode_)
    {
        return dedicated_english_preedit_;
    }
    if (local_input_mode_ != LocalInputMode::None)
    {
        return local_preedit_;
    }
    return engine_.get_preedit();
}

const std::string &InputSession::raw_segmentation() const
{
    if (dedicated_english_mode_)
    {
        return dedicated_english_preedit_;
    }
    if (local_input_mode_ != LocalInputMode::None)
    {
        return local_preedit_;
    }
    return engine_.get_request().raw_segmentation;
}

const std::string &InputSession::normalized_segmentation() const
{
    if (dedicated_english_mode_)
    {
        return dedicated_english_preedit_;
    }
    if (local_input_mode_ != LocalInputMode::None)
    {
        return local_preedit_;
    }
    return engine_.get_request().normalized_segmentation;
}

const std::vector<WordItem> &InputSession::candidates() const
{
    if (dedicated_english_mode_)
    {
        return dedicated_english_candidates_;
    }
    if (local_input_mode_ != LocalInputMode::None)
    {
        return local_candidates_;
    }
    if (fixed_positions_enabled_ ||
        ((english_input_options_.mixed_candidates || mixed_expressive_options_.emoji_candidates ||
          mixed_expressive_options_.kaomoji_candidates) &&
         (scheme() == SchemeType::Quanpin || scheme() == SchemeType::Shuangpin)))
    {
        return mixed_candidates_;
    }
    if (prefix_candidates_active_)
    {
        return prefix_candidates_;
    }
    return engine_.get_candidates();
}

std::vector<std::string> InputSession::candidate_annotations() const
{
    const auto &items = candidates();
    std::vector<std::string> result;
    result.reserve(items.size());
    const bool enabled = helpcode_enabled();
    const bool uppercase_all = scheme() == SchemeType::Quanpin;
    const auto *keymap = helpcode_keymap_ ? helpcode_keymap_.get() : nullptr;
    for (const auto &item : items)
    {
        std::string annotation;
        if (enabled && item.source != CandidateSource::EnglishDictionary)
            annotation = HelpcodeUtils::compute_helpcodes(item.word, uppercase_all, keymap);
        if (annotation.empty())
            annotation = item.corrected_from;
        result.push_back(std::move(annotation));
    }
    return result;
}

SchemeType InputSession::scheme_type() const
{
    return scheme();
}

unsigned InputSession::quanpin_autocorrect_types() const
{
    return quanpin_autocorrect_types_;
}

bool InputSession::helpcode_enabled() const
{
    if (scheme() == SchemeType::Quanpin)
    {
        return quanpin_helpcode_enabled_;
    }
    if (scheme() == SchemeType::Shuangpin)
    {
        return shuangpin_helpcode_enabled_;
    }
    return false;
}

bool InputSession::chinese_punctuation_enabled() const
{
    return chinese_punctuation_enabled_;
}

bool InputSession::candidate_learning_enabled() const
{
    return candidate_learning_enabled_;
}

KeyResult InputSession::commit(std::size_t index)
{
    caret_.reset();
    std::optional<std::string> text;
    std::optional<WordItem> selected;
    if (index < candidates().size())
    {
        selected = candidates()[index];
        text = selected->word;
    }
    else if (!((local_input_mode_ == LocalInputMode::TemporaryEnglish ||
                local_input_mode_ == LocalInputMode::TemporaryJapanese) &&
               local_preedit_.size() == 1))
    {
        text = preedit();
    }
    std::optional<std::string> diagnostic = learn_candidate(index);
    const bool has_dictionary_reading = selected && (selected->source == CandidateSource::Database ||
                                                     selected->source == CandidateSource::UserDatabase);
    // Whole-sentence candidates produced by the lattice (and Google fallback
    // candidates) are valid pinyin selections when they carry a canonical
    // reading.  They must participate in the same creating-word completion
    // path as dictionary rows so a preceding selected segment can be learned.
    const bool has_generated_reading =
        selected && (selected->source == CandidateSource::Generated || selected->source == CandidateSource::Fallback) &&
        !selected->canonical_pinyin.empty();
    if ((has_dictionary_reading || has_generated_reading) && local_input_mode_ == LocalInputMode::None &&
        !dedicated_english_mode_ && (candidates_follow_pinyin() || selected->scheme == SchemeType::Quanpin))
    {
        const auto transition = advance_composition_after_selection(selected->pinyin, selected->word,
                                                                    selected->canonical_pinyin, selected->scheme);
        auto progress = update_creating_word_progress(immediate_phrase_progress_.pinyin,
                                                      immediate_phrase_progress_.word, selected->word, transition);
        if (transition.continues_composition)
        {
            immediate_phrase_progress_ = std::move(progress);
            discard_abandoned_phrase_progress();
            return {true, std::move(text), std::move(diagnostic)};
        }
        if (!immediate_phrase_progress_.word.empty() && progress.can_store && candidate_learning_enabled_ &&
            store_user_phrase_from_canonical_pinyin(progress.pinyin, progress.word) != 0)
        {
            diagnostic = "Unable to persist the composed phrase.";
        }
    }
    reset_composition();
    return {true, std::move(text), std::move(diagnostic)};
}

KeyResult InputSession::handle_local_character(char character)
{
    if (local_input_mode_ == LocalInputMode::TemporaryEnglish)
    {
        const bool ascii_letter = CommonUtils::is_ascii_letter(static_cast<unsigned char>(character));
        if (!ascii_letter)
        {
            return {};
        }
        local_preedit_.push_back(character);
        return {true, std::nullopt, update_local_candidates()};
    }
    if (local_input_mode_ == LocalInputMode::TemporaryJapanese)
    {
        const auto unsigned_character = static_cast<unsigned char>(character);
        if (!std::isalpha(unsigned_character) && character != '\'')
        {
            return {};
        }
        const ImeKeyCode key_code =
            character == '\'' ? ImeKey::Apostrophe : static_cast<ImeKeyCode>(std::toupper(unsigned_character));
        engine_.handle_key(key_code, 0, static_cast<ImeCharacter>(unsigned_character));
        local_preedit_ = "R" + engine_.get_preedit();
        local_candidates_ = engine_.get_candidates();
        return {true, std::nullopt, std::nullopt};
    }
    if (local_input_mode_ == LocalInputMode::SuperJianpin)
    {
        const bool ascii_letter = CommonUtils::is_ascii_letter(static_cast<unsigned char>(character));
        if (!ascii_letter)
        {
            return {true, std::nullopt, std::nullopt};
        }
        local_preedit_.push_back(character);
        return {true, std::nullopt, update_local_candidates()};
    }
    if (local_input_mode_ == LocalInputMode::Emoji || local_input_mode_ == LocalInputMode::Kaomoji)
    {
        const bool ascii_letter = CommonUtils::is_ascii_letter(static_cast<unsigned char>(character));
        if (!ascii_letter && character != '\'')
        {
            return {true, std::nullopt, std::nullopt};
        }
        local_preedit_.push_back(character);
        return {true, std::nullopt, update_local_candidates()};
    }
    if (local_input_mode_ == LocalInputMode::QuickPhrase)
    {
        if (!CommonUtils::is_ascii_lowercase(static_cast<unsigned char>(character)))
        {
            return {true, std::nullopt, std::nullopt};
        }
        local_preedit_.push_back(character);
        return {true, std::nullopt, update_local_candidates()};
    }
    if (local_input_mode_ == LocalInputMode::DateTime)
    {
        if (!CommonUtils::is_ascii_lowercase(static_cast<unsigned char>(character)))
        {
            return {true, std::nullopt, std::nullopt};
        }
        local_preedit_.push_back(character);
        return {true, std::nullopt, update_local_candidates()};
    }
    if (local_input_mode_ != LocalInputMode::Unicode)
    {
        return {};
    }

    const auto unsigned_character = static_cast<unsigned char>(character);
    const bool optional_plus = character == '+' && local_preedit_ == "U";
    if (!optional_plus && std::isxdigit(unsigned_character) == 0)
    {
        return {true, std::nullopt, std::nullopt};
    }
    local_preedit_.push_back(character);
    return {true, std::nullopt, update_local_candidates()};
}

std::optional<std::string> InputSession::update_local_candidates()
{
    auto result = candidate_queries_.local(local_input_mode_, local_preedit_, scheme(), local_date_time_provider_,
                                           engine_.get_candidates());
    local_candidates_ = std::move(result.candidates);
    apply_candidate_positions(local_candidates_);
    return std::move(result.diagnostic);
}

void InputSession::update_mixed_candidates()
{
    refresh_prefix_candidates();
    const auto &decoded = prefix_candidates_active_ ? prefix_candidates_ : engine_.get_candidates();
    const std::string association_input =
        prefix_candidates_active_ ? prefix_query_input_ : engine_.get_request().raw_input;
    mixed_candidates_ = candidate_queries_.mixed(decoded, association_input, scheme(), english_input_options_,
                                                 mixed_expressive_options_, dedicated_english_mode_, local_input_mode_);
    apply_candidate_positions(mixed_candidates_);
}

void InputSession::set_caret(std::optional<std::size_t> caret)
{
    if (caret.has_value())
    {
        *caret = std::min(*caret, editing_text().size());
    }
    caret_ = caret;
}

std::size_t InputSession::quantized_prefix_end() const
{
    const std::string &raw = get_pinyin_sequence_with_cases();
    if (!caret_.has_value())
    {
        return raw.size();
    }
    const std::vector<std::size_t> boundaries = segment_raw_boundaries();
    if (boundaries.empty())
    {
        return raw.size();
    }
    return *std::prev(std::upper_bound(boundaries.begin(), boundaries.end(), caret_position()));
}

std::size_t InputSession::prefix_end() const
{
    return quantized_prefix_end();
}

std::string InputSession::pending_suffix() const
{
    const std::string &raw = get_pinyin_sequence_with_cases();
    const std::size_t end = quantized_prefix_end();
    return end < raw.size() ? raw.substr(end) : std::string{};
}

void InputSession::refresh_prefix_candidates()
{
    prefix_candidates_active_ = false;
    if (!caret_.has_value() || dedicated_english_mode_ || local_input_mode_ != LocalInputMode::None)
    {
        prefix_candidates_.clear();
        prefix_query_input_.clear();
        return;
    }

    const std::string &raw_with_cases = get_pinyin_sequence_with_cases();
    const std::size_t end = quantized_prefix_end();
    if (end >= raw_with_cases.size())
    {
        prefix_candidates_.clear();
        prefix_query_input_.clear();
        return;
    }

    std::string prefix = CommonUtils::lowercase_ascii(raw_with_cases.substr(0, end));
    if (prefix_query_input_ != prefix)
    {
        prefix_candidates_ = engine_.query_raw_candidates(prefix, raw_with_cases.substr(0, end));
        prefix_query_input_ = prefix;
    }
    prefix_candidates_active_ = true;
}

void InputSession::update_dedicated_english_candidates()
{
    dedicated_english_candidates_.clear();
    if (dedicated_english_preedit_.empty())
    {
        return;
    }

    std::string prefix = CommonUtils::lowercase_ascii(dedicated_english_preedit_);
    dedicated_english_candidates_ = candidate_queries_.english_dictionary().query_prefix(prefix, 1000);
    apply_candidate_positions(dedicated_english_candidates_);
    if (dedicated_english_candidates_.empty())
    {
        dedicated_english_candidates_.emplace_back("", dedicated_english_preedit_, 0, CandidateSource::Generated);
    }
}

void InputSession::reset_composition()
{
    caret_.reset();
    prefix_candidates_.clear();
    prefix_query_input_.clear();
    prefix_candidates_active_ = false;
    immediate_phrase_progress_ = {};
    clear_pending_sequence();
    online_requests_.invalidate();
    const std::optional<SchemeType> original_scheme = temporary_original_scheme_;
    local_input_mode_ = LocalInputMode::None;
    temporary_original_scheme_.reset();
    local_preedit_.clear();
    local_candidates_.clear();
    dedicated_english_preedit_.clear();
    dedicated_english_candidates_.clear();
    mixed_candidates_.clear();
    engine_.reset();
    if (original_scheme.has_value() && engine_.current_scheme_type() != *original_scheme)
    {
        engine_.switch_scheme(*original_scheme);
    }
}

// The phrase assembled across segmented selections only means anything while its composition stays alive. Every path
// that empties the preedit without going through reset_composition() - backspacing the remaining pinyin away, or a
// continuing selection whose leftover input collapses to nothing - abandons that phrase, so it has to be dropped here
// instead of being concatenated with the next composition's selections and stored as a phrase nobody typed.
void InputSession::discard_abandoned_phrase_progress()
{
    if (!has_composition())
    {
        immediate_phrase_progress_ = {};
    }
}

std::optional<std::string> InputSession::learn_candidate(std::size_t index)
{
    if (!candidate_learning_enabled_ || index >= candidates().size())
    {
        return std::nullopt;
    }
    const bool temporary_english = local_input_mode_ == LocalInputMode::TemporaryEnglish;
    if ((dedicated_english_mode_ || temporary_english) &&
        candidates()[index].source == CandidateSource::EnglishDictionary && frequency_adjustment_configured_ &&
        frequency_adjustment_.mode != FrequencyAdjustmentMode::Disabled && index != 0)
    {
        return adjust_candidate_frequency(index, frequency_adjustment_, false);
    }

    const WordItem &selected = candidates()[index];
    if (selected.source == CandidateSource::Generated || selected.source == CandidateSource::Fallback ||
        selected.source == CandidateSource::NeuralDesktop || selected.source == CandidateSource::NeuralKeyboard ||
        selected.source == CandidateSource::CloudSuggestion || selected.source == CandidateSource::AiSuggestion)
    {
        return learn_sentence_candidate(selected);
    }
    if (!frequency_adjustment_configured_)
    {
        if (selected.source == CandidateSource::Database || selected.source == CandidateSource::UserDatabase)
        {
            const std::string &pinyin = selected.canonical_pinyin.empty() ? selected.pinyin : selected.canonical_pinyin;
            (void)engine_.update_weight_by_pinyin_and_word(selected.scheme, pinyin, selected.word);
        }
        return std::nullopt;
    }
    if (frequency_adjustment_.mode == FrequencyAdjustmentMode::Disabled || index == 0)
    {
        return std::nullopt;
    }
    if ((selected.source != CandidateSource::Database && selected.source != CandidateSource::UserDatabase) ||
        engine_.current_scheme_type() == SchemeType::JapaneseRomaji)
    {
        return std::nullopt;
    }

    return adjust_candidate_frequency(index, frequency_adjustment_, false);
}

KeyResult InputSession::pin_candidate(std::size_t index)
{
    if (index >= candidates().size())
        return {};
    const auto source = candidates()[index].source;
    if (source != CandidateSource::EnglishDictionary &&
        ((source != CandidateSource::Database && source != CandidateSource::UserDatabase) ||
         scheme() == SchemeType::JapaneseRomaji))
        return {};

    // Manual pinning is independent of automatic learning preferences and never selects text.
    auto diagnostic = adjust_candidate_frequency(index, {FrequencyAdjustmentMode::Pin, 1, 1}, true);
    if (diagnostic)
        return {true, std::nullopt, std::move(diagnostic)};
    reset_cache();
    if (dedicated_english_mode_)
        update_dedicated_english_candidates();
    else if (local_input_mode_ != LocalInputMode::None)
        diagnostic = update_local_candidates();
    else
        recompute_candidates();
    return {true, std::nullopt, std::move(diagnostic)};
}

std::optional<std::string> InputSession::adjust_candidate_frequency(std::size_t index,
                                                                    FrequencyAdjustmentOptions options, bool force_top)
{
    const WordItem &selected = candidates()[index];
    if (selected.source == CandidateSource::EnglishDictionary)
    {
        std::string context = dedicated_english_mode_ ? dedicated_english_preedit_
                                                      : (local_input_mode_ == LocalInputMode::TemporaryEnglish
                                                             ? local_preedit_.substr(1)
                                                             : engine_.get_request().raw_input);
        context = CommonUtils::lowercase_ascii(std::move(context));
        std::vector<WordItem> ranked_candidates;
        std::copy_if(candidates().begin(), candidates().end(), std::back_inserter(ranked_candidates),
                     [](const WordItem &candidate) { return candidate.source == CandidateSource::EnglishDictionary; });
        const bool adjusted = user_dictionary::adjust_english_candidate_ranking(
            path_to_utf8(paths_.dictionary(assets::english_dictionary)),
            path_to_utf8(paths_.user(assets::user_journal)), "english:" + context, ranked_candidates, selected.pinyin,
            selected.word, frequency_mode_name(options.mode), options.linear_step, options.trigger_count, force_top);
        return adjusted ? std::nullopt
                        : std::optional<std::string>("English candidate frequency could not be persisted.");
    }
    const bool super_jianpin = local_input_mode_ == LocalInputMode::SuperJianpin;
    // A wubi code the table could not answer carries quanpin words, so it is ranked, keyed and
    // stored as pinyin; only a code the wubi table answered is ranked under the code itself. The
    // fallback reuses the context the fixed positions are written under, otherwise a pinned
    // candidate would not be recognised here.
    const bool wubi = is_wubi_native_candidate(selected);
    const bool pinyin_fallback = is_wubi() && !wubi;
    std::string context_key =
        super_jianpin     ? local_modes::jianpin_ranking_context(local_preedit_.substr(1), scheme(), shuangpin_profile_)
        : wubi            ? engine_.get_request().raw_input
        : pinyin_fallback ? position_context(false)
                          : engine_.get_request().normalized_segmentation;
    if (!super_jianpin && context_key.empty())
    {
        context_key = engine_.get_request().segmentation;
    }
    const std::string entry_key = (wubi && !super_jianpin)
                                      ? selected.pinyin
                                      : (selected.canonical_pinyin.empty() ? context_key : selected.canonical_pinyin);
    std::vector<WordItem> ranked_candidates;
    ranked_candidates.reserve(candidates().size());
    for (const auto &candidate : candidates())
    {
        if (is_wubi() && candidate.scheme != selected.scheme)
            continue;
        ranked_candidates.push_back(candidate);
    }
    bool ranking_changed = false;
    const bool adjusted = user_dictionary::adjust_candidate_ranking(
        path_to_utf8(paths_.dictionary(assets::main_dictionary)), path_to_utf8(paths_.user(assets::user_journal)),
        context_key, ranked_candidates, entry_key, selected.word, frequency_mode_name(options.mode),
        options.linear_step, options.trigger_count, force_top, &ranking_changed,
        (wubi && !super_jianpin) ? user_dictionary::DictionaryKind::Wubi : user_dictionary::DictionaryKind::Pinyin);
    if (!adjusted)
    {
        return std::string("Unable to persist candidate frequency adjustment.");
    }
    if (ranking_changed)
    {
        engine_.reset_cache();
    }
    return std::nullopt;
}
void InputSession::set_quanpin_autocorrect_types(unsigned autocorrect_types)
{
    // Hosts may re-apply their configuration before every key. An unchanged
    // value must not rebuild and copy the mixed candidate list on that hot path.
    if (quanpin_autocorrect_types_ == autocorrect_types)
    {
        return;
    }
    quanpin_autocorrect_types_ = autocorrect_types;
    engine_.set_quanpin_autocorrect_types(autocorrect_types);
    update_mixed_candidates();
}

bool InputSession::apply_online_candidates(const OnlineQuery &query, const std::vector<std::string> &words,
                                           CandidateSource source)
{
    std::vector<WordItem> validated;
    if (!replace_online_candidate_batch(validated, query.cache_key, words, source))
        return false;
    const auto current = online_query();
    if (!current || !online_requests_.matches(*current, query) ||
        (source == CandidateSource::CloudSuggestion && !current->cloud_eligible) ||
        (source == CandidateSource::AiSuggestion && !current->ai_eligible))
        return false;
    if (engine_.apply_dynamic_candidates(words, source) != 0)
        return false;
    update_mixed_candidates();
    return true;
}

} // namespace metasequoia
