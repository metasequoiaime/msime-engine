#include "pinyin_candidate_provider.h"
#include "../core/scheme_type.h"
#include "../shuangpin/shuangpin_query.h"
#include "../shuangpin/shuangpin_utils.h"

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

    std::vector<WordItem> candidates;
    if (request.scheme == SchemeType::Shuangpin)
    {
        candidates = shuangpin_engine_.query(request);
    }
    else if (request.scheme == SchemeType::Quanpin)
    {
        candidates = quanpin_engine_.query(request);
    }
    else
    {
        return {};
    }
    for (WordItem &item : candidates)
        item.scheme = request.scheme;
    return candidates;
}

bool PinyinCandidateProvider::expand_initial_candidates(const QueryRequest &request, std::vector<WordItem> &candidates)
{
    if (request.scheme == SchemeType::Shuangpin)
    {
        return shuangpin_engine_.expand_initial_candidates(request, candidates);
    }
    if (request.scheme == SchemeType::Quanpin)
    {
        return quanpin_engine_.expand_initial_candidates(request, candidates);
    }
    return false;
}

void PinyinCandidateProvider::reset_cache()
{
    quanpin_engine_.reset_cache();
    shuangpin_engine_.reset_cache();
}

int PinyinCandidateProvider::create_word(SchemeType scheme, std::string pinyin, std::string word)
{
    if (scheme == SchemeType::Shuangpin)
    {
        return shuangpin_engine_.create_word(std::move(pinyin), std::move(word));
    }
    return quanpin_engine_.create_word(std::move(pinyin), std::move(word));
}

int PinyinCandidateProvider::update_weight_by_pinyin_and_word(SchemeType scheme, std::string pinyin, std::string word)
{
    if (scheme == SchemeType::Shuangpin)
    {
        return shuangpin_engine_.update_weight_by_pinyin_and_word(std::move(pinyin), std::move(word));
    }
    return quanpin_engine_.update_weight_by_pinyin_and_word(std::move(pinyin), std::move(word));
}

int PinyinCandidateProvider::delete_by_pinyin_and_word(SchemeType scheme, std::string pinyin, std::string word)
{
    if (scheme == SchemeType::Shuangpin)
    {
        return shuangpin_engine_.delete_by_pinyin_and_word(std::move(pinyin), std::move(word));
    }
    return quanpin_engine_.delete_by_pinyin_and_word(std::move(pinyin), std::move(word));
}

int PinyinCandidateProvider::cache_dynamic_candidate(SchemeType scheme, const std::string &pinyin,
                                                     const std::string &word, CandidateSource source)
{
    if (scheme == SchemeType::Shuangpin)
    {
        return shuangpin_engine_.insert_word_to_series_cache(pinyin, word, source);
    }
    return quanpin_engine_.insert_word_to_series_cache(pinyin, word, source);
}

std::optional<WordItem> PinyinCandidateProvider::find_candidate(SchemeType scheme, const std::string &key,
                                                                const std::string &value)
{
    return scheme == SchemeType::Shuangpin ? shuangpin_engine_.find_candidate(key, value)
                                           : quanpin_engine_.find_candidate(key, value);
}

std::optional<std::string> PinyinCandidateProvider::active_helpcode_for_request(const QueryRequest &request) const
{
    if (request.scheme != SchemeType::Shuangpin || !request.enable_shuangpin_helpcode)
        return std::nullopt;

    const std::string &raw_input_with_cases =
        request.raw_input_with_cases.empty() ? request.raw_input : request.raw_input_with_cases;
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
    if (request.scheme == SchemeType::Quanpin)
    {
        return quanpin_engine_.insert_word_to_series_cache(request, word, source);
    }

    if (request.scheme != SchemeType::Shuangpin)
    {
        return -1;
    }

    if (const auto helpcodes = active_helpcode_for_request(request))
    {
        if (helpcodes->empty())
            return shuangpin_engine_.insert_word_to_active_helpcode_cache(request.raw_input, word, source);
        return shuangpin_engine_.insert_word_to_active_helpcode_cache(request.raw_input, word, source, *helpcodes);
    }
    return shuangpin_engine_.insert_word_to_series_cache(request.raw_input, word, source);
}

int PinyinCandidateProvider::cache_dynamic_candidate_for_request(const QueryRequest &request,
                                                                 const std::vector<std::string> &words,
                                                                 CandidateSource source)
{
    if (request.scheme == SchemeType::Quanpin)
    {
        return quanpin_engine_.insert_word_to_series_cache(request, words, source);
    }

    if (request.scheme != SchemeType::Shuangpin)
    {
        return -1;
    }

    if (const auto helpcodes = active_helpcode_for_request(request))
    {
        if (helpcodes->empty())
            return shuangpin_engine_.insert_word_to_active_helpcode_cache(request.raw_input, words, source);
        return shuangpin_engine_.insert_word_to_active_helpcode_cache(request.raw_input, words, source, *helpcodes);
    }
    return shuangpin_engine_.insert_word_to_series_cache(request.raw_input, words, source);
}
