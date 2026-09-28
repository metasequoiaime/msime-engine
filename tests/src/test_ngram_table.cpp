// The packed word-sequence table and the two context terms it adds to the lattice.
//
// The hash is recomputed here rather than reused from the implementation on purpose: the table is written by
// dictionary/makecikudb/ngramdb/build_ngram.py, so the file format is a contract between two programs and a test that
// borrowed the engine's own hash would keep passing if both sides drifted together.

#include "quanpin/ngram_table.h"
#include "quanpin/word_lattice.h"

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <unordered_map>
#include <vector>

namespace
{

int failures = 0;

void check(bool condition, const std::string &what)
{
    if (condition)
        return;
    std::cerr << "FAIL: " << what << '\n';
    ++failures;
}

std::uint64_t hash_words(const std::string &joined)
{
    std::uint64_t digest = 0xCBF29CE484222325ULL;
    for (unsigned char byte : joined)
    {
        digest ^= byte;
        digest *= 0x100000001B3ULL;
    }
    return digest;
}

std::uint64_t hash_pair(const std::string &previous, const std::string &next)
{
    return hash_words(previous + '\0' + next);
}

std::uint64_t hash_triple(const std::string &before, const std::string &previous, const std::string &next)
{
    return hash_words(before + '\0' + previous + '\0' + next);
}

std::filesystem::path scratch_dir()
{
    const auto dir = std::filesystem::temp_directory_path() / "msime-ngram-table-test";
    std::filesystem::create_directories(dir);
    return dir;
}

void write_table(const std::filesystem::path &path, const std::vector<std::pair<std::uint64_t, float>> &entries,
                 const char *magic = "MSNG", std::uint32_t version = 1)
{
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    const std::uint32_t count = static_cast<std::uint32_t>(entries.size());
    const std::uint32_t reserved = 0;
    file.write(magic, 4);
    file.write(reinterpret_cast<const char *>(&version), sizeof(version));
    file.write(reinterpret_cast<const char *>(&count), sizeof(count));
    file.write(reinterpret_cast<const char *>(&reserved), sizeof(reserved));
    for (const auto &entry : entries)
        file.write(reinterpret_cast<const char *>(&entry.first), sizeof(entry.first));
    for (const auto &entry : entries)
        file.write(reinterpret_cast<const char *>(&entry.second), sizeof(entry.second));
}

std::vector<std::pair<std::uint64_t, float>> sorted_entries(std::vector<std::pair<std::uint64_t, float>> entries)
{
    std::sort(entries.begin(), entries.end());
    return entries;
}

void test_lookup()
{
    const auto path = scratch_dir() / "lookup.bin";
    write_table(path, sorted_entries({{hash_pair("配置", "与"), 2.5f}, {hash_pair("\x01", "本仓"), -1.25f}}));

    const auto table = quanpin::NgramTable::open(path);
    check(table != nullptr, "a well-formed table loads");
    if (!table)
        return;
    check(table->size() == 2, "both entries survive the round trip");
    check(table->bonus("配置", "与") > 2.49 && table->bonus("配置", "与") < 2.51, "a stored bonus comes back");
    check(table->bonus(quanpin::NgramTable::sentence_start(), "本仓") < -1.24, "a negative bonus comes back");
    check(table->bonus("配置", "于") == 0.0, "an absent pair scores zero rather than a penalty");
    check(table->bonus("与", "配置") == 0.0, "the pair is ordered, not a set");
    check(table->bonus("配置", "") == 0.0, "an empty second word scores zero");
}

void test_rejects_bad_files()
{
    const auto dir = scratch_dir();
    check(quanpin::NgramTable::open(dir / "absent.bin") == nullptr, "a missing file yields no table");

    const auto wrong_magic = dir / "magic.bin";
    write_table(wrong_magic, sorted_entries({{1, 1.0f}}), "XXXX");
    check(quanpin::NgramTable::open(wrong_magic) == nullptr, "a foreign file is rejected");

    const auto wrong_version = dir / "version.bin";
    write_table(wrong_version, sorted_entries({{1, 1.0f}}), "MSNG", 99);
    check(quanpin::NgramTable::open(wrong_version) == nullptr, "a future version is rejected");

    // Binary search silently returns wrong answers on unsorted keys, so the loader has to refuse the file rather
    // than serve bonuses for pairs that were never in it.
    const auto unsorted = dir / "unsorted.bin";
    write_table(unsorted, {{9, 1.0f}, {2, 2.0f}});
    check(quanpin::NgramTable::open(unsorted) == nullptr, "unsorted keys are rejected");

    const auto truncated = dir / "truncated.bin";
    {
        write_table(truncated, sorted_entries({{1, 1.0f}, {2, 2.0f}}));
        const auto size = std::filesystem::file_size(truncated);
        std::filesystem::resize_file(truncated, size - 6);
    }
    check(quanpin::NgramTable::open(truncated) == nullptr, "a truncated file is rejected");

    const auto empty = dir / "empty.bin";
    write_table(empty, {});
    const auto table = quanpin::NgramTable::open(empty);
    check(table != nullptr && table->size() == 0, "an empty but valid table loads");
    if (table)
        check(table->bonus("配置", "与") == 0.0, "an empty table scores everything zero");
}

quanpin::WordLatticeLookup table_lookup(
    const std::unordered_map<std::string, std::vector<quanpin::LatticeLexeme>> &rows)
{
    return [&rows](const quanpin::Segments &span) {
        std::string key;
        for (std::size_t i = 0; i < span.size(); ++i)
        {
            if (i)
                key.push_back('\'');
            key += span[i];
        }
        const auto found = rows.find(key);
        return found == rows.end() ? std::vector<quanpin::LatticeLexeme>{} : found->second;
    };
}

// The failure the table exists to fix: 于 outweighs 与 as a word, so a path score built only from word frequencies
// picks 配置于权限 over 配置与权限 and no amount of reranking downstream can see the alternative.
void test_lattice_uses_the_transition()
{
    std::unordered_map<std::string, std::vector<quanpin::LatticeLexeme>> rows;
    rows["pei'zhi"] = {{"pei'zhi", "配置", 20000}};
    rows["yu"] = {{"yu", "于", 900000}, {"yu", "与", 300000}};
    rows["quan'xian"] = {{"quan'xian", "权限", 18000}};
    const quanpin::Segments syllables = {"pei", "zhi", "yu", "quan", "xian"};

    const auto without = quanpin::decode_word_lattice(syllables, table_lookup(rows));
    check(!without.empty() && without.front().sentence == "配置于权限",
          "without a table the commoner character wins: got '" +
              (without.empty() ? std::string() : without.front().sentence) + "'");

    const auto path = scratch_dir() / "transition.bin";
    write_table(path, sorted_entries({{hash_pair("配置", "与"), 3.0f}}));
    const auto table = quanpin::NgramTable::open(path);
    check(table != nullptr, "the transition table loads");
    if (!table)
        return;

    quanpin::WordLatticeOptions options;
    options.bigram = table.get();
    const auto with = quanpin::decode_word_lattice(syllables, table_lookup(rows), options);
    check(!with.empty() && with.front().sentence == "配置与权限",
          "one known pair flips the path: got '" + (with.empty() ? std::string() : with.front().sentence) + "'");

    // The weight has to be able to turn the term off again, or a host cannot ship the table and disable it.
    options.bigram_weight = 0.0;
    const auto silenced = quanpin::decode_word_lattice(syllables, table_lookup(rows), options);
    check(!silenced.empty() && silenced.front().sentence == "配置于权限", "weight zero restores the unigram ranking");

    // A pair the corpus never saw must not be penalised into last place: with only 配置-与 known, the paths through
    // 于 keep exactly the scores they had, which is what makes the table safe to ship against a partial corpus.
    check(with.size() > 1, "the alternative path is still generated, not pruned away");
}

// A third word of context cannot be searched inside the beam, so it is applied to the finished paths. This checks
// the two halves of that: the trigram reorders what the search produced, and `emit` keeps the alternatives out of
// the candidate list they exist to improve rather than to fill.
void test_trigram_reorders_the_paths()
{
    std::unordered_map<std::string, std::vector<quanpin::LatticeLexeme>> rows;
    rows["shu'ru"] = {{"shu'ru", "输入", 20000}};
    rows["fa"] = {{"fa", "法", 800000}, {"fa", "发", 900000}};
    const quanpin::Segments syllables = {"shu", "ru", "fa"};

    const auto plain = quanpin::decode_word_lattice(syllables, table_lookup(rows));
    check(plain.size() >= 2, "both readings are decoded");
    check(!plain.empty() && plain.front().sentence == "输入发", "the commoner character leads without a table");

    const auto path = scratch_dir() / "triple.bin";
    const auto &start = quanpin::NgramTable::sentence_start();
    write_table(path, sorted_entries({{hash_triple(start, "输入", "法"), 4.0f}}));
    const auto table = quanpin::NgramTable::open(path);
    check(table != nullptr, "the trigram table loads");
    if (!table)
        return;

    quanpin::WordLatticeOptions options;
    options.trigram = table.get();
    const auto rescored = quanpin::decode_word_lattice(syllables, table_lookup(rows), options);
    check(!rescored.empty() && rescored.front().sentence == "输入法",
          "the trigram reorders the finished paths: got '" +
              (rescored.empty() ? std::string() : rescored.front().sentence) + "'");

    options.trigram_weight = 0.0;
    const auto silenced = quanpin::decode_word_lattice(syllables, table_lookup(rows), options);
    check(!silenced.empty() && silenced.front().sentence == "输入发", "weight zero leaves the search order alone");

    // Six searched, one shown. Without the cap every alternative the trigram needed would also reach the page.
    std::vector<WordItem> candidates;
    options.trigram_weight = 1.0;
    options.nbest = 6;
    options.emit = 1;
    quanpin::merge_lattice_candidates(candidates, syllables, table_lookup(rows), "shurufa", options);
    check(candidates.size() == 1, "emit caps what reaches the candidate list");
    check(!candidates.empty() && candidates.front().word == "输入法", "and what reaches it is the rescored winner");

    // A host that reorders the readings itself asks for all of them. The alternatives are already
    // decoded and rescored either way; emit only decides how many are handed back.
    std::vector<WordItem> every;
    options.emit = 0;
    quanpin::merge_lattice_candidates(every, syllables, table_lookup(rows), "shurufa", options);
    check(every.size() > 1, "emit zero hands back the alternatives, got " + std::to_string(every.size()));
    check(!every.empty() && every.front().word == "输入法", "and the winner still leads them");

    // The option block both dictionaries use is built in one place so they cannot drift apart.
    const auto shown = quanpin::make_sentence_lattice_options(metasequoia::RuntimePaths::legacy());
    check(shown.nbest == 6 && shown.emit == 1, "the default searches six and shows one");
    const auto all = quanpin::make_sentence_lattice_options(metasequoia::RuntimePaths::legacy(), true);
    check(all.nbest == 6 && all.emit == 0, "asking for alternatives searches the same six and shows them");
}

void test_lattice_reranker_sources_one_path_each()
{
    std::unordered_map<std::string, std::vector<quanpin::LatticeLexeme>> rows;
    rows["ni'hao'xue"] = {{"ni'hao'xue", "你好学", 20000}};
    rows["ni'hao"] = {{"ni'hao", "你好", 20000}};
    rows["ni"] = {{"ni", "你", 20000}, {"ni", "泥", 19000}};
    rows["hao"] = {{"hao", "好", 20000}};
    rows["xue"] = {{"xue", "学", 20000}};
    const quanpin::Segments syllables = {"ni", "hao", "xue"};
    std::vector<WordItem> candidates;
    quanpin::WordLatticeOptions options;
    options.nbest = 2;
    options.include_lattice_best = false;
    options.emit = 0;
    const quanpin::SourcedLatticeReranker reranker{[](std::vector<quanpin::LatticePath> &paths) {
                                                       if (paths.size() < 2)
                                                           return false;
                                                       std::reverse(paths.begin(), paths.end());
                                                       return true;
                                                   },
                                                   CandidateSource::NeuralKeyboard};
    quanpin::merge_lattice_candidates(candidates, syllables, table_lookup(rows), "nihaoxue", options, {}, nullptr,
                                      {reranker});
    check(candidates.size() == 1, "a completed reranker contributes one sentence");
    check(!candidates.empty() && candidates.front().source == CandidateSource::NeuralKeyboard,
          "the reranked sentence records its source");
}

} // namespace

int main()
{
    test_lookup();
    test_rejects_bad_files();
    test_lattice_uses_the_transition();
    test_trigram_reorders_the_paths();
    test_lattice_reranker_sources_one_path_each();
    std::filesystem::remove_all(scratch_dir());
    if (failures)
    {
        std::cerr << failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "ngram table ok\n";
    return 0;
}
