#include "../../core/data_path.h"
#include "../../core/input_session.h"
#include "../../quanpin/quanpin_utils.h"
#include "test_directory_cleanup.h"

#include <sqlite3.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <stdexcept>
#include <string>

namespace
{
class Database
{
  public:
    explicit Database(const std::filesystem::path &path)
    {
        if (sqlite3_open(metasequoia::path_to_utf8(path).c_str(), &database_) != SQLITE_OK)
        {
            throw std::runtime_error("Failed to create the online-candidate test dictionary.");
        }
    }

    ~Database()
    {
        sqlite3_close(database_);
    }

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

void require(bool condition, const char *message)
{
    if (!condition)
    {
        throw std::runtime_error(message);
    }
}

void set_data_directory(const std::filesystem::path &directory)
{
#ifdef _WIN32
    if (_wputenv_s(L"METASEQUOIA_IME_DATA_DIR", directory.c_str()) != 0)
#else
    if (setenv("METASEQUOIA_IME_DATA_DIR", metasequoia::path_to_utf8(directory).c_str(), 1) != 0)
#endif
    {
        throw std::runtime_error("Failed to set the online-candidate test data directory.");
    }
}

void type(metasequoia::InputSession &session, const std::string &text)
{
    for (const char character : text)
    {
        require(session.handle_character(character).handled, "An online-candidate test character was not handled.");
    }
}

bool has_word(const metasequoia::InputSession &session, const std::string &word)
{
    return std::any_of(session.candidates().begin(), session.candidates().end(),
                       [&](const WordItem &candidate) { return candidate.word == word; });
}
} // namespace

int main()
{
    try
    {
        const auto suffix = std::to_string(std::chrono::high_resolution_clock::now().time_since_epoch().count());
        const std::filesystem::path root =
            std::filesystem::temp_directory_path() / ("metasequoia-online-candidate-" + suffix);
        metasequoia::test::ScopedDataDirectoryCleanup cleanup(root);
        std::filesystem::create_directories(root);
        {
            Database database(root / "msime.db");
            database.execute("BEGIN;"
                             "CREATE TABLE tbl_1_a(key TEXT,jp TEXT,value TEXT,weight INTEGER);"
                             "INSERT INTO tbl_1_a VALUES('ang','a','昂',200);"
                             "CREATE TABLE tbl_1_n(key TEXT,jp TEXT,value TEXT,weight INTEGER);"
                             "INSERT INTO tbl_1_n VALUES('ni','n','你',200);"
                             "INSERT INTO tbl_1_n VALUES('ni','n','倪',100);"
                             "CREATE TABLE tbl_2_n(key TEXT,jp TEXT,value TEXT,weight INTEGER);"
                             "INSERT INTO tbl_2_n VALUES('ni''hao','nh','你好',200);"
                             "COMMIT;");
        }
        {
            Database database(root / "english.db");
            database.execute("CREATE TABLE english_words(word TEXT COLLATE BINARY NOT NULL,display TEXT NOT NULL,"
                             "weight INTEGER NOT NULL DEFAULT 0,PRIMARY KEY(word,display)) WITHOUT ROWID;"
                             "INSERT INTO english_words VALUES('ninja','Ninja',100);");
        }
        set_data_directory(root);

        metasequoia::InputSession quanpin(SchemeType::Quanpin);
        type(quanpin, "ni");
        const auto query = quanpin.online_query();
        require(query.has_value(), "Quanpin did not expose an online query.");
        require(query->scheme == SchemeType::Quanpin && query->query_text == "ni" && query->cache_key == "ni" &&
                    query->pinyin_segments == std::vector<std::string>{"ni"} && query->cloud_eligible &&
                    query->ai_eligible && !query->identity.empty(),
                "Quanpin exposed the wrong online query state.");

        metasequoia::InputSession manually_segmented(SchemeType::Quanpin);
        type(manually_segmented, "qi'e'huan");
        const auto manually_segmented_query = manually_segmented.online_query();
        require(manually_segmented_query.has_value() && manually_segmented_query->query_text == "qi'e'huan",
                "Quanpin cloud queries dropped the user's manual syllable boundaries.");
        require(quanpin::to_google_spelling("nve'dai'lve") == "nue'dai'lue",
                "Google spelling conversion did not preserve segments while rewriting v spellings.");

        require(quanpin.apply_online_candidate(*query, "泥", CandidateSource::CloudSuggestion),
                "A current cloud candidate was rejected.");
        require(quanpin.candidates().size() >= 3 && quanpin.candidates()[1].word == "泥" &&
                    quanpin.candidates()[1].source == CandidateSource::CloudSuggestion,
                "The cloud candidate was not inserted in slot two.");
        require(quanpin.apply_online_candidate(*query, "拟", CandidateSource::AiSuggestion),
                "A current AI candidate was rejected.");
        require(quanpin.candidates().size() >= 4 && quanpin.candidates()[2].word == "拟" &&
                    quanpin.candidates()[2].source == CandidateSource::AiSuggestion,
                "The AI candidate was not inserted in slot three.");

        require(quanpin.apply_online_candidate(*query, "呢", CandidateSource::CloudSuggestion),
                "A replacement cloud candidate was rejected.");
        require(quanpin.candidates()[1].word == "呢" && !has_word(quanpin, "泥"),
                "Replacing a cloud candidate left the previous cloud row visible.");
        const auto candidate_count = quanpin.candidates().size();
        require(!quanpin.apply_online_candidate(*query, "你", CandidateSource::CloudSuggestion) &&
                    quanpin.candidates().size() == candidate_count,
                "A duplicate local word was treated as a new cloud candidate.");
        require(!quanpin.apply_online_candidate(*query, "bad", CandidateSource::Database),
                "An unsupported online candidate source was accepted.");
        auto wrong_query = *query;
        wrong_query.identity += "-stale";
        require(!quanpin.apply_online_candidate(wrong_query, "旧", CandidateSource::CloudSuggestion),
                "A mismatched query identity was accepted.");
        type(quanpin, "h");
        require(!quanpin.apply_online_candidate(*query, "迟", CandidateSource::CloudSuggestion),
                "A stale query was accepted after composition changed.");

        metasequoia::InputSession repeated(SchemeType::Quanpin);
        type(repeated, "ni");
        const auto first_lifetime = repeated.online_query();
        require(first_lifetime.has_value(), "The first repeated composition did not expose an online query.");
        require(repeated.handle_command(metasequoia::Command::Cancel).handled,
                "The first repeated composition was not cancelled.");
        type(repeated, "ni");
        const auto second_lifetime = repeated.online_query();
        require(second_lifetime.has_value() &&
                    repeated.apply_online_candidate(*second_lifetime, "新", CandidateSource::CloudSuggestion),
                "The current result for a repeated composition was rejected.");
        require(!repeated.apply_online_candidate(*first_lifetime, "旧", CandidateSource::CloudSuggestion) &&
                    has_word(repeated, "新") && !has_word(repeated, "旧"),
                "A stale result from an earlier identical composition replaced the current result.");

        metasequoia::InputSession autocorrected(SchemeType::Quanpin);
        autocorrected.set_quanpin_autocorrect_types(quanpin::kAutocorrectNeighbor);
        type(autocorrected, "abg");
        const auto autocorrected_query = autocorrected.online_query();
        require(autocorrected_query.has_value(), "Autocorrected Quanpin did not expose an online query.");
        require(autocorrected.apply_online_candidate(*autocorrected_query, "盎", CandidateSource::CloudSuggestion),
                "A cloud candidate for autocorrected Quanpin was rejected.");
        require(autocorrected.candidates().size() >= 2 && autocorrected.candidates()[0].word == "昂" &&
                    autocorrected.candidates()[1].word == "盎" &&
                    autocorrected.candidates()[1].source == CandidateSource::CloudSuggestion,
                "The cloud candidate was not inserted into the autocorrected Quanpin cache.");
        require(autocorrected.handle_command(metasequoia::Command::Cancel).handled,
                "Autocorrected Quanpin composition was not cancelled.");
        type(autocorrected, "ang");
        require(!has_word(autocorrected, "盎"),
                "An autocorrected online candidate leaked into the plain Quanpin cache.");

        metasequoia::InputSession mixed(SchemeType::Quanpin);
        metasequoia::EnglishInputOptions english_options;
        english_options.mixed_candidates = true;
        require(mixed.set_english_input_options(english_options), "Mixed English options were rejected.");
        type(mixed, "ni");
        const auto mixed_query = mixed.online_query();
        require(mixed_query.has_value() &&
                    mixed.apply_online_candidate(*mixed_query, "泥", CandidateSource::CloudSuggestion) &&
                    mixed.apply_online_candidate(*mixed_query, "拟", CandidateSource::AiSuggestion),
                "Online candidates were not applied with mixed local sources enabled.");
        require(mixed.candidates().size() >= 4 && mixed.candidates()[0].word == "你" &&
                    mixed.candidates()[1].source == CandidateSource::CloudSuggestion &&
                    mixed.candidates()[2].source == CandidateSource::AiSuggestion &&
                    mixed.candidates()[3].source == CandidateSource::EnglishDictionary,
                "Mixed local sources did not follow cloud and AI priority slots.");

        metasequoia::InputSession mixed_ai_only(SchemeType::Quanpin);
        require(mixed_ai_only.set_english_input_options(english_options), "Mixed English options were rejected.");
        type(mixed_ai_only, "ni");
        const auto mixed_ai_query = mixed_ai_only.online_query();
        require(mixed_ai_query.has_value() &&
                    mixed_ai_only.apply_online_candidate(*mixed_ai_query, "拟", CandidateSource::AiSuggestion),
                "An AI-only candidate was not applied with mixed English enabled.");
        require(mixed_ai_only.candidates().size() >= 4 &&
                    mixed_ai_only.candidates()[2].source == CandidateSource::AiSuggestion &&
                    mixed_ai_only.candidates()[3].source == CandidateSource::EnglishDictionary,
                "Mixed English displaced an AI-only suggestion from slot three.");
        require(mixed_ai_only.apply_online_candidate(*mixed_ai_query, "泥", CandidateSource::CloudSuggestion),
                "A cloud candidate was rejected after an AI suggestion.");
        require(mixed_ai_only.candidates().size() >= 4 &&
                    mixed_ai_only.candidates()[1].source == CandidateSource::CloudSuggestion &&
                    mixed_ai_only.candidates()[2].source == CandidateSource::AiSuggestion &&
                    mixed_ai_only.candidates()[3].source == CandidateSource::EnglishDictionary,
                "Cloud arrival moved an existing AI suggestion out of slot three.");

        metasequoia::InputSession shuangpin(SchemeType::Shuangpin);
        type(shuangpin, "ni");
        const auto shuangpin_query = shuangpin.online_query();
        require(shuangpin_query.has_value() && shuangpin_query->scheme == SchemeType::Shuangpin &&
                    !shuangpin_query->query_text.empty() && shuangpin_query->cloud_eligible &&
                    shuangpin_query->ai_eligible,
                "Complete Shuangpin did not expose an online query.");
        require(shuangpin.apply_online_candidate(*shuangpin_query, "拟", CandidateSource::AiSuggestion) &&
                    shuangpin.apply_online_candidate(*shuangpin_query, "泥", CandidateSource::CloudSuggestion),
                "Shuangpin did not accept AI-then-cloud candidates.");
        require(shuangpin.candidates().size() >= 3 &&
                    shuangpin.candidates()[1].source == CandidateSource::CloudSuggestion &&
                    shuangpin.candidates()[2].source == CandidateSource::AiSuggestion,
                "Cloud arrival moved the Shuangpin AI suggestion out of slot three.");
        require(shuangpin.handle_character('A').handled && !shuangpin.online_query().has_value(),
                "Active Shuangpin helpcode input was exposed to online providers.");

        metasequoia::InputSession japanese(SchemeType::JapaneseRomaji);
        type(japanese, "ka");
        const auto japanese_query = japanese.online_query();
        require(japanese_query.has_value() && japanese_query->query_text == "ka" && japanese_query->cloud_eligible &&
                    !japanese_query->ai_eligible,
                "Japanese did not expose a cloud-only online query.");
        require(japanese.apply_online_candidate(*japanese_query, "蚊", CandidateSource::CloudSuggestion),
                "A current Japanese cloud candidate was rejected.");
        require(japanese.candidates().size() >= 3 && japanese.candidates()[2].word == "蚊" &&
                    japanese.candidates()[2].source == CandidateSource::CloudSuggestion,
                "A Japanese cloud candidate displaced the hiragana/katakana pair.");
        require(!japanese.apply_online_candidate(*japanese_query, "AI", CandidateSource::AiSuggestion),
                "Japanese accepted an AI suggestion despite being cloud-only.");

        metasequoia::InputSession wubi(SchemeType::Wubi);
        type(wubi, "aaaa");
        require(!wubi.online_query().has_value(), "Wubi unexpectedly exposed an online query.");

        metasequoia::InputSession local_mode(SchemeType::Quanpin);
        require(local_mode.handle_character('U', true).handled && !local_mode.online_query().has_value(),
                "A local input mode unexpectedly exposed an online query.");

        metasequoia::InputSession dedicated(SchemeType::Quanpin);
        dedicated.set_dedicated_english_mode(true);
        type(dedicated, "word");
        require(!dedicated.online_query().has_value(), "Dedicated English unexpectedly exposed an online query.");
    }
    catch (const std::exception &error)
    {
        std::fprintf(stderr, "Test failure: %s\n", error.what());
        return 1;
    }
    return 0;
}
