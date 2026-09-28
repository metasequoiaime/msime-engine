#pragma once

#include "candidate_provider.h"
#include "../core/runtime_paths.h"
#include <sqlite3.h>
#include <string>

class WubiCandidateProvider : public ICandidateProvider
{
  public:
    explicit WubiCandidateProvider(std::string db_path = {},
                                   metasequoia::RuntimePaths paths = metasequoia::RuntimePaths::legacy());
    ~WubiCandidateProvider() override;

    WubiCandidateProvider(const WubiCandidateProvider &) = delete;
    WubiCandidateProvider &operator=(const WubiCandidateProvider &) = delete;

    std::vector<WordItem> query(const QueryRequest &request) override;
    std::optional<WordItem> find_candidate(SchemeType, const std::string &, const std::string &) override
    {
        return std::nullopt;
    }
    void reset_cache() override;
    int create_word(SchemeType scheme, std::string code, std::string word) override;
    int update_weight_by_pinyin_and_word(SchemeType scheme, std::string code, std::string word) override;
    int delete_by_pinyin_and_word(SchemeType scheme, std::string code, std::string word) override;
    int cache_dynamic_candidate(SchemeType scheme, const std::string &code, const std::string &word,
                                CandidateSource source) override;
    int cache_dynamic_candidate_for_request(const QueryRequest &request, const std::string &word,
                                            CandidateSource source) override;

  private:
    bool ensure_query_statement();
    void close_database();
    std::string journal_db_path() const;

    std::string db_path_;
    metasequoia::RuntimePaths paths_;
    sqlite3 *db_ = nullptr;
    sqlite3_stmt *query_statement_ = nullptr;
};
