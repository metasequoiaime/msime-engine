#pragma once

#include "candidate_provider.h"
#include "../common/cache.h"
#include "../common/sqlite_database.h"
#include "../common/sqlite_statement.h"
#include "../japanese/japanese_sentence_decoder.h"
#include <sqlite3.h>
#include <memory>
#include <string>
#include <vector>

class JapaneseCandidateProvider : public ICandidateProvider
{
  public:
    explicit JapaneseCandidateProvider(std::string db_path = {}, std::string model_path = {});
    ~JapaneseCandidateProvider() override;

    JapaneseCandidateProvider(const JapaneseCandidateProvider &) = delete;
    JapaneseCandidateProvider &operator=(const JapaneseCandidateProvider &) = delete;

    std::vector<WordItem> query(const QueryRequest &request) override;
    std::optional<WordItem> find_candidate(SchemeType scheme, const std::string &key,
                                           const std::string &value) override;
    void reset_cache() override;
    int create_word(SchemeType, std::string, std::string) override;
    int update_weight_by_pinyin_and_word(SchemeType, std::string, std::string) override;
    int delete_by_pinyin_and_word(SchemeType, std::string, std::string) override;
    int cache_dynamic_candidate(SchemeType, const std::string &, const std::string &, CandidateSource) override;
    int cache_dynamic_candidate_for_request(const QueryRequest &, const std::string &, CandidateSource) override;

  private:
    void bind_query_statement(const std::string &raw_input_with_cases, const std::string &raw_input);
    bool ensure_query_statement();
    void close_database();

    std::string db_path_;
    std::string model_path_;
    metasequoia::SqliteDatabase db_;
    metasequoia::SqliteStatement query_statement_;
    std::shared_ptr<const japanese::JapaneseSentenceDecoder> sentence_decoder_;
    CircularBuffer<std::string, std::vector<WordItem>> dynamic_candidates_{128};
};
