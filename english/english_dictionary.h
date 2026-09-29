#pragma once

#include "../common/sqlite_database.h"
#include "../common/sqlite_statement.h"
#include "../core/word_item.h"
#include <cstddef>
#include <sqlite3.h>
#include <string>
#include <unordered_map>
#include <vector>

class EnglishDictionary
{
  public:
    explicit EnglishDictionary(std::string db_path, bool initialize_schema = true, std::string translations_path = {},
                               std::string gloss_cache_path = {});
    ~EnglishDictionary();

    EnglishDictionary(const EnglishDictionary &) = delete;
    EnglishDictionary &operator=(const EnglishDictionary &) = delete;

    std::vector<WordItem> query_prefix(const std::string &prefix, size_t limit = 5);
    std::string query_chinese_gloss(const std::string &english);
    std::string query_english_gloss(const std::string &chinese);
    // 记下一条出货词库里没有的释义。写的是独立的用户文件,不是 english.db —— 换一代词库时
    // english.db 会从资源目录重新拷贝、只回放 msime_user.db,写进去的释义会被静默清掉。
    bool cache_gloss(bool chinese_to_english, const std::string &key, const std::string &gloss);

    static bool ensure_schema(const std::string &db_path);
    static bool upsert_gloss(const std::string &db_path, bool chinese_to_english, const std::string &key,
                             const std::string &gloss);

  private:
    bool ensure_query_statement();
    bool ensure_gloss_statements();
    bool ensure_cache_statements();
    void load_custom_translations();
    std::string translations_path_;
    void close_database();
    void close_cache();

  private:
    std::string db_path_;
    std::string gloss_cache_path_;
    metasequoia::SqliteDatabase db_;
    metasequoia::SqliteDatabase cache_db_;
    metasequoia::SqliteStatement query_statement_;
    metasequoia::SqliteStatement en_zh_statement_;
    metasequoia::SqliteStatement zh_en_statement_;
    metasequoia::SqliteStatement cache_en_zh_statement_;
    metasequoia::SqliteStatement cache_zh_en_statement_;
    std::unordered_map<std::string, std::string> custom_en_zh_;
    std::unordered_map<std::string, std::string> custom_zh_en_;
};
