#include "ime_session.h"
#include "../schemes/quanpin_scheme.h"
#include "../schemes/shuangpin_scheme.h"
#include "../schemes/wubi_scheme.h"
#include "../schemes/japanese_romaji_scheme.h"
#include "../quanpin/quanpin_utils.h"
#include "../shuangpin/shuangpin_query.h"
#include <algorithm>
#include <stdexcept>

namespace
{
void ApplyShuangpinHelpcodeSegmentation(QueryRequest &request, const ShuangpinProfile &profile)
{
    if (request.scheme != SchemeType::Shuangpin || !request.enable_shuangpin_helpcode ||
        shuangpin::detect_active_double_helpcode_length(request.raw_input, request.raw_input_with_cases, profile) != 2)
    {
        return;
    }

    const size_t helpcode_length = 2;
    // The detector locates the help codes in delimiter-stripped space, so the split has to be made there too: slicing
    // the last raw bytes would push a pinyin letter into the base and a manual delimiter into the help codes.
    const std::string base_raw_input =
        shuangpin::trim_trailing_letters_preserve_delimiters(request.raw_input, helpcode_length);
    const std::string base_raw_input_with_cases =
        shuangpin::trim_trailing_letters_preserve_delimiters(request.raw_input_with_cases, helpcode_length);
    const std::string base_segmentation = shuangpin::segment_input(base_raw_input, profile);
    const std::string effective_input_with_cases = shuangpin::remove_manual_delimiters(request.raw_input_with_cases);
    const std::string help_codes =
        effective_input_with_cases.substr(effective_input_with_cases.size() - helpcode_length);

    request.raw_segmentation =
        shuangpin::apply_segmentation_cases(base_segmentation, base_raw_input_with_cases) + "'" + help_codes;
    request.normalized_segmentation = shuangpin::to_quanpin_segmentation(base_segmentation, profile) + "'" + help_codes;
    request.segmentation = request.normalized_segmentation;
}
} // namespace

ImeSession::ImeSession(SchemeType scheme_type, const ShuangpinProfile &shuangpin_profile,
                       metasequoia::RuntimePaths paths)
    : provider_registry_(shuangpin_profile, std::move(paths)), shuangpin_profile_(shuangpin_profile),
      scheme_(create_scheme(scheme_type))
{
    bind_wubi_scheme();
}

void ImeSession::bind_wubi_scheme()
{
    wubi_scheme_ = dynamic_cast<WubiScheme *>(scheme_.get());
    if (wubi_scheme_ != nullptr)
    {
        wubi_scheme_->set_mixed_pinyin_allowed(wubi_options_.mixed_pinyin);
    }
}

void ImeSession::set_wubi_input_options(metasequoia::WubiInputOptions options)
{
    wubi_options_ = options;
    if (wubi_scheme_ != nullptr)
    {
        wubi_scheme_->set_mixed_pinyin_allowed(wubi_options_.mixed_pinyin);
    }
}

void ImeSession::handle_key(ImeKeyCode vk, ImeModifierMask modifiers_down, ImeCharacter wch)
{
    scheme_->handle_key(vk, modifiers_down, wch);
    // 查询并更新候选词列表
    refresh_candidates();
}

void ImeSession::switch_scheme(SchemeType scheme_type)
{
    scheme_ = create_scheme(scheme_type);
    bind_wubi_scheme();
    state_ = CompositionState{};
}

void ImeSession::set_shuangpin_helpcode_enabled(bool enabled)
{
    enable_shuangpin_helpcode_ = enabled;
}

void ImeSession::set_quanpin_helpcode_enabled(bool enabled)
{
    enable_quanpin_helpcode_ = enabled;
}

void ImeSession::set_sentence_alternatives(bool enabled)
{
    sentence_alternatives_ = enabled;
}

void ImeSession::set_quanpin_autocorrect_types(unsigned autocorrect_types)
{
    quanpin_autocorrect_types_ = autocorrect_types;
}

void ImeSession::replace_shuangpin_raw_input(const std::string &raw_input, const std::string &raw_input_with_cases)
{
    if (scheme_->type() != SchemeType::Shuangpin)
    {
        return;
    }

    auto *shuangpin_scheme = dynamic_cast<ShuangpinScheme *>(scheme_.get());
    if (!shuangpin_scheme)
    {
        return;
    }

    shuangpin_scheme->set_raw_input(raw_input, raw_input_with_cases);
    refresh_candidates();
}

void ImeSession::replace_quanpin_raw_input(const std::string &raw_input, const std::string &raw_input_with_cases)
{
    if (scheme_->type() != SchemeType::Quanpin)
    {
        return;
    }

    auto *quanpin_scheme = dynamic_cast<QuanpinScheme *>(scheme_.get());
    if (!quanpin_scheme)
    {
        return;
    }

    quanpin_scheme->set_raw_input(raw_input, raw_input_with_cases);
    refresh_candidates();
}

void ImeSession::replace_wubi_raw_input(const std::string &raw_input, const std::string &raw_input_with_cases)
{
    if (scheme_->type() != SchemeType::Wubi)
    {
        return;
    }

    if (wubi_scheme_ == nullptr)
    {
        return;
    }

    // A replacement is host-driven editing of text the composition already holds, so the length it
    // is clipped to comes from the setting rather than from whatever the previous query happened to
    // answer. Without this, moving the caret through a mixed composition silently drops everything
    // past the fourth letter. refresh_candidates puts the query-derived value back.
    wubi_scheme_->set_extended_length_allowed(wubi_options_.mixed_pinyin);
    wubi_scheme_->set_raw_input(raw_input, raw_input_with_cases);
    refresh_candidates();
}

void ImeSession::reset()
{
    scheme_->reset();
    composition_uses_pinyin_fallback_ = false;
    state_ = CompositionState{};
}

SchemeType ImeSession::candidate_scheme() const
{
    // The dictionary that actually produced the candidates on screen. A wubi code answered by the
    // quanpin fallback is cached, looked up and learnt in the pinyin dictionary; routing it by the
    // typed scheme would clear the wrong cache and write the weight into the wubi table.
    return state_.answered_by_pinyin_fallback ? SchemeType::Quanpin : current_scheme_type();
}

void ImeSession::reset_cache()
{
    provider_registry_.reset_cache(candidate_scheme());
    refresh_candidates();
}

int ImeSession::create_word(std::string pinyin, std::string word)
{
    return provider_registry_.create_word(current_scheme_type(), std::move(pinyin), std::move(word));
}

int ImeSession::update_weight_by_pinyin_and_word(std::string pinyin, std::string word)
{
    return provider_registry_.update_weight_by_pinyin_and_word(candidate_scheme(), std::move(pinyin), std::move(word));
}

int ImeSession::delete_by_pinyin_and_word(std::string pinyin, std::string word)
{
    return provider_registry_.delete_by_pinyin_and_word(current_scheme_type(), std::move(pinyin), std::move(word));
}

int ImeSession::cache_dynamic_candidate(const std::string &pinyin, const std::string &word, CandidateSource source)
{
    return provider_registry_.cache_dynamic_candidate(current_scheme_type(), pinyin, word, source);
}

void ImeSession::replace_japanese_raw_input(const std::string &raw_input, const std::string &raw_input_with_cases)
{
    if (scheme_->type() != SchemeType::JapaneseRomaji)
        return;
    auto *japanese_scheme = dynamic_cast<JapaneseRomajiScheme *>(scheme_.get());
    if (!japanese_scheme)
        return;
    japanese_scheme->set_raw_input(raw_input, raw_input_with_cases);
    refresh_candidates();
}

bool ImeSession::cycle_japanese_kana_variant()
{
    if (scheme_->type() != SchemeType::JapaneseRomaji)
        return false;
    auto *japanese_scheme = dynamic_cast<JapaneseRomajiScheme *>(scheme_.get());
    if (!japanese_scheme || !japanese_scheme->cycle_last_kana_variant())
        return false;
    refresh_candidates();
    return true;
}

void ImeSession::replace_active_raw_input(const std::string &raw_input, const std::string &raw_input_with_cases)
{
    switch (scheme_->type())
    {
    case SchemeType::Quanpin:
        replace_quanpin_raw_input(raw_input, raw_input_with_cases);
        return;
    case SchemeType::Shuangpin:
        replace_shuangpin_raw_input(raw_input, raw_input_with_cases);
        return;
    case SchemeType::Wubi:
        replace_wubi_raw_input(raw_input, raw_input_with_cases);
        return;
    case SchemeType::JapaneseRomaji:
        replace_japanese_raw_input(raw_input, raw_input_with_cases);
        return;
    }
}

std::vector<WordItem> ImeSession::query_raw_candidates(const std::string &raw_input,
                                                       const std::string &raw_input_with_cases)
{
    // A throwaway scheme keeps the live scheme's key strokes and composition untouched while a
    // caret prefix is decoded independently.
    const std::unique_ptr<IInputScheme> query_scheme = create_scheme(scheme_->type());
    query_scheme->set_raw_input(raw_input, raw_input_with_cases);
    QueryRequest request = query_scheme->build_request();
    apply_request_options(request);
    ApplyShuangpinHelpcodeSegmentation(request, shuangpin_profile_);
    if (!request.valid)
    {
        return {};
    }
    return provider_registry_.resolve(request.scheme).query(request);
}

int ImeSession::cache_dynamic_candidate_for_current_request(const std::string &word, CandidateSource source)
{
    return provider_registry_.cache_dynamic_candidate_for_request(state_.request, word, source);
}

int ImeSession::apply_dynamic_candidate(const std::string &word, CandidateSource source)
{
    const int result = cache_dynamic_candidate_for_current_request(word, source);
    if (result == 0)
    {
        refresh_candidates();
    }
    return result;
}

SchemeType ImeSession::current_scheme_type() const
{
    return scheme_->type();
}

const std::string &ImeSession::get_preedit() const
{
    return state_.preedit;
}

const QueryRequest &ImeSession::get_request() const
{
    return state_.request;
}

const std::vector<WordItem> &ImeSession::get_candidates() const
{
    return state_.candidates;
}

std::optional<WordItem> ImeSession::find_candidate(const std::string &key, const std::string &value)
{
    return provider_registry_.find_candidate(candidate_scheme(), key, value);
}

bool ImeSession::expand_initial_candidates()
{
    return provider_registry_.expand_initial_candidates(state_.request, state_.candidates);
}

void ImeSession::refresh_candidates()
{
    state_.preedit = scheme_->get_preedit();
    state_.request = scheme_->build_request();
    apply_request_options(state_.request);
    ApplyShuangpinHelpcodeSegmentation(state_.request, shuangpin_profile_);

    state_.answered_by_pinyin_fallback = false;

    // An emptied composition starts over, so the fallback flag has to be cleared before the invalid
    // request leaves early: a scheme that reports an empty input as invalid would otherwise carry the
    // flag into the next code typed and blank out what the table answers.
    if (state_.request.raw_input.empty())
    {
        composition_uses_pinyin_fallback_ = false;
    }

    if (!state_.request.valid)
    {
        state_.candidates.clear();
        return;
    }

    state_.candidates = provider_registry_.resolve(state_.request.scheme).query(state_.request);
    // Prefix rows are useful hints, but only an exact row for the current code means that the
    // wubi table answered it. A longer-code hint must not suppress mixed-pinyin fallback.
    const bool wubi_table_answered =
        !composition_uses_pinyin_fallback_ &&
        std::any_of(state_.candidates.begin(), state_.candidates.end(),
                    [this](const WordItem &item) { return item.pinyin == state_.request.normalized_input; });

    if (wubi_scheme_ != nullptr)
    {
        // Once the table has failed the code in hand, mixed input lets the composition grow past
        // four letters so a full spelling can be finished. A code the table answers keeps the limit.
        wubi_scheme_->set_extended_length_allowed(wubi_options_.mixed_pinyin && !wubi_table_answered);
    }

    if (wubi_scheme_ != nullptr && !wubi_table_answered && wubi_options_.mixed_pinyin)
    {
        // The wubi table knows nothing for this code, so the same letters are offered to quanpin.
        // A code the table does know never reaches here, which is what keeps this out of the way of
        // someone typing wubi fluently: it only speaks up where nothing could be typed at all.
        QuanpinScheme pinyin;
        pinyin.set_raw_input(state_.request.raw_input, state_.request.raw_input_with_cases);
        QueryRequest fallback = pinyin.build_request();
        apply_request_options(fallback);
        // The same physical keys produced these letters, so the strokes carry over rather than
        // reaching the provider empty.
        fallback.key_strokes = state_.request.key_strokes;
        if (fallback.valid)
        {
            // Keep native wubi prefix hints when pinyin has no answer. Once pinyin has taken
            // ownership of a composition, however, do not switch back to wubi for its tail.
            std::vector<WordItem> pinyin_candidates = provider_registry_.resolve(fallback.scheme).query(fallback);
            state_.answered_by_pinyin_fallback = !pinyin_candidates.empty();
            if (state_.answered_by_pinyin_fallback || composition_uses_pinyin_fallback_)
            {
                state_.candidates = std::move(pinyin_candidates);
            }
            composition_uses_pinyin_fallback_ = composition_uses_pinyin_fallback_ || state_.answered_by_pinyin_fallback;
            if (state_.answered_by_pinyin_fallback)
            {
                // The candidates are pinyin, so the request describing them has to be the pinyin one:
                // everything downstream reads the segmentation from here, and the wubi request carries
                // the letters unsplit. The preedit keeps showing the letters as typed either way.
                state_.request = std::move(fallback);
            }
        }
    }
}

void ImeSession::apply_request_options(QueryRequest &request) const
{
    request.enable_shuangpin_helpcode = enable_shuangpin_helpcode_;
    request.enable_quanpin_helpcode = enable_quanpin_helpcode_;
    request.sentence_alternatives = sentence_alternatives_;
    request.enable_quanpin_autocorrect_transposition =
        (quanpin_autocorrect_types_ & quanpin::kAutocorrectTransposition) != 0;
    request.enable_quanpin_autocorrect_neighbor = (quanpin_autocorrect_types_ & quanpin::kAutocorrectNeighbor) != 0;
    request.fuzzy_pinyin = fuzzy_pinyin_;
    request.sentence_association = sentence_association_;
    request.rescoring_context = rescoring_context_;
}

std::unique_ptr<IInputScheme> ImeSession::create_scheme(SchemeType scheme_type) const
{
    switch (scheme_type)
    {
    case SchemeType::Shuangpin:
        return std::make_unique<ShuangpinScheme>(shuangpin_profile_);
    case SchemeType::Quanpin:
        return std::make_unique<QuanpinScheme>();
    case SchemeType::Wubi:
        return std::make_unique<WubiScheme>();
    case SchemeType::JapaneseRomaji:
        return std::make_unique<JapaneseRomajiScheme>();
    default:
        throw std::runtime_error("Unknown scheme type.");
    }
}

int ImeSession::apply_dynamic_candidates(const std::vector<std::string> &words, CandidateSource source)
{
    const int result = provider_registry_.cache_dynamic_candidate_for_request(state_.request, words, source);
    if (result == 0)
        refresh_candidates();
    return result;
}
