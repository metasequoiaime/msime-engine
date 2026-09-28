#pragma once
#include "../quanpin/quanpin_dictionary.h"

#include "../core/query_request.h"
#include "../core/word_item.h"
#include "shuangpin_dictionary.h"
#include <string>
#include <vector>

class ShuangpinEngine
{
  public:
    explicit ShuangpinEngine(const ShuangpinProfile &profile = GetXiaoheShuangpinProfile(),
                             metasequoia::RuntimePaths paths = metasequoia::RuntimePaths::legacy());
    std::vector<WordItem> query(const QueryRequest &request);
    bool expand_initial_candidates(const QueryRequest &request, std::vector<WordItem> &candidates);
    std::optional<WordItem> find_candidate(const std::string &key, const std::string &value);
    int create_word(std::string pinyin, std::string word);
    int update_weight_by_pinyin_and_word(std::string pinyin, std::string word);
    int delete_by_pinyin_and_word(std::string pinyin, std::string word);
    int insert_word_to_series_cache(const std::string &pinyin, const std::string &word, CandidateSource source);
    int insert_word_to_series_cache(const std::string &pinyin, const std::vector<std::string> &words,
                                    CandidateSource source);
    int insert_word_to_active_helpcode_cache(const std::string &pinyin, const std::string &word, CandidateSource source,
                                             const std::string &double_helpcodes = {});
    int insert_word_to_active_helpcode_cache(const std::string &pinyin, const std::vector<std::string> &words,
                                             CandidateSource source, const std::string &double_helpcodes = {});
    void reset_cache();

    void set_helpcode_keymap(HelpcodeUtils::SharedKeymap table)
    {
        helpcodes_ = table;
        dictionary_.set_helpcode_keymap(std::move(table));
    }

    // Both schemes decode sentences with the same code, so both have to answer the host the same way.
    void set_sentence_alternatives(bool enabled)
    {
        dictionary_.set_sentence_alternatives(enabled);
        if (fuzzy_dictionary_)
            fuzzy_dictionary_->set_sentence_alternatives(enabled);
    }
    void set_sentence_association(const SentenceAssociationOptions &options)
    {
        dictionary_.set_sentence_association(options);
        if (fuzzy_dictionary_)
            fuzzy_dictionary_->set_sentence_association(options);
    }
    void set_rescoring_context(const std::string &context)
    {
        dictionary_.set_rescoring_context(context);
        if (fuzzy_dictionary_)
            fuzzy_dictionary_->set_rescoring_context(context);
    }

  private:
    const ShuangpinProfile profile_;
    ShuangpinDictionary dictionary_;
    std::unique_ptr<QuanpinDictionary> fuzzy_dictionary_;
    metasequoia::RuntimePaths paths_;
    HelpcodeUtils::SharedKeymap helpcodes_;
    std::vector<WordItem> append_fuzzy(std::vector<WordItem> exact, const std::string &raw_segmentation,
                                       metasequoia::FuzzyPinyinOptions options, const std::string &helpcodes = "");
};
