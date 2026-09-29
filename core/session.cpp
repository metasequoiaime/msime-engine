#include "../include/metasequoia/session.h"
#include "candidate_utils.h"
#include "input_session.h"
#include "nine_key_session.h"
#include <stdexcept>

namespace metasequoia
{
class Session::Impl
{
  public:
    explicit Impl(const SessionOptions &options)
        : session(options.scheme, options.shuangpin_profile, options.paths),
          nine_key(options.paths, options.learning, options.frequency, options.fuzzy_pinyin, options.english)
    {
        shuangpin_preedit_uses_raw = options.shuangpin_preedit_uses_raw;
        session.set_shuangpin_preedit_uses_raw(options.shuangpin_preedit_uses_raw);
        session.set_quanpin_autocorrect_types(options.autocorrect_types);
        session.set_fuzzy_pinyin_options(options.fuzzy_pinyin);
        session.set_sentence_association(options.sentence_association);
        session.set_rescoring_context(options.rescoring_context);
        session.set_helpcode_enabled(options.helpcode);
        if (!session.set_helpcode_schema(options.helpcode_schema) ||
            !session.set_frequency_adjustment(options.frequency) || !session.set_english_input_options(options.english))
            throw std::invalid_argument("Invalid session options");
        session.set_local_mode_options(options.local_modes);
        session.set_mixed_expressive_options(options.expressive);
        session.set_wubi_input_options(options.wubi);
        session.enable_fixed_positions();
    }
    InputSession session;
    NineKeySession nine_key;
    bool nine_key_enabled = false;
    bool shuangpin_preedit_uses_raw = true;
};

Session::Session(SessionOptions options)
{
    options.paths.validate();
    impl_ = std::make_unique<Impl>(options);
    impl_->session.set_chinese_punctuation_enabled(options.chinese_punctuation);
    impl_->session.set_paired_punctuation_enabled(options.paired_punctuation);
    impl_->session.set_punctuation_lock(options.punctuation_lock);
    impl_->session.set_candidate_learning_enabled(options.learning);
    impl_->session.set_sentence_alternatives(options.sentence_alternatives);
}
Session::~Session() = default;
void Session::set_nine_key_enabled(bool enabled)
{
    impl_->nine_key.command(Command::Cancel);
    impl_->nine_key_enabled = enabled;
    impl_->nine_key.set_english_only(enabled && impl_->session.dedicated_english_mode());
}
KeyResult Session::choose_nine_key_spelling(std::size_t index)
{
    return impl_->nine_key.choose_spelling(index);
}
KeyResult Session::character(char value, bool shift_only)
{
    // English is a mode rather than a scheme, so the grid stays available in it: the same digits spell words instead of
    // syllables. A local input mode still takes the keys, and the scheme underneath must still be quanpin, because that
    // is the only one whose syllables the grid knows.
    if (impl_->nine_key_enabled && impl_->session.scheme() == SchemeType::Quanpin &&
        impl_->session.local_input_mode() == LocalInputMode::None && impl_->session.preedit().empty() && value >= '2' &&
        value <= '9')
        return impl_->nine_key.character(value);
    if (impl_->nine_key.active())
        return {};
    return impl_->session.handle_character(value, shift_only);
}
KeyResult Session::command(Command value)
{
    if (impl_->nine_key.active())
        return impl_->nine_key.command(value);
    return impl_->session.handle_command(value);
}
KeyResult Session::candidate_key(char value)
{
    if (impl_->nine_key.active())
        return value >= '1' && value <= '9' ? impl_->nine_key.select(value - '1') : KeyResult{};
    return impl_->session.handle_candidate_key(value);
}
std::vector<std::size_t> Session::segment_raw_boundaries() const
{
    if (impl_->nine_key.active())
        return {};
    return impl_->session.segment_raw_boundaries();
}
KeyResult Session::punctuation(char value)
{
    if (impl_->nine_key.active())
    {
        auto result = impl_->session.handle_punctuation(value);
        if (!result.handled)
            return result;
        const auto composition = impl_->nine_key.finish(0);
        result.commit = composition.commit.value_or("") + result.commit.value_or("");
        return result;
    }
    return impl_->session.handle_punctuation(value);
}
void Session::set_chinese_punctuation_enabled(bool enabled)
{
    impl_->session.set_chinese_punctuation_enabled(enabled);
}
void Session::set_paired_punctuation_enabled(bool enabled)
{
    impl_->session.set_paired_punctuation_enabled(enabled);
}
void Session::balance_paired_punctuation_after_auto_close(char opening)
{
    impl_->session.balance_paired_punctuation_after_auto_close(opening);
}
void Session::set_punctuation_lock(int lock)
{
    if (lock < 0 || lock > 2)
        throw std::invalid_argument("Invalid punctuation lock");
    impl_->session.set_punctuation_lock(lock);
}
KeyResult Session::select(std::size_t index)
{
    if (impl_->nine_key.active())
        return impl_->nine_key.select(index);
    return impl_->session.select_candidate(index);
}
KeyResult Session::select_edge(std::size_t index, CandidateEdge edge)
{
    if (impl_->nine_key.active())
        return {};
    return impl_->session.select_candidate_edge(index, edge);
}
KeyResult Session::pin(std::size_t index)
{
    if (impl_->nine_key.active())
        return impl_->nine_key.pin(index);
    return impl_->session.pin_candidate(index);
}
KeyResult Session::remove(std::size_t index)
{
    if (impl_->nine_key.active())
        return impl_->nine_key.remove(index);
    return impl_->session.remove_candidate(index);
}
KeyResult Session::fix_position(std::size_t index, int position)
{
    if (position < 1 || position > 5)
        return {};
    if (impl_->nine_key.active())
        return impl_->nine_key.set_position(index, position);
    return impl_->session.set_candidate_position(index, position);
}
KeyResult Session::clear_position(std::size_t index)
{
    if (impl_->nine_key.active())
        return impl_->nine_key.set_position(index, 0);
    return impl_->session.set_candidate_position(index, 0);
}
KeyResult Session::finish()
{
    return finish(0);
}
KeyResult Session::finish(std::size_t first_index)
{
    if (impl_->nine_key.active())
        return impl_->nine_key.finish(first_index);
    return impl_->session.finish_composition(first_index);
}
void Session::switch_scheme(SchemeType scheme)
{
    impl_->nine_key.command(Command::Cancel);
    impl_->session.switch_scheme(scheme);
}
bool Session::is_supported_helpcode_schema(const std::string &schema)
{
    return InputSession::is_supported_helpcode_schema(schema);
}
bool Session::set_helpcode_schema(const std::string &schema)
{
    return impl_->session.set_helpcode_schema(schema);
}
void Session::set_helpcode_enabled(bool enabled)
{
    impl_->session.set_helpcode_enabled(enabled);
}
void Session::set_dedicated_english(bool enabled)
{
    impl_->nine_key.command(Command::Cancel);
    impl_->session.set_dedicated_english_mode(enabled);
    // The grid's digits mean letters in English and syllables outside it, so whichever of the two switches moves last
    // has to tell it.
    impl_->nine_key.set_english_only(impl_->nine_key_enabled && enabled);
}
void Session::set_wubi_mixed_pinyin(bool enabled)
{
    auto options = impl_->session.wubi_input_options();
    options.mixed_pinyin = enabled;
    impl_->session.set_wubi_input_options(options);
}
void Session::reset_cache()
{
    impl_->session.reset_cache();
}
void Session::set_sentence_association(SentenceAssociationOptions options)
{
    impl_->session.set_sentence_association(options);
}
void Session::set_rescoring_context(std::string context)
{
    impl_->session.set_rescoring_context(std::move(context));
}
SessionSnapshot Session::snapshot() const
{
    if (impl_->nine_key.active())
        return impl_->nine_key.snapshot();
    const auto &session = impl_->session;
    SessionSnapshot view{session.scheme(),
                         session.local_input_mode(),
                         session.preedit(),
                         session.raw_segmentation(),
                         session.normalized_segmentation(),
                         session.candidates(),
                         session.dedicated_english_mode(),
                         session.editing_text(),
                         session.caret_position()};
    view.shuangpin_profile = session.shuangpin_profile().name;
    if (!impl_->shuangpin_preedit_uses_raw && session.scheme() == SchemeType::Shuangpin &&
        session.local_input_mode() == LocalInputMode::None && !session.dedicated_english_mode())
        view.preedit = session.get_pinyin_segmentation_with_cases();
    view.answered_by_pinyin_fallback = session.answered_by_pinyin_fallback();
    view.candidate_sources = collect_candidate_sources(view.candidates);
    view.candidate_answers_key.reserve(view.candidates.size());
    for (const auto &candidate : view.candidates)
    {
        // `pinyin` rather than `canonical_pinyin`: the former is what composition advancement
        // consumes, which is the question being asked.
        view.candidate_answers_key.push_back(
            session.selection_completes_composition(candidate.pinyin, candidate.word, candidate.scheme));
    }
    view.candidate_annotations = session.candidate_annotations();
    return view;
}
std::optional<OnlineQuery> Session::online_query() const
{
    if (impl_->nine_key.active())
        return std::nullopt;
    return impl_->session.online_query();
}
bool Session::apply_online_candidate(const OnlineQuery &query, std::string candidate, CandidateSource source)
{
    if (impl_->nine_key.active())
        return false;
    return impl_->session.apply_online_candidate(query, std::move(candidate), source);
}
bool Session::apply_online_candidates(const OnlineQuery &query, const std::vector<std::string> &words,
                                      CandidateSource source)
{
    if (impl_->nine_key.active())
        return false;
    return impl_->session.apply_online_candidates(query, words, source);
}
} // namespace metasequoia
