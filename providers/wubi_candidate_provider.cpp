#include "wubi_candidate_provider.h"
#include "../core/candidate_utils.h"
#include "../common/sqlite_statement.h"
#include "../contracts/assets/assets.h"
#include "../core/data_path.h"
#include "../quanpin/quanpin_query.h"
#include "../user_dictionary/user_dictionary_journal.h"
#include <spdlog/spdlog.h>
#include <unordered_set>
#include <utility>

namespace
{
constexpr int kNoMutation = 0;
// A one-letter code prefixes several thousand rows, and the window pages through whatever this
// returns. Long enough to page through, short enough to build on every keystroke; a user who wants
// what lies past it types another letter, which is what the remaining letters of the code are for.
constexpr int kMaxCandidates = 200;

// Wubi codes are lowercase letters, all of which sort below '{', so the range covers exactly the
// keys carrying the typed prefix.
std::string prefix_upper_bound(const std::string &prefix)
{
    return prefix + "{";
}
} // namespace

WubiCandidateProvider::WubiCandidateProvider(std::string db_path, metasequoia::RuntimePaths paths)
    : db_path_(db_path.empty() ? quanpin::get_default_db_path() : std::move(db_path)), paths_(std::move(paths))
{
}

WubiCandidateProvider::~WubiCandidateProvider()
{
    close_database();
}

std::vector<WordItem> WubiCandidateProvider::query(const QueryRequest &request)
{
    if (!request.valid || request.scheme != SchemeType::Wubi || request.normalized_input.empty() ||
        !ensure_query_statement())
    {
        return {};
    }

    sqlite3_reset(query_statement_.get());
    sqlite3_reset(wildcard_statement_.get());
    if (request.wubi_z_wildcard)
    {
        std::string pattern;
        pattern.reserve(request.normalized_input.size() + 1);
        for (char ch : request.normalized_input)
            pattern.push_back(ch == 'z' ? '?' : ch);
        pattern.push_back('*');
        if (sqlite3_bind_text(wildcard_statement_.get(), 1, pattern.c_str(), -1, SQLITE_TRANSIENT) != SQLITE_OK)
            return {};
        return collect_rows(wildcard_statement_.get());
    }
    const std::string upper_bound = prefix_upper_bound(request.normalized_input);
    sqlite3_clear_bindings(query_statement_.get());
    if (!metasequoia::sqlite_bind_text_range_limit(query_statement_.get(), request.normalized_input, upper_bound,
                                                   kMaxCandidates))
    {
        return {};
    }

    return collect_rows(query_statement_.get());
}

std::vector<WordItem> WubiCandidateProvider::collect_rows(sqlite3_stmt *statement)
{
    std::vector<WordItem> candidates;
    // The same word reaches the list under several codes -- 工 sits at a, aaa and aaaa -- and the
    // shortest one comes first, so the first spelling seen is the one to keep. Ranking and removal
    // downstream act on the key the candidate arrived with, which is why the row is kept whole
    // rather than rewritten to the typed prefix.
    std::unordered_set<std::string> seen;
    int result = SQLITE_ROW;
    while ((result = sqlite3_step(statement)) == SQLITE_ROW)
    {
        const auto *key = reinterpret_cast<const char *>(sqlite3_column_text(statement, 0));
        const auto *value = reinterpret_cast<const char *>(sqlite3_column_text(statement, 1));
        if (key == nullptr || value == nullptr)
        {
            continue;
        }
        if (append_unique_candidate(candidates, seen, key, value, sqlite3_column_int64(statement, 2),
                                    CandidateSource::Database, {}))
        {
            candidates.back().scheme = SchemeType::Wubi;
        }
    }

    if (result != SQLITE_DONE)
    {
        (void)0;
        return {};
    }
    return candidates;
}

void WubiCandidateProvider::reset_cache()
{
    close_database();
}

int WubiCandidateProvider::create_word(SchemeType, std::string, std::string)
{
    return kNoMutation;
}

int WubiCandidateProvider::update_weight_by_pinyin_and_word(SchemeType, std::string code, std::string word)
{
    if (!user_dictionary::bump_wubi_weight(db_path_, journal_db_path(), code, word))
    {
        return -1;
    }
    reset_cache();
    return kNoMutation;
}

int WubiCandidateProvider::delete_by_pinyin_and_word(SchemeType, std::string code, std::string word)
{
    if (!user_dictionary::delete_dictionary_candidate(db_path_, journal_db_path(),
                                                      user_dictionary::DictionaryKind::Wubi, code, word))
    {
        return -1;
    }
    reset_cache();
    return kNoMutation;
}

int WubiCandidateProvider::cache_dynamic_candidate(SchemeType, const std::string &, const std::string &,
                                                   CandidateSource)
{
    return kNoMutation;
}

int WubiCandidateProvider::cache_dynamic_candidate_for_request(const QueryRequest &, const std::string &,
                                                               CandidateSource)
{
    return kNoMutation;
}

bool WubiCandidateProvider::ensure_query_statement()
{
    if (query_statement_ != nullptr && wildcard_statement_ != nullptr)
    {
        return true;
    }

    if (!db_)
    {
        auto opened = metasequoia::sqlite_open_database(db_path_, SQLITE_OPEN_READONLY);
        if (!opened)
        {
            close_database();
            return false;
        }
        db_ = std::move(opened);
    }

    // An unfinished code is a prefix of the codes it can still become, so it answers with all of
    // them: a table matched on the code alone leaves 你 as the only candidate for wq and the two or
    // three simplified codes as the whole list for most of the alphabet. Shorter codes first, since
    // a code that is already complete is the one being typed; the typed code itself is the shortest
    // match there is and stays at the head of the list.
    constexpr const char *query_sql = "SELECT \"key\", \"value\", \"weight\" FROM wubi86 "
                                      "WHERE \"key\" >= ?1 AND \"key\" < ?2 "
                                      "ORDER BY length(\"key\") ASC, \"weight\" DESC, rowid ASC LIMIT ?3";
    auto query = metasequoia::sqlite_prepare_statement(db_.get(), query_sql);
    if (!query)
    {
        (void)0;
        close_database();
        return false;
    }
    query_statement_ = std::move(query);
    const std::string wildcard_sql = "SELECT \"key\", \"value\", \"weight\" FROM wubi86 WHERE \"key\" GLOB ?1 "
                                     "ORDER BY \"weight\" DESC, \"key\" ASC, rowid ASC LIMIT " +
                                     std::to_string(kMaxCandidates);
    auto wildcard = metasequoia::sqlite_prepare_statement(db_.get(), wildcard_sql);
    if (!wildcard)
    {
        close_database();
        return false;
    }
    wildcard_statement_ = std::move(wildcard);
    return true;
}

std::string WubiCandidateProvider::journal_db_path() const
{
    return metasequoia::path_to_utf8(paths_.user(metasequoia::assets::user_journal));
}

void WubiCandidateProvider::close_database()
{
    query_statement_.reset();
    wildcard_statement_.reset();
    db_.reset();
}
