#include "runtime_paths.h"
#include "data_path.h"
#include "../common/sqlite_database.h"
#include "../contracts/assets/assets.h"
#include "../user_dictionary/user_dictionary_journal.h"
#include <algorithm>
#include <fstream>
#include <stdexcept>
#include <sqlite3.h>

namespace metasequoia
{
namespace
{
std::filesystem::path join(const std::filesystem::path &root, const std::filesystem::path &name)
{
    if (root.empty() || name.empty() || name.is_absolute())
        return {};
    for (const auto &part : name)
        if (part == "..")
            throw std::invalid_argument("Runtime asset path escapes its directory");
    return root / name;
}

std::filesystem::path normalized_root(const std::filesystem::path &path)
{
    auto result = std::filesystem::weakly_canonical(path).lexically_normal();
    if (result.filename().empty())
        result = result.parent_path();
    return result;
}

bool roots_overlap(const std::filesystem::path &first, const std::filesystem::path &second)
{
    const auto common = std::mismatch(first.begin(), first.end(), second.begin(), second.end());
    return common.first == first.end() || common.second == second.end();
}

void copy_database(const std::filesystem::path &source, const std::filesystem::path &target)
{
    // SQLite backup includes committed WAL content; copying a live .db alone would lose it.
    sqlite3 *input_raw = nullptr;
    const int input_status = sqlite3_open_v2(path_to_utf8(source).c_str(), &input_raw, SQLITE_OPEN_READONLY, nullptr);
    metasequoia::SqliteDatabase input(input_raw);
    sqlite3 *output_raw = nullptr;
    const int output_status = input_status == SQLITE_OK
                                  ? sqlite3_open_v2(path_to_utf8(target).c_str(), &output_raw,
                                                    SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr)
                                  : SQLITE_ERROR;
    metasequoia::SqliteDatabase output(output_raw);
    const bool opened = input_status == SQLITE_OK && output_status == SQLITE_OK;
    sqlite3_backup *backup = opened ? sqlite3_backup_init(output.get(), "main", input.get(), "main") : nullptr;
    const int copied = backup ? sqlite3_backup_step(backup, -1) : SQLITE_ERROR;
    const int finished = backup ? sqlite3_backup_finish(backup) : SQLITE_ERROR;
    if (copied != SQLITE_DONE || finished != SQLITE_OK)
        throw std::runtime_error("Unable to copy runtime dictionary: " + path_to_utf8(source));
}

// Tables the lattice reads through RuntimePaths::dictionary(). They are built from one dictionary
// generation's vocabulary, so they belong beside that generation rather than in the resource
// directory, and a resource set that carries none simply leaves the decoder without them.
constexpr const char *kGenerationCopies[] = {assets::bigram_table, assets::trigram_table};

// Rename into place rather than writing the target directly: a session may already have the
// previous file mapped, and a half-written table under an mmap is a crash rather than a miss.
void copy_generation_file(const std::filesystem::path &source, const std::filesystem::path &target)
{
    const auto incoming = target.parent_path() / (target.filename().string() + ".incoming");
    std::error_code error;
    std::filesystem::remove(incoming, error);
    std::filesystem::copy_file(source, incoming, std::filesystem::copy_options::overwrite_existing);
    std::filesystem::rename(incoming, target);
}

// Copy the optional tables the resource set carries. Called for a generation being staged and for
// one already prepared: the second case is what stops a table added by a later build from staying
// invisible until the dictionary generation itself changes.
void stage_generation_copies(const std::filesystem::path &resources, const std::filesystem::path &generation,
                             bool replace_existing)
{
    for (const auto *name : kGenerationCopies)
    {
        const auto source = resources / name;
        if (!std::filesystem::is_regular_file(source))
            continue;
        const auto target = generation / name;
        if (!replace_existing && std::filesystem::exists(target))
            continue;
        copy_generation_file(source, target);
    }
}
} // namespace

RuntimePaths RuntimePaths::legacy()
{
    const auto root = data_directory();
    return {root, root, root / "cache", root};
}
std::filesystem::path RuntimePaths::resource(const std::filesystem::path &name) const
{
    return join(resources, name);
}
std::filesystem::path RuntimePaths::user(const std::filesystem::path &name) const
{
    return join(user_data, name);
}
std::filesystem::path RuntimePaths::dictionary(const std::filesystem::path &name) const
{
    return join(dictionaries, name);
}
void RuntimePaths::validate() const
{
    for (const auto &path : {resources, user_data, cache, dictionaries})
        if (!path.is_absolute())
            throw std::invalid_argument("Runtime directories must be absolute");
}

RuntimePaths prepare_runtime_paths(const std::filesystem::path &resources, const std::filesystem::path &user_data,
                                   const std::filesystem::path &cache, const std::string &content_id)
{
    if (content_id.empty() || content_id.size() > 128 ||
        !std::all_of(content_id.begin(), content_id.end(), [](unsigned char ch) {
            return (ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || ch == '-' ||
                   ch == '_';
        }))
        throw std::invalid_argument("Invalid runtime content ID");
    RuntimePaths result{resources, user_data, cache, user_data / "dictionaries" / content_id};
    result.validate();
    // Each root has a different lifetime. Validate before creating anything: clearing cache
    // must not delete durable data, and preparation must not write inside immutable resources.
    const auto resource_root = normalized_root(resources);
    const auto user_root = normalized_root(user_data);
    const auto cache_root = normalized_root(cache);
    if (roots_overlap(resource_root, user_root) || roots_overlap(resource_root, cache_root) ||
        roots_overlap(user_root, cache_root))
        throw std::invalid_argument("Resource, user-data and cache directories must not overlap");
    std::filesystem::create_directories(user_data);
    std::filesystem::create_directories(cache);
    const auto marker = result.dictionaries / ".ready";
    if (std::filesystem::exists(marker))
    {
        for (const auto *name : {assets::main_dictionary, assets::english_dictionary})
            if (!std::filesystem::is_regular_file(result.dictionary(name)))
                throw std::runtime_error("Incomplete runtime dictionary generation");
        // A host can switch back to a previously prepared generation. Replay the current
        // journal again so changes learned on a newer generation survive that switch.
        const auto replay = user_dictionary::replay(path_to_utf8(result.user(assets::user_journal)),
                                                    path_to_utf8(result.dictionary(assets::main_dictionary)),
                                                    path_to_utf8(result.dictionary(assets::english_dictionary)));
        if (replay.failed || !replay.error.empty())
            throw std::runtime_error("Runtime dictionary replay failed: " + replay.error);
        stage_generation_copies(resources, result.dictionaries, false);
        return result;
    }
    const auto stage = result.dictionaries.parent_path() / (content_id + ".incoming");
    std::filesystem::create_directories(stage.parent_path());
    // Exclusive directory creation also prevents overlapping preparation for the same ID.
    if (!std::filesystem::create_directory(stage))
        throw std::runtime_error("Runtime dictionary staging directory already exists");
    try
    {
        for (const auto *name : {assets::main_dictionary, assets::english_dictionary})
            copy_database(resources / name, stage / name);
        stage_generation_copies(resources, stage, true);
        const auto replay = user_dictionary::replay(path_to_utf8(result.user(assets::user_journal)),
                                                    path_to_utf8(stage / assets::main_dictionary),
                                                    path_to_utf8(stage / assets::english_dictionary));
        if (replay.failed || !replay.error.empty())
            throw std::runtime_error("Runtime dictionary replay failed: " + replay.error);
        std::ofstream ready(stage / ".ready");
        ready << content_id << '\n';
        ready.close();
        if (!ready)
            throw std::runtime_error("Cannot finalize runtime dictionary generation");
        std::filesystem::rename(stage, result.dictionaries);
    }
    catch (...)
    {
        std::error_code ignored;
        std::filesystem::remove_all(stage, ignored);
        throw;
    }
    return result;
}
} // namespace metasequoia
