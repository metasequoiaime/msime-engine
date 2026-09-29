#include "pinyin_candidate_provider.h"
#include "../core/scheme_type.h"
#include "../shuangpin/shuangpin_query.h"
#include "../shuangpin/shuangpin_utils.h"

namespace
{
template <typename QuanpinHandler, typename ShuangpinHandler>
decltype(auto) route_pinyin_scheme(SchemeType scheme, QuanpinHandler quanpin_handler,
                                   ShuangpinHandler shuangpin_handler)
{
    if (scheme == SchemeType::Shuangpin)
        return shuangpin_handler();
    return quanpin_handler();
}

template <typename QuanpinHandler, typename ShuangpinHandler, typename UnsupportedHandler>
decltype(auto) route_supported_pinyin_scheme(SchemeType scheme, QuanpinHandler quanpin_handler,
                                             ShuangpinHandler shuangpin_handler, UnsupportedHandler unsupported_handler)
{
    if (scheme == SchemeType::Shuangpin)
        return shuangpin_handler();
    if (scheme == SchemeType::Quanpin)
        return quanpin_handler();
    return unsupported_handler();
}

template <typename HelpcodeQuery, typename QuanpinHandler, typename ShuangpinSeriesHandler,
          typename ShuangpinActiveHandler>
int route_cached_candidate_request(SchemeType scheme, HelpcodeQuery helpcode_query, QuanpinHandler quanpin_handler,
                                   ShuangpinSeriesHandler shuangpin_series_handler,
                                   ShuangpinActiveHandler shuangpin_active_handler)
{
    if (scheme == SchemeType::Quanpin)
        return quanpin_handler();
    if (scheme != SchemeType::Shuangpin)
        return -1;

    if (const auto helpcodes = helpcode_query())
        return shuangpin_active_handler(*helpcodes);
    return shuangpin_series_handler();
}
} // namespace

PinyinCandidateProvider::PinyinCandidateProvider(const ShuangpinProfile &shuangpin_profile,
                                                 metasequoia::RuntimePaths paths)
    : shuangpin_profile_(shuangpin_profile), quanpin_engine_(paths), shuangpin_engine_(shuangpin_profile, paths)
{
}

std::vector<WordItem> PinyinCandidateProvider::query(const QueryRequest &request)
{
    if (!request.valid)
    {
        return {};
    }

    std::vector<WordItem> candidates = route_supported_pinyin_scheme(
        request.scheme, [&] { return quanpin_engine_.query(request); },
        [&] { return shuangpin_engine_.query(request); }, [] { return std::vector<WordItem>{}; });
    for (WordItem &item : candidates)
        item.scheme = request.scheme;
    return candidates;
}

bool PinyinCandidateProvider::expand_initial_candidates(const QueryRequest &request, std::vector<WordItem> &candidates)
{
    return route_supported_pinyin_scheme(
        request.scheme, [&] { return quanpin_engine_.expand_initial_candidates(request, candidates); },
        [&] { return shuangpin_engine_.expand_initial_candidates(request, candidates); }, [] { return false; });
}

void PinyinCandidateProvider::reset_cache()
{
    quanpin_engine_.reset_cache();
    shuangpin_engine_.reset_cache();
}

int PinyinCandidateProvider::create_word(SchemeType scheme, std::string pinyin, std::string word)
{
    return route_pinyin_scheme(
        scheme, [&] { return quanpin_engine_.create_word(std::move(pinyin), std::move(word)); },
        [&] { return shuangpin_engine_.create_word(std::move(pinyin), std::move(word)); });
}

int PinyinCandidateProvider::update_weight_by_pinyin_and_word(SchemeType scheme, std::string pinyin, std::string word)
{
    return route_pinyin_scheme(
        scheme, [&] { return quanpin_engine_.update_weight_by_pinyin_and_word(std::move(pinyin), std::move(word)); },
        [&] { return shuangpin_engine_.update_weight_by_pinyin_and_word(std::move(pinyin), std::move(word)); });
}

int PinyinCandidateProvider::delete_by_pinyin_and_word(SchemeType scheme, std::string pinyin, std::string word)
{
    return route_pinyin_scheme(
        scheme, [&] { return quanpin_engine_.delete_by_pinyin_and_word(std::move(pinyin), std::move(word)); },
        [&] { return shuangpin_engine_.delete_by_pinyin_and_word(std::move(pinyin), std::move(word)); });
}

int PinyinCandidateProvider::cache_dynamic_candidate(SchemeType scheme, const std::string &pinyin,
                                                     const std::string &word, CandidateSource source)
{
    return route_pinyin_scheme(
        scheme, [&] { return quanpin_engine_.insert_word_to_series_cache(pinyin, word, source); },
        [&] { return shuangpin_engine_.insert_word_to_series_cache(pinyin, word, source); });
}

std::optional<WordItem> PinyinCandidateProvider::find_candidate(SchemeType scheme, const std::string &key,
                                                                const std::string &value)
{
    return route_pinyin_scheme(
        scheme, [&] { return quanpin_engine_.find_candidate(key, value); },
        [&] { return shuangpin_engine_.find_candidate(key, value); });
}

std::optional<std::string> PinyinCandidateProvider::active_helpcode_for_request(const QueryRequest &request) const
{
    if (request.scheme != SchemeType::Shuangpin || !request.enable_shuangpin_helpcode)
        return std::nullopt;

    const std::string &raw_input_with_cases = query_request_raw_input_with_cases(request);
    const std::string pure_input = shuangpin::remove_manual_delimiters(request.raw_input);
    const std::string pure_input_with_cases = shuangpin::remove_manual_delimiters(raw_input_with_cases);
    if (ShuangpinUtil::IsFullHelpMode(pure_input_with_cases, shuangpin_profile_))
        return ShuangpinUtil::GetFullHelpCodes(pure_input_with_cases);

    if (pure_input.size() % 2 == 1 && pure_input.size() > 1)
    {
        const std::string base_raw_input = pure_input.substr(0, pure_input.size() - 1);
        const std::string base_raw_segmentation = shuangpin::segment_input(base_raw_input, shuangpin_profile_);
        if (ShuangpinUtil::is_all_complete_pinyin(base_raw_input, base_raw_segmentation))
            return std::string{};
    }
    return std::nullopt;
}

int PinyinCandidateProvider::cache_dynamic_candidate_for_request(const QueryRequest &request, const std::string &word,
                                                                 CandidateSource source)
{
    return route_cached_candidate_request(
        request.scheme, [&] { return active_helpcode_for_request(request); },
        [&] { return quanpin_engine_.insert_word_to_series_cache(request, word, source); },
        [&] { return shuangpin_engine_.insert_word_to_series_cache(request.raw_input, word, source); },
        [&](const std::string &helpcodes) {
            return shuangpin_engine_.insert_word_to_active_helpcode_cache(request.raw_input, word, source, helpcodes);
        });
}

int PinyinCandidateProvider::cache_dynamic_candidate_for_request(const QueryRequest &request,
                                                                 const std::vector<std::string> &words,
                                                                 CandidateSource source)
{
    return route_cached_candidate_request(
        request.scheme, [&] { return active_helpcode_for_request(request); },
        [&] { return quanpin_engine_.insert_word_to_series_cache(request, words, source); },
        [&] { return shuangpin_engine_.insert_word_to_series_cache(request.raw_input, words, source); },
        [&](const std::string &helpcodes) {
            return shuangpin_engine_.insert_word_to_active_helpcode_cache(request.raw_input, words, source, helpcodes);
        });
}
