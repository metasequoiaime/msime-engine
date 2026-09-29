#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace japanese
{
struct SentenceCandidate
{
    std::string text;
    std::int64_t cost = 0;
};

struct JapaneseLemma
{
    std::string reading;
    std::string surface;
    std::uint16_t left_id = 0;
    std::uint16_t right_id = 0;
    std::int32_t word_cost = 0;
    std::uint32_t token_id = 0;
};

class JapaneseMatrixSearch;

class JapaneseSentenceDecoder
{
  public:
    explicit JapaneseSentenceDecoder(std::string model_path = {});
    bool ready() const
    {
        return ready_;
    }
    std::vector<JapaneseLemma> ExactLemmas(const std::string &reading, size_t limit = 32) const;
    std::vector<JapaneseLemma> PrefixLemmas(const std::string &reading_prefix, size_t limit = 32) const;
    std::vector<JapaneseLemma> PrefixLemmasContinuing(const std::string &reading_prefix,
                                                      const std::vector<std::string> &next_kana,
                                                      size_t limit = 32) const;
    int ConnectionCost(std::uint16_t right_id, std::uint16_t left_id) const;

  private:
    friend class JapaneseMatrixSearch;

    struct Token
    {
        std::uint32_t reading_offset = 0;
        std::uint32_t surface_offset = 0;
        std::uint16_t reading_length = 0;
        std::uint16_t surface_length = 0;
        std::uint16_t left_id = 0;
        std::uint16_t right_id = 0;
        std::int32_t word_cost = 0;
    };

    bool Load(const std::string &path);
    Token TokenAt(size_t index) const;
    JapaneseLemma MakeLemma(std::uint32_t token_id) const;
    std::string_view Reading(const Token &token) const;
    std::string_view Surface(const Token &token) const;
    size_t LowerBoundReading(std::string_view reading) const;
    bool TokenCheaper(std::uint32_t a, std::uint32_t b) const;
    void KeepBestToken(std::vector<std::uint32_t> &best, std::uint32_t token_id, size_t limit) const;
    void SortTokenIds(std::vector<std::uint32_t> &token_ids) const;
    std::vector<JapaneseLemma> MakeLemmas(const std::vector<std::uint32_t> &token_ids, size_t limit) const;
    std::vector<JapaneseLemma> BestLemmas(const std::vector<std::uint32_t> &token_ids, size_t limit) const;

    bool ready_ = false;
    std::uint32_t connection_size_ = 0;
    // Immutable model bytes: POSIX uses a read-only file mapping; Windows retains one byte buffer.
    std::shared_ptr<const char> model_data_;
    const char *token_data_ = nullptr;
    const char *connection_data_ = nullptr;
    const char *string_data_ = nullptr;
    size_t token_count_ = 0;
    std::unordered_map<std::string, std::vector<std::uint32_t>> short_prefix_index_;
};
} // namespace japanese
