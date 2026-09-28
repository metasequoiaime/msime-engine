#include "../../shuangpin/engine.h"
#include "../../shuangpin/shuangpin_profile.h"
#include "../../core/data_path.h"
#include "../../core/query_request.h"
#include "../../core/word_item.h"
#include "../../shuangpin/shuangpin_query.h"
#include "test_directory_cleanup.h"

#include <sqlite3.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
class Database
{
  public:
    explicit Database(const std::filesystem::path &path)
    {
        if (sqlite3_open(metasequoia::path_to_utf8(path).c_str(), &database_) != SQLITE_OK)
        {
            throw std::runtime_error("Failed to create the shuangpin test dictionary.");
        }
    }

    ~Database()
    {
        sqlite3_close(database_);
    }

    Database(const Database &) = delete;
    Database &operator=(const Database &) = delete;

    void execute(const char *sql)
    {
        char *error = nullptr;
        if (sqlite3_exec(database_, sql, nullptr, nullptr, &error) != SQLITE_OK)
        {
            const std::string message = error == nullptr ? "SQLite operation failed." : error;
            sqlite3_free(error);
            throw std::runtime_error(message);
        }
    }

  private:
    sqlite3 *database_ = nullptr;
};

void require(bool condition, const std::string &message)
{
    if (!condition)
    {
        throw std::runtime_error(message);
    }
}

void write_file(const std::filesystem::path &path, const std::string &contents)
{
    std::filesystem::create_directories(path.parent_path());
    std::ofstream stream(path);
    stream << contents;
    if (!stream)
    {
        throw std::runtime_error("Failed to prepare a shuangpin helpcode fixture.");
    }
}

std::string describe(const std::vector<WordItem> &candidates)
{
    std::string description;
    for (const auto &candidate : candidates)
    {
        description += " [" + candidate.word + "]";
    }
    return description;
}

std::size_t index_of(const std::vector<WordItem> &candidates, const std::string &word, const std::string &context)
{
    const auto found =
        std::find_if(candidates.begin(), candidates.end(), [&](const WordItem &item) { return item.word == word; });
    if (found == candidates.end())
    {
        throw std::runtime_error(context + " did not return " + word + "; actual:" + describe(candidates));
    }
    return static_cast<std::size_t>(std::distance(candidates.begin(), found));
}

std::vector<std::string> words_of(const std::vector<WordItem> &candidates)
{
    std::vector<std::string> words;
    words.reserve(candidates.size());
    for (const auto &candidate : candidates)
    {
        words.push_back(candidate.word);
    }
    return words;
}

// Each query runs on its own engine so that the shared query caches (series, single-helpcode) of one raw input can
// never answer another.
std::vector<WordItem> query_once(const metasequoia::RuntimePaths &paths, const std::string &raw_input,
                                 bool enable_helpcode)
{
    ShuangpinEngine engine(GetXiaoheShuangpinProfile(), paths);
    QueryRequest request;
    request.scheme = SchemeType::Shuangpin;
    request.raw_input = raw_input;
    request.raw_input_with_cases = raw_input;
    request.enable_shuangpin_helpcode = enable_helpcode;
    request.valid = true;
    return engine.query(request);
}

// Xiaohe reads "ui" as shi, so "uiu" is a complete syllable plus the single auxiliary code 'u', and "ui'u" is the same
// syllable followed by a user-delimited second segment. 是 carries an auxiliary code starting with 'u' and is the
// lighter of the two entries, so auxiliary-code filtering and plain weight ordering disagree about which comes first.
void prepare_fixture(const std::filesystem::path &directory)
{
    std::filesystem::create_directories(directory);
    Database database(directory / "msime.db");
    database.execute("BEGIN;"
                     "CREATE TABLE tbl_1_s(key TEXT, jp TEXT, value TEXT, weight INTEGER);"
                     "INSERT INTO tbl_1_s VALUES('shi', 's', '使', 200);"
                     "INSERT INTO tbl_1_s VALUES('shi', 's', '是', 100);"
                     "COMMIT;");
    // helpcode.txt is the file of the default "lantian" schema, which the dictionary loads from
    // RuntimePaths::resources.
    write_file(directory / "helpcodes" / "helpcode.txt", "使=ab\n是=uc\n");
}

void testManualDelimiterDisablesSingleHelpcode(const metasequoia::RuntimePaths &paths)
{
    const auto undelimited = query_once(paths, "uiu", true);
    require(index_of(undelimited, "是", "Undelimited \"uiu\"") < index_of(undelimited, "使", "Undelimited \"uiu\""),
            "The trailing letter of \"uiu\" stopped acting as a single auxiliary code:" + describe(undelimited));

    const auto delimited = query_once(paths, "ui'u", true);
    require(index_of(delimited, "使", "Delimited \"ui'u\"") < index_of(delimited, "是", "Delimited \"ui'u\""),
            "A manual delimiter before the last letter was ignored and the engine still filtered by the auxiliary code "
            "'u', disagreeing with the composition layer that reads the letter as a pinyin segment:" +
                describe(delimited));

    const auto without_helpcode = query_once(paths, "ui'u", false);
    require(words_of(delimited) == words_of(without_helpcode),
            "A manual delimiter before the last letter left the auxiliary-code setting observable:" +
                describe(delimited) + " vs" + describe(without_helpcode));
}

int run_test()
{
    const auto unique_suffix = std::to_string(std::chrono::high_resolution_clock::now().time_since_epoch().count());
    const std::filesystem::path data_directory =
        std::filesystem::temp_directory_path() / std::filesystem::u8path("metasequoia-shuangpin-" + unique_suffix);
    metasequoia::test::ScopedDataDirectoryCleanup cleanup(data_directory);
    prepare_fixture(data_directory);

    const metasequoia::RuntimePaths paths{data_directory, data_directory, data_directory, data_directory};
    require(shuangpin::is_complete_input("yo"), "The complete Shuangpin syllable yo was rejected.");
    testManualDelimiterDisablesSingleHelpcode(paths);
    return 0;
}
} // namespace

int main()
{
    try
    {
        return run_test();
    }
    catch (const std::exception &exception)
    {
        std::fprintf(stderr, "%s\n", exception.what());
        return 1;
    }
}
