#include "../contracts/assets/assets.h"
#include "japanese_sentence_decoder.h"
#include "../common/string_utils.h"
#include "../core/data_path.h"
#include "../shuangpin/shuangpin_utils.h"
#include <algorithm>
#include <cstring>
#include <fstream>
#include <string_view>
#include <unordered_set>
#ifndef _WIN32
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace
{
#pragma pack(push, 1)
struct ModelHeader
{
    char magic[8];
    std::uint32_t version;
    std::uint32_t token_count;
    std::uint32_t connection_size;
    std::uint32_t reserved;
    std::uint64_t token_offset;
    std::uint64_t connection_offset;
    std::uint64_t string_offset;
    std::uint64_t string_size;
};

struct ModelToken
{
    std::uint32_t reading_offset;
    std::uint16_t reading_length;
    std::uint32_t surface_offset;
    std::uint16_t surface_length;
    std::uint16_t left_id;
    std::uint16_t right_id;
    std::int32_t word_cost;
};
#pragma pack(pop)

constexpr char kMagic[8] = {'M', 'S', 'J', 'P', 'D', 'T', '1', '\0'};
constexpr std::int64_t kUnknownKanaCost = 12000;
constexpr size_t kShortPrefixCandidateCount = 64;

size_t FirstUtf8CodePointLength(std::string_view text)
{
    if (text.empty())
        return 0;
    const unsigned char lead = static_cast<unsigned char>(text.front());
    const size_t length = lead < 0x80 ? 1 : (lead >> 5) == 0x6 ? 2 : (lead >> 4) == 0xE ? 3 : 4;
    return (std::min)(length, text.size());
}

} // namespace

namespace japanese
{
JapaneseSentenceDecoder::JapaneseSentenceDecoder(std::string model_path)
{
    if (model_path.empty())
    {
        model_path = metasequoia::path_to_utf8(shuangpin::get_data_file_path(metasequoia::assets::japanese_model));
    }
    ready_ = Load(model_path);
}

bool JapaneseSentenceDecoder::Load(const std::string &path)
{
    std::shared_ptr<const char> storage;
    std::uint64_t size = 0;
#ifdef _WIN32
    std::ifstream stream(metasequoia::path_from_utf8(path.c_str()), std::ios::binary | std::ios::ate);
    const auto end = stream.tellg();
    if (end < static_cast<std::streamoff>(sizeof(ModelHeader)))
        return false;
    size = static_cast<std::uint64_t>(end);
    if (size > static_cast<std::uint64_t>(SIZE_MAX))
        return false;
    auto buffer = std::make_unique<char[]>(static_cast<size_t>(size));
    stream.seekg(0);
    if (!stream.read(buffer.get(), static_cast<std::streamsize>(size)))
        return false;
    storage = std::shared_ptr<const char>(buffer.release(), [](const char *data) { delete[] data; });
#else
    const int descriptor = open(path.c_str(), O_RDONLY);
    if (descriptor < 0)
        return false;
    struct stat status
    {
    };
    if (fstat(descriptor, &status) != 0 || !S_ISREG(status.st_mode) ||
        status.st_size < static_cast<off_t>(sizeof(ModelHeader)))
    {
        close(descriptor);
        return false;
    }
    size = static_cast<std::uint64_t>(status.st_size);
    if (size > static_cast<std::uint64_t>(SIZE_MAX))
    {
        close(descriptor);
        return false;
    }
    void *mapping = mmap(nullptr, static_cast<size_t>(size), PROT_READ, MAP_PRIVATE, descriptor, 0);
    close(descriptor);
    if (mapping == MAP_FAILED)
        return false;
    storage = std::shared_ptr<const char>(static_cast<const char *>(mapping), [size](const char *data) {
        munmap(const_cast<char *>(data), static_cast<size_t>(size));
    });
#endif
    ModelHeader header{};
    std::memcpy(&header, storage.get(), sizeof(header));
    if (std::memcmp(header.magic, kMagic, sizeof(kMagic)) != 0 || header.version != 1 || header.connection_size == 0 ||
        header.token_count > 2000000 || header.string_size > (1ull << 32))
        return false;
    const std::uint64_t connection_count = static_cast<std::uint64_t>(header.connection_size) * header.connection_size;
    const auto contains = [size](std::uint64_t offset, std::uint64_t length) {
        return offset >= sizeof(ModelHeader) && offset <= size && length <= size - offset;
    };
    if (connection_count > 20000000 ||
        !contains(header.token_offset, static_cast<std::uint64_t>(header.token_count) * sizeof(ModelToken)) ||
        !contains(header.connection_offset, connection_count * sizeof(std::int16_t)) ||
        !contains(header.string_offset, header.string_size))
        return false;
    model_data_ = std::move(storage);
    token_data_ = model_data_.get() + header.token_offset;
    connection_data_ = model_data_.get() + header.connection_offset;
    string_data_ = model_data_.get() + header.string_offset;
    token_count_ = header.token_count;
    connection_size_ = header.connection_size;
    for (size_t index = 0; index < token_count_; ++index)
    {
        const auto token = TokenAt(index);
        if (static_cast<std::uint64_t>(token.reading_offset) + token.reading_length > header.string_size ||
            static_cast<std::uint64_t>(token.surface_offset) + token.surface_length > header.string_size ||
            token.left_id >= connection_size_ || token.right_id >= connection_size_)
            return false;
        // The file's reading order is the search index; reject corrupt or unsorted input.
        if (index > 0 && Reading(token) < Reading(TokenAt(index - 1)))
            return false;
    }

    // Pending romaji such as "k" expands to several one-kana prefix queries. Those
    // ranges are by far the largest, so cache their best token IDs while the model is
    // already being warmed in the background. Readings are sorted, making each group
    // contiguous and keeping this pass linear with only a tiny permanent index.
    size_t group_start = 0;
    while (group_start < token_count_)
    {
        const auto first_reading = Reading(TokenAt(group_start));
        const auto prefix_length = FirstUtf8CodePointLength(first_reading);
        if (prefix_length == 0)
            return false;
        const std::string prefix(first_reading.substr(0, prefix_length));
        std::vector<std::uint32_t> best;
        best.reserve(kShortPrefixCandidateCount);
        size_t group_end = group_start;
        for (; group_end < token_count_; ++group_end)
        {
            const auto reading = Reading(TokenAt(group_end));
            if (!CommonUtils::starts_with(reading, prefix))
                break;
            KeepBestToken(best, static_cast<std::uint32_t>(group_end), kShortPrefixCandidateCount);
        }
        SortTokenIds(best);
        short_prefix_index_.emplace(prefix, std::move(best));
        group_start = group_end;
    }
    return token_count_ != 0;
}

JapaneseSentenceDecoder::Token JapaneseSentenceDecoder::TokenAt(size_t index) const
{
    // ModelToken is packed on disk; memcpy also supports unaligned offsets safely.
    ModelToken record{};
    std::memcpy(&record, token_data_ + index * sizeof(record), sizeof(record));
    return {record.reading_offset, record.surface_offset, record.reading_length, record.surface_length,
            record.left_id,        record.right_id,       record.word_cost};
}

int JapaneseSentenceDecoder::ConnectionCost(std::uint16_t right_id, std::uint16_t left_id) const
{
    if (!ready_ || right_id >= connection_size_ || left_id >= connection_size_)
        return 10000;
    std::int16_t cost = 0;
    const auto index = static_cast<size_t>(right_id) * connection_size_ + left_id;
    std::memcpy(&cost, connection_data_ + index * sizeof(cost), sizeof(cost));
    return cost;
}

JapaneseLemma JapaneseSentenceDecoder::MakeLemma(std::uint32_t token_id) const
{
    const Token &token = TokenAt(token_id);
    return {std::string(Reading(token)),
            std::string(Surface(token)),
            token.left_id,
            token.right_id,
            token.word_cost,
            token_id};
}

std::string_view JapaneseSentenceDecoder::Reading(const Token &token) const
{
    return {string_data_ + token.reading_offset, token.reading_length};
}

std::string_view JapaneseSentenceDecoder::Surface(const Token &token) const
{
    return {string_data_ + token.surface_offset, token.surface_length};
}

size_t JapaneseSentenceDecoder::LowerBoundReading(std::string_view reading) const
{
    size_t first = 0, last = token_count_;
    while (first < last)
    {
        const auto middle = first + (last - first) / 2;
        if (Reading(TokenAt(middle)) < reading)
            first = middle + 1;
        else
            last = middle;
    }
    return first;
}

bool JapaneseSentenceDecoder::TokenCheaper(std::uint32_t a, std::uint32_t b) const
{
    if (TokenAt(a).word_cost != TokenAt(b).word_cost)
        return TokenAt(a).word_cost < TokenAt(b).word_cost;
    return a < b;
}

void JapaneseSentenceDecoder::KeepBestToken(std::vector<std::uint32_t> &best, std::uint32_t token_id,
                                            size_t limit) const
{
    const auto cheaper = [this](std::uint32_t a, std::uint32_t b) { return TokenCheaper(a, b); };
    if (best.size() < limit)
    {
        best.push_back(token_id);
        if (best.size() == limit)
            std::make_heap(best.begin(), best.end(), cheaper);
    }
    else if (TokenCheaper(token_id, best.front()))
    {
        std::pop_heap(best.begin(), best.end(), cheaper);
        best.back() = token_id;
        std::push_heap(best.begin(), best.end(), cheaper);
    }
}

void JapaneseSentenceDecoder::SortTokenIds(std::vector<std::uint32_t> &token_ids) const
{
    std::sort(token_ids.begin(), token_ids.end(),
              [this](std::uint32_t a, std::uint32_t b) { return TokenCheaper(a, b); });
}

std::vector<JapaneseLemma> JapaneseSentenceDecoder::MakeLemmas(const std::vector<std::uint32_t> &token_ids,
                                                               size_t limit) const
{
    std::vector<JapaneseLemma> result;
    const size_t count = (std::min)(limit, token_ids.size());
    result.reserve(count);
    for (size_t index = 0; index < count; ++index)
        result.push_back(MakeLemma(token_ids[index]));
    return result;
}

std::vector<JapaneseLemma> JapaneseSentenceDecoder::BestLemmas(const std::vector<std::uint32_t> &token_ids,
                                                               size_t limit) const
{
    std::vector<std::uint32_t> best;
    best.reserve((std::min)(limit, token_ids.size()));
    for (const auto token_id : token_ids)
        KeepBestToken(best, token_id, limit);
    SortTokenIds(best);
    return MakeLemmas(best, limit);
}

std::vector<JapaneseLemma> JapaneseSentenceDecoder::ExactLemmas(const std::string &reading, size_t limit) const
{
    if (!ready_ || reading.empty() || limit == 0)
        return {};
    std::vector<std::uint32_t> matches;
    for (size_t index = LowerBoundReading(reading); index < token_count_; ++index)
    {
        if (Reading(TokenAt(index)) != reading)
            break;
        matches.push_back(static_cast<std::uint32_t>(index));
    }
    return BestLemmas(matches, limit);
}

std::vector<JapaneseLemma> JapaneseSentenceDecoder::PrefixLemmas(const std::string &reading_prefix, size_t limit) const
{
    if (!ready_ || reading_prefix.empty() || limit == 0)
        return {};
    const auto cached = short_prefix_index_.find(reading_prefix);
    if (cached != short_prefix_index_.end() && limit <= kShortPrefixCandidateCount)
        return MakeLemmas(cached->second, limit);

    std::vector<std::uint32_t> matches;
    for (size_t index = LowerBoundReading(reading_prefix); index < token_count_; ++index)
    {
        const auto token_reading = Reading(TokenAt(index));
        if (!CommonUtils::starts_with(token_reading, reading_prefix))
            break;
        matches.push_back(static_cast<std::uint32_t>(index));
    }
    return BestLemmas(matches, limit);
}

std::vector<JapaneseLemma> JapaneseSentenceDecoder::PrefixLemmasContinuing(const std::string &reading_prefix,
                                                                           const std::vector<std::string> &next_kana,
                                                                           size_t limit) const
{
    if (!ready_ || reading_prefix.empty() || next_kana.empty() || limit == 0)
        return {};
    std::vector<std::uint32_t> matches;
    for (size_t index = LowerBoundReading(reading_prefix); index < token_count_; ++index)
    {
        const auto token_reading = Reading(TokenAt(index));
        if (!CommonUtils::starts_with(token_reading, reading_prefix) || token_reading.size() <= reading_prefix.size())
            break;
        const std::string_view remaining(token_reading.data() + reading_prefix.size(),
                                         token_reading.size() - reading_prefix.size());
        bool matched = false;
        for (const auto &kana : next_kana)
        {
            if (remaining.size() >= kana.size() && remaining.compare(0, kana.size(), kana) == 0)
            {
                matched = true;
                break;
            }
        }
        if (matched)
            matches.push_back(static_cast<std::uint32_t>(index));
    }
    return BestLemmas(matches, limit);
}

std::vector<SentenceCandidate> JapaneseSentenceDecoder::Decode(const std::string &reading, size_t limit) const
{
    if (!ready_ || reading.empty() || limit == 0)
        return {};
    struct Path
    {
        std::string text;
        std::int64_t cost;
        std::uint16_t right_id;
    };
    const auto boundaries = CommonUtils::utf8_boundaries(reading);
    std::vector<std::vector<Path>> paths(reading.size() + 1);
    paths[0].push_back({{}, 0, 0});
    const size_t beam = (std::max)(size_t{16}, limit * 4);

    for (size_t boundary_index = 0; boundary_index + 1 < boundaries.size(); ++boundary_index)
    {
        const size_t start = boundaries[boundary_index];
        if (paths[start].empty())
            continue;
        for (size_t end_index = boundary_index + 1; end_index < boundaries.size(); ++end_index)
        {
            const size_t end = boundaries[end_index];
            const std::string key = reading.substr(start, end - start);
            const auto lemmas = ExactLemmas(key, 24);
            for (const auto &lemma : lemmas)
            {
                for (const auto &previous : paths[start])
                {
                    paths[end].push_back(
                        {previous.text + lemma.surface,
                         previous.cost + lemma.word_cost + ConnectionCost(previous.right_id, lemma.left_id),
                         lemma.right_id});
                }
            }
        }

        const size_t next = boundaries[boundary_index + 1];
        const std::string kana = reading.substr(start, next - start);
        for (const auto &previous : paths[start])
            paths[next].push_back({previous.text + kana, previous.cost + kUnknownKanaCost, 0});

        for (size_t end_index = boundary_index + 1; end_index < boundaries.size(); ++end_index)
        {
            auto &bucket = paths[boundaries[end_index]];
            if (bucket.size() > beam)
            {
                std::partial_sort(bucket.begin(), bucket.begin() + beam, bucket.end(),
                                  [](const Path &a, const Path &b) { return a.cost < b.cost; });
                bucket.resize(beam);
            }
        }
    }

    auto finals = std::move(paths[reading.size()]);
    for (auto &path : finals)
        path.cost += ConnectionCost(path.right_id, 0);
    std::sort(finals.begin(), finals.end(), [](const Path &a, const Path &b) { return a.cost < b.cost; });
    std::vector<SentenceCandidate> result;
    std::unordered_set<std::string> seen;
    for (auto &path : finals)
    {
        if (seen.insert(path.text).second)
            result.push_back({std::move(path.text), path.cost});
        if (result.size() == limit)
            break;
    }
    return result;
}
} // namespace japanese
