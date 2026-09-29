#include "core/data_path.h"
#include "core/input_session.h"
#include "english/english_dictionary.h"
#include "local_modes/emoji_query.h"
#include "local_modes/quick_phrase_query.h"
#include "user_dictionary/user_dictionary_journal.h"
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <stdexcept>

void require(bool condition, const char *message)
{
    if (!condition)
        throw std::runtime_error(message);
}

bool contains(const std::vector<WordItem> &items, const std::string &word)
{
    return std::any_of(items.begin(), items.end(), [&](const auto &item) { return item.word == word; });
}

int main(int argc, char **argv)
{
    if (argc != 3)
        return 2;
    const auto temporary =
        std::filesystem::temp_directory_path() /
        ("msime-dictionary-consumer-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    int status = 0;
    try
    {
        const auto source = std::filesystem::u8path(argv[1]);
        const bool desktop = std::string(argv[2]) == "desktop";
        std::filesystem::create_directories(temporary);
        // Test writes/replay against an isolated copy of the actual produced
        // database.
        std::filesystem::copy_file(source / "msime.db", temporary / "msime.db");
        if (desktop)
            for (const char *name : {"english.db", "others.db"})
                std::filesystem::copy_file(source / name, temporary / name);
#ifdef _WIN32
        require(_wputenv_s(L"METASEQUOIA_IME_DATA_DIR", temporary.c_str()) == 0, "Cannot set data directory");
#else
        require(setenv("METASEQUOIA_IME_DATA_DIR", temporary.c_str(), 1) == 0, "Cannot set data directory");
#endif
        {
            metasequoia::InputSession session(SchemeType::Quanpin, true, false, true, false);
            for (char ch : std::string("nihao"))
                session.handle_character(ch);
            require(contains(session.candidates(), "你好"), "Produced pinyin dictionary cannot be queried by Engine");
            const auto destination = metasequoia::path_to_utf8(temporary / "msime.db");
            const auto journal = metasequoia::path_to_utf8(temporary / "replay-journal.db");
            for (int syllables : {7, 8, 9})
            {
                std::string key, word;
                for (int i = 0; i < syllables; ++i)
                {
                    if (i)
                        key += "'";
                    key += "ni";
                    word += "你";
                }
                require(session.store_user_phrase_from_canonical_pinyin(key, word) == 0,
                        "Produced schema cannot store a boundary-length phrase");
                session.reset_state();
                session.reset_cache();
                for (char ch : key)
                    session.handle_character(ch);
                require(contains(session.candidates(), word), "Stored phrase cannot be read through Engine");
                require(user_dictionary::record_user_insert(journal, user_dictionary::DictionaryKind::Pinyin, key, word,
                                                            10000),
                        "Cannot record phrase for replay");
            }
            const auto replay = user_dictionary::replay(journal, destination,
                                                        metasequoia::path_to_utf8(temporary / "replay-english.db"));
            require(replay.failed == 0 && replay.applied == 3, "Produced schema cannot replay boundary-length phrases");
            if (desktop)
            {
                EnglishDictionary english(metasequoia::path_to_utf8(temporary / "english.db"), false);
                require(!english.query_prefix("hello").empty(),
                        "Desktop product lacks the shared English query schema");
                require(contains(metasequoia::local_modes::query_quick_phrases("yyds").candidates, "永远滴神"),
                        "Desktop product lacks the shared quick-phrase behavior");
                require(
                    contains(metasequoia::local_modes::query_emoji("xiaolian", SchemeType::Quanpin).candidates, "😀"),
                    "Desktop product lacks the shared emoji behavior");
            }
        }
        std::cout << argv[2] << ": real product query, phrase creation and replay passed\n";
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        status = 1;
    }
    user_dictionary::close_default_user_database();
    std::filesystem::remove_all(temporary);
    return status;
}
