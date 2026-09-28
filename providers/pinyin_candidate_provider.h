#pragma once

#include "candidate_provider.h"
#include "../quanpin/engine.h"
#include "../shuangpin/engine.h"

class PinyinCandidateProvider : public ICandidateProvider
{
  public:
    explicit PinyinCandidateProvider(const ShuangpinProfile &shuangpin_profile = GetXiaoheShuangpinProfile(),
                                     metasequoia::RuntimePaths paths = metasequoia::RuntimePaths::legacy());
    std::vector<WordItem> query(const QueryRequest &request) override;
    std::optional<WordItem> find_candidate(SchemeType scheme, const std::string &key,
                                           const std::string &value) override;
    bool expand_initial_candidates(const QueryRequest &request, std::vector<WordItem> &candidates);
    void reset_cache() override;
    int create_word(SchemeType scheme, std::string pinyin, std::string word) override;
    int update_weight_by_pinyin_and_word(SchemeType scheme, std::string pinyin, std::string word) override;
    int delete_by_pinyin_and_word(SchemeType scheme, std::string pinyin, std::string word) override;
    int cache_dynamic_candidate(SchemeType scheme, const std::string &pinyin, const std::string &word,
                                CandidateSource source) override;
    int cache_dynamic_candidate_for_request(const QueryRequest &request, const std::string &word,
                                            CandidateSource source) override;
    int cache_dynamic_candidate_for_request(const QueryRequest &request, const std::vector<std::string> &words,
                                            CandidateSource source) override;

    void set_helpcode_keymap(HelpcodeUtils::SharedKeymap table)
    {
        quanpin_engine_.set_helpcode_keymap(table);
        shuangpin_engine_.set_helpcode_keymap(std::move(table));
    }

  private:
    std::optional<std::string> active_helpcode_for_request(const QueryRequest &request) const;

    const ShuangpinProfile shuangpin_profile_;
    QuanpinEngine quanpin_engine_;
    ShuangpinEngine shuangpin_engine_;
};
