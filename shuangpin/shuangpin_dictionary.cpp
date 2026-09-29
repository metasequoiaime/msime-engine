#include "../core/online_candidate_batch.h"
#include "../core/candidate_utils.h"
#include "shuangpin_dictionary.h"
#include "../user_dictionary/user_dictionary_journal.h"
#include "../common/sqlite_data_version.h"
#include "../common/sqlite_query.h"
#include "../common/helpcode_utils.h"
#include "../common/string_utils.h"
#include "../quanpin/quanpin_query.h"
#include "../quanpin/quanpin_utils.h"
#include "shuangpin_query.h"
#include "shuangpin_utils.h"
#include "../neural/neural_decoder.h"
#include <algorithm>
#include <mutex>
#include <shared_mutex>
#include <sqlite3.h>
#include <string>
#include <tuple>
#include <unordered_set>
#include <utility>
#include <cstdlib>
#include <climits>
#include <utf8/cpp17.h>

using namespace std;

namespace
{
std::string double_helpcode_cache_key(const std::string &pinyin, const std::string &help_codes)
{
    return pinyin + ":" + help_codes;
}

} // namespace

ShuangpinDictionary::ShuangpinDictionary(const ShuangpinProfile &profile, metasequoia::RuntimePaths paths)
    : profile_(profile), paths_(std::move(paths)), decoder_(paths_.resource(metasequoia::assets::pinyin_model),
                                                            paths_.user(metasequoia::assets::pinyin_user_dictionary)),
      neural_desktop_model_(neural::shared_sentence_model(
          metasequoia::path_to_utf8(paths_.resource(metasequoia::assets::neural_model_desktop)))),
      neural_keyboard_model_(neural::shared_sentence_model(
          metasequoia::path_to_utf8(paths_.resource(metasequoia::assets::neural_model_keyboard)))),
      helpcodes_(HelpcodeUtils::load_helpcode_keymap(paths_.resources, HelpcodeUtils::selected_helpcode_schema())),
      _kb_input_sequence(100), _cached_buffer(128), _cached_buffer_sgl(128), _cached_buffer_sgl_reversed(128),
      _cached_buffer_dbl(128), _cached_buffer_series(128)
{
    // 最多可以输出 64 个汉字，拼音最多可以接受 128 个字符

    quanpin_db_path_ = metasequoia::path_to_utf8(paths_.dictionary(metasequoia::assets::main_dictionary));
    // No SQLITE_OPEN_CREATE: a missing dictionary must stay missing instead of being materialised as an empty file,
    // and the handle has to become null so the quanpin_db_ == nullptr guards on the query paths actually fire.
    sqlite3 *raw = nullptr;
    const int status = sqlite3_open_v2(quanpin_db_path_.c_str(), &raw, SQLITE_OPEN_READWRITE, nullptr);
    metasequoia::SqliteDatabase opened(raw);
    if (status == SQLITE_OK)
    {
        // See QuanpinDictionary's constructor: readers must wait out a commit
        // rather than fail with SQLITE_BUSY.
        sqlite3_busy_timeout(opened.get(), quanpin::kDictionaryBusyTimeoutMs);
        quanpin_db_ = std::move(opened);
        quanpin::warm_up(quanpin_db_.get(), quanpin_statement_cache_);
        // Off the typing path, for the reason QuanpinDictionary's constructor gives.
        quanpin::NgramTable::shared(paths_.dictionary(quanpin::kBigramFileName));
        quanpin::NgramTable::shared(paths_.dictionary(quanpin::kTrigramFileName));
        metasequoia::reset_if_sqlite_data_version_changed(quanpin_db_.get(), data_version_, [this] { reset_cache(); });
    }
}

void ShuangpinDictionary::set_sentence_association(const SentenceAssociationOptions &options)
{
    if (sentence_association_ == options)
        return;
    sentence_association_ = options;
    reset_cache();
}

void ShuangpinDictionary::set_rescoring_context(const std::string &context)
{
    if (rescoring_context_ == context)
        return;
    rescoring_context_ = context;
    // The committed context affects ordering only while a neural model is active; otherwise keep
    // the dictionary cache warm across selections.
    const bool rescoring_active = sentence_association_.neural_reranking_enabled(neural_desktop_model_ != nullptr,
                                                                                 neural_keyboard_model_ != nullptr);
    if (rescoring_active)
        reset_cache();
}

/**
 * @brief Generate candidate list when not in help mode
 *
 * @param pinyin_sequence
 * @param pinyin_segmentation
 * @return vector<ShuangpinDictionary::WordItem>
 */
vector<ShuangpinDictionary::WordItem> ShuangpinDictionary::generate( //
    const string &pinyin_sequence,                                   //
    const string &pinyin_segmentation,                               //
    const string &cache_key                                          //
)
{
    // std::shared_lock lock(mutex_);
    vector<ShuangpinDictionary::WordItem> candidate_list;
    if (pinyin_sequence.size() == 0)
    {
        return candidate_list;
    }
    vector<string> code_list;
    if (pinyin_sequence.size() == 1)
    {
        generate_for_single_char(candidate_list, pinyin_sequence);
    }
    else
    {
        const std::string effective_cache_key = cache_key.empty() ? pinyin_sequence : cache_key;
        // Check cache first
        if (_cached_buffer.contains(effective_cache_key))
        {
            metasequoia::reset_if_sqlite_data_version_changed(quanpin_db_.get(), data_version_,
                                                              [this] { reset_cache(); });
            if (const auto cached = _cached_buffer.get(effective_cache_key))
            {
                return cached.value();
            }
        }

        candidate_list = query_from_quanpin_database(pinyin_sequence, pinyin_segmentation);
        _cached_buffer.insert(effective_cache_key, candidate_list);
    }
    return candidate_list;
}

/**
 * @brief 对于纯粹的拼音，除了完全匹配的汉字串，子串也要全部给出来，子串是为了给接下来可能会进行的造词使用的
 *
 * @param pinyin_sequence
 * @param pinyin_segmentation
 * @return vector<ShuangpinDictionary::WordItem>
 */
void ShuangpinDictionary::set_sentence_alternatives(bool enabled)
{
    if (sentence_alternatives_ == enabled)
        return;
    sentence_alternatives_ = enabled;
    // The cached series were assembled under the previous answer.
    _cached_buffer_series.clear();
}

vector<ShuangpinDictionary::WordItem> ShuangpinDictionary::generateSeries( //
    const string &pinyin_sequence,                                         //
    const string &pinyin_segmentation,                                     //
    const string &cache_key                                                //
)
{
    vector<ShuangpinDictionary::WordItem> candidate_list;
    if (pinyin_sequence.size() == 0)
    {
        return candidate_list;
    }
    vector<string> code_list;
    if (pinyin_sequence.size() == 1)
    {
        generate_for_single_char(candidate_list, pinyin_sequence);
    }
    else
    {
        const std::string effective_cache_key = cache_key.empty() ? pinyin_sequence : cache_key;
        const bool neural_enabled = sentence_association_.neural_reranking_enabled(neural_desktop_model_ != nullptr,
                                                                                   neural_keyboard_model_ != nullptr);
        // 先看一下缓存里有没有
        if (!neural_enabled && _cached_buffer_series.contains(effective_cache_key))
        {
            metasequoia::reset_if_sqlite_data_version_changed(quanpin_db_.get(), data_version_,
                                                              [this] { reset_cache(); });
            if (const auto cached = _cached_buffer_series.get(effective_cache_key))
            {
                return cached.value();
            }
        }

        // 查询当前的拼音严格对应的数据
        vector<ShuangpinDictionary::WordItem> cur_pinyin_cand =
            generate(pinyin_sequence, pinyin_segmentation, effective_cache_key);
        if (cur_pinyin_cand.size() > 0)
        {
            candidate_list.insert(candidate_list.end(), cur_pinyin_cand.begin(), cur_pinyin_cand.end());
        }
        else
        { /* 可能数据库查询的结果是空，这时就需要联想，这个只适合在此处联想 */
            if (candidate_list.size() == 0 && sentence_association_.google)
            {
                string quanpin_str =
                    ShuangpinUtil::convert_seg_shuangpin_to_seg_complete_pinyin(pinyin_segmentation, profile_);
                string res = decoder_.sentence(quanpin_str);
                if (res.size() > 0)
                {
                    // Whole-sentence fallbacks must retain the converted
                    // quanpin reading for creating-word persistence.
                    candidate_list.emplace_back(_pinyin_sequence, res, 1, CandidateSource::Fallback, quanpin_str);
                    candidate_list.back().sentence_association = true;
                }
            }
        }

        // 查询当前的拼音子串对应的数据
        string pure_pinyin = pinyin_sequence;
        string seg_pinyin = pinyin_segmentation;
        while (true)
        {
            size_t pos = seg_pinyin.rfind('\'');
            if (pos != string::npos)
            {
                seg_pinyin = seg_pinyin.substr(0, pos);
                pure_pinyin = CommonUtils::remove_apostrophe_delimiters(seg_pinyin);
                vector<ShuangpinDictionary::WordItem> sub_pinyin_cand = generate(pure_pinyin, seg_pinyin);
                candidate_list.insert(candidate_list.end(), sub_pinyin_cand.begin(), sub_pinyin_cand.end());
            }
            else
            {
                break;
            }
        }

        const std::string quanpin_segmentation =
            ShuangpinUtil::convert_seg_shuangpin_to_seg_complete_pinyin(pinyin_segmentation, profile_);
        const auto quanpin_segments = quanpin::split_segments(quanpin_segmentation);
        // Prefer one local Google-Pinyin whole-sentence result, followed by
        // only the best dictionary-lattice path.  Multiple lattice paths tend
        // to crowd out useful candidates with near-duplicate sentences.
        //
        // Both rank below a dictionary entry that already answers the whole
        // key, which is what word_lattice.h documents and what
        // whole_sentence_insert_position computes.  The quanpin path learned
        // this; this one kept inserting at index 0, so an assembled sentence
        // displaced the exact word, turning 筚路蓝缕 into 笔录蓝绿.
        std::string google_sentence;
        if (sentence_association_.google && quanpin_segments.size() >= 3 &&
            quanpin_segmentation.find('\'') != std::string::npos)
        {
            google_sentence = decoder_.sentence(quanpin_segmentation);
            const bool duplicate = contains_candidate_word(candidate_list, google_sentence);
            if (!google_sentence.empty() && !duplicate)
            {
                const auto at = static_cast<std::ptrdiff_t>(
                    quanpin::whole_sentence_insert_position(candidate_list, quanpin_segments.size()));
                WordItem sentence(_pinyin_sequence, google_sentence, 1, CandidateSource::Fallback,
                                  quanpin_segmentation);
                sentence.sentence_association = true;
                candidate_list.insert(candidate_list.begin() + at, std::move(sentence));
            }
        }
        const auto rerankers = quanpin::make_neural_rerankers(sentence_association_, neural_keyboard_model_,
                                                              neural_desktop_model_, rescoring_context_);
        const bool need_lattice = sentence_association_.word_lattice || !rerankers.empty();
        auto lattice_options = quanpin::make_sentence_lattice_options(paths_, sentence_alternatives_);
        lattice_options.nbest =
            rerankers.empty() ? lattice_options.nbest : static_cast<int>(neural::RerankOptions{}.max_paths);
        lattice_options.include_lattice_best = sentence_association_.word_lattice;
        lattice_options.show_next_on_duplicate = sentence_association_.show_next_on_duplicate;
        if (need_lattice)
        {
            quanpin::merge_lattice_candidates(
                candidate_list, quanpin_segments,
                quanpin::make_lattice_db_lookup(quanpin_db_.get(), quanpin_statement_cache_,
                                                quanpin::QuerySource::Shuangpin, lattice_options.span_limit),
                pinyin_sequence, lattice_options, {}, nullptr, rerankers);
        }
        if (!google_sentence.empty())
        {
            // The lattice merge above may have inserted ahead of the fallback. Put the fallback
            // back in front of the lattice row, but still behind the exact dictionary hits.
            const auto boundary = static_cast<std::ptrdiff_t>(
                quanpin::whole_sentence_insert_position(candidate_list, quanpin_segments.size()));
            move_candidate_to_position(candidate_list, google_sentence, CandidateSource::Fallback,
                                       static_cast<std::size_t>(boundary));
        }

        /* 缓存起来 */
        if (!neural_enabled)
            _cached_buffer_series.insert(effective_cache_key, candidate_list);
    }

    return candidate_list;
}

/**
 * @brief Filter with single help code
 *
 * Not only the first Hanzi part, but also the last one that will be considered.
 *   - For single Hanzi, we consider its first and last part
 *   - For Multi Hanzi, we consider first Hanzi's first part and last Hanzi's first part
 *
 * e.g. 阿: 阿's helpcode is ek, when we type aae or aak, 阿 will both be filtered.
 *      阿姨: 阿's helpcode is ek, 姨's helpcode is nr, when we type aayie or aayin, 阿姨 will both be filtered.
 *
 * 此外，单码辅助的情况，需要把原始拼音的候选列表加到辅助码模式的候选列表后面，这里的指的是不将最后一个字符看成是辅助码的情况下得到的候选项的结果
 *
 * @param candidate_list
 * @param filtered_list
 * @param help_code
 * @param pinyin_sequence 原始的拼音序列
 */
void ShuangpinDictionary::filter_with_single_helpcode(           //
    const vector<ShuangpinDictionary::WordItem> &candidate_list, //
    vector<ShuangpinDictionary::WordItem> &result_list,          //
    const string &help_code,                                     //
    const string &pinyin_sequence                                //
)
{
    if (candidate_list.empty() || help_code.size() != 1)
        return;
    const bool prefer_last_helpcode = help_code[0] >= 'A' && help_code[0] <= 'Z';
    const string normalized_help_code(1, CommonUtils::lowercase_ascii_char(static_cast<unsigned char>(help_code[0])));
    vector<ShuangpinDictionary::WordItem> first_helpcode_matched_list;
    vector<ShuangpinDictionary::WordItem> last_helpcode_matched_list;
    vector<ShuangpinDictionary::WordItem> left_helpcode_matched_list; // 被筛完之后剩下的

    for (const auto &cand : candidate_list)
    {
        switch (HelpcodeUtils::match_single_helpcode(cand.word, normalized_help_code, helpcodes_.get()))
        {
        case HelpcodeUtils::SingleHelpcodeMatch::First:
            first_helpcode_matched_list.push_back(cand);
            break;
        case HelpcodeUtils::SingleHelpcodeMatch::Last:
            last_helpcode_matched_list.push_back(cand);
            break;
        case HelpcodeUtils::SingleHelpcodeMatch::Both:
            (prefer_last_helpcode ? last_helpcode_matched_list : first_helpcode_matched_list).push_back(cand);
            break;
        case HelpcodeUtils::SingleHelpcodeMatch::None:
            left_helpcode_matched_list.push_back(cand);
            break;
        }
    }

    /* 辅助码筛出来的候选列表 */
    if (prefer_last_helpcode)
    {
        result_list.insert(result_list.end(), last_helpcode_matched_list.begin(), last_helpcode_matched_list.end());
        result_list.insert(result_list.end(), first_helpcode_matched_list.begin(), first_helpcode_matched_list.end());
    }
    else
    {
        result_list.insert(result_list.end(), first_helpcode_matched_list.begin(), first_helpcode_matched_list.end());
        result_list.insert(result_list.end(), last_helpcode_matched_list.begin(), last_helpcode_matched_list.end());
    }
    /* 把原始拼音的候选列表加到辅助码模式的候选列表后面 */
    const std::string original_segmentation = ShuangpinUtil::pinyin_segmentation(pinyin_sequence, profile_);
    auto original_candidate_list = generateSeries(pinyin_sequence, original_segmentation);
    result_list.insert(result_list.end(), original_candidate_list.begin(), original_candidate_list.end());
    /* 把剩下的候选列表加到辅助码模式的候选列表后面 */
    result_list.insert(result_list.end(), left_helpcode_matched_list.begin(), left_helpcode_matched_list.end());
}

/**
 * @brief Filter with double help codes
 *
 * Rules:
 *   - For single Hanzi, we consider its first and last part
 *   - For Multi Hanzi, we consider first Hanzi's first part and last Hanzi's first part
 *
 * e.g. 阿: 阿's helpcode is ek, when we type aaek, 阿 will be filtered.
 *      阿姨: 阿's helpcode is ek, 姨's helpcode is nr, when we type aayien, 阿姨 will be filtered.
 *
 * @param candidate_list
 * @param result_list
 * @param help_codes
 */
void ShuangpinDictionary::filter_with_double_helpcodes(               //
    const std::vector<ShuangpinDictionary::WordItem> &candidate_list, //
    std::vector<ShuangpinDictionary::WordItem> &result_list,          //
    const std::string &help_codes                                     //
)
{
    if (candidate_list.empty())
        return;

    for (const auto &cand : candidate_list)
    {
        if (HelpcodeUtils::matches_double_helpcodes(cand.word, help_codes, helpcodes_.get()))
        {
            result_list.push_back(cand);
        }
    }
}

/**
 * @brief
 *
 * Note: Use the help code only in standard cases—that is, when the shuangpin part is complete.
 *
 * @param pure_pinyin
 * @param pure_pinyin_segmentation
 * @param pinyin_sequence
 * @param help_codes
 * @return vector<ShuangpinDictionary::WordItem>
 */
vector<ShuangpinDictionary::WordItem> ShuangpinDictionary::generate_with_helpcodes( //
    const string &pure_pinyin,                                                      //
    const string &pure_pinyin_segmentation,                                         //
    const string &pinyin_sequence,                                                  //
    const string &help_codes                                                        //
)
{
    vector<WordItem> candidate_list;
    const bool reversed_single_helpcode = help_codes.size() == 1 && help_codes[0] >= 'A' && help_codes[0] <= 'Z';
    // Check cache first
    if (help_codes.size() == 1)
    {
        auto &single_helpcode_cache = reversed_single_helpcode ? _cached_buffer_sgl_reversed : _cached_buffer_sgl;
        if (single_helpcode_cache.contains(pinyin_sequence))
        {
            metasequoia::reset_if_sqlite_data_version_changed(quanpin_db_.get(), data_version_,
                                                              [this] { reset_cache(); });
            if (const auto refreshed = single_helpcode_cache.get(pinyin_sequence))
            {
                return refreshed.value();
            }
        }
    }
    else if (help_codes.size() == 2)
    {
        const auto cache_key = double_helpcode_cache_key(pinyin_sequence, help_codes);
        if (_cached_buffer_dbl.contains(cache_key))
        {
            metasequoia::reset_if_sqlite_data_version_changed(quanpin_db_.get(), data_version_,
                                                              [this] { reset_cache(); });
            if (const auto cached = _cached_buffer_dbl.get(cache_key))
            {
                return cached.value();
            }
        }
    }

    candidate_list = generateSeries(pure_pinyin, pure_pinyin_segmentation);
    vector<WordItem> result_list;
    // Filter with help codes
    if (help_codes.size() == 1)
    {
        filter_with_single_helpcode( //
            candidate_list,          //
            result_list,             //
            help_codes,              //
            pinyin_sequence          //
        );
        auto &single_helpcode_cache = reversed_single_helpcode ? _cached_buffer_sgl_reversed : _cached_buffer_sgl;
        single_helpcode_cache.insert(pinyin_sequence, result_list);
    }
    else if (help_codes.size() == 2)
    {
        filter_with_double_helpcodes( //
            candidate_list,           //
            result_list,              //
            help_codes                //
        );
        _cached_buffer_dbl.insert(double_helpcode_cache_key(pinyin_sequence, help_codes), result_list);
    }
    return result_list;
}

std::string VkCodeToChar(ImeKeyCode vk)
{
    if (vk >= 'A' && vk <= 'Z')
    {
        return std::string(1, char(vk + ('a' - 'A')));
    }
    if (vk >= '0' && vk <= '9')
    {
        return std::string(1, char(vk));
    }
    switch (vk)
    {
    case ImeKey::Space:
        return " ";
    case ImeKey::Tab:
        return "\t";
    case ImeKey::Return:
        return "\n";
    default:
        return "";
    }
}

std::string VkSequenceToString(const ImeKeyCode *vk_codes, size_t count)
{
    std::string result;
    for (size_t i = 0; i < count; ++i)
    {
        result += VkCodeToChar(vk_codes[i]);
    }
    return result;
}

void ShuangpinDictionary::generate_for_single_char(vector<ShuangpinDictionary::WordItem> &candidate_list, string code)
{
    constexpr int kInitialCandidateLimit = 24;
    candidate_list = query_initial_from_quanpin_database(code, kInitialCandidateLimit);
}

bool ShuangpinDictionary::expand_initial_candidates()
{
    return expand_initial_candidates(_pinyin_sequence, _cur_candidate_list);
}

bool ShuangpinDictionary::expand_initial_candidates(const std::string &code, std::vector<WordItem> &candidates,
                                                    const std::string &series_cache_key)
{
    if (!expand_limited_initial_candidates(code, candidates,
                                           [&] { return query_initial_from_quanpin_database(code, INT_MAX); }))
        return false;

    if (!series_cache_key.empty())
    {
        _cached_buffer_series.insert(series_cache_key, candidates);
    }
    return true;
}

/**
 * @brief
 *
 * @param vk
 * @return int
 */
int ShuangpinDictionary::handleVkCode(ImeKeyCode vk, ImeModifierMask modifiers_down, ImeCharacter wch)
{
    if (vk != 0)
    { /* 0 是造词过程中的 dummy code */
        _kb_input_sequence.push_back(vk);
        if (vk >= 'A' && vk <= 'Z')
        {
            const char lowerAlpha = static_cast<char>(vk + ('a' - 'A'));
            _pinyin_sequence += lowerAlpha;

            // Prefer the real typed character from TSF side so CapsLock/Shift combinations are preserved.
            if (wch >= u'A' && wch <= u'Z')
            {
                _pinyin_sequence_with_cases += static_cast<char>(wch);
            }
            else if (wch >= u'a' && wch <= u'z')
            {
                _pinyin_sequence_with_cases += static_cast<char>(wch);
            }
            else if (modifiers_down >> 0 & 1u)
            {
                // Fallback for callers that don't provide wch.
                _pinyin_sequence_with_cases += static_cast<char>(vk);
            }
            else
            {
                _pinyin_sequence_with_cases += lowerAlpha;
            }
        }
        else if (profile_.name == "microsoft" && vk == ImeKey::Semicolon && wch == u';' &&
                 _pinyin_sequence.size() % 2 == 1)
        {
            _pinyin_sequence += ';';
            _pinyin_sequence_with_cases += ';';
        }
        else if (vk == ImeKey::Space || (vk >= '0' && vk <= '9') || vk == ImeKey::Return || vk == ImeKey::Shift ||
                 vk == ImeKey::Escape)
        {
            if (vk == ImeKey::Return || vk == ImeKey::Shift || vk == ImeKey::Escape)
            { /* 空格键和数字键不要清理状态，因为可能会触发造词 */
                // Clear state
                reset_state();
            }
            return 0;
        }
        else if (vk == ImeKey::Tab)
        {
            return 0;
        }
        else if (vk == ImeKey::Backspace)
        {
            if (_pinyin_sequence.size() > 0)
            {
                _pinyin_sequence = _pinyin_sequence.substr(0, _pinyin_sequence.size() - 1);
                _pinyin_sequence_with_cases =
                    _pinyin_sequence_with_cases.substr(0, _pinyin_sequence_with_cases.size() - 1);
            }
        }
    }

    //
    // We do not handle other keys currently
    //

    /* 初始状态 */
    _pure_pinyin_sequence = _pinyin_sequence;

    /* Whether in full help mode */
    _is_full_help_mode = ShuangpinUtil::IsFullHelpMode(_pinyin_sequence_with_cases, profile_);
    if (_is_full_help_mode)
    {
        _help_mode_raw_pos = _pinyin_sequence.size() - 2;
    }
    else
    {
        _help_mode_raw_pos = 0;
    }

    /* Generate candidate list */
    if (_is_full_help_mode)
    { // 全码辅助，结果只包含根据辅助码筛出来的候选词部分
        _pure_pinyin_sequence = _pinyin_sequence.substr(0, _help_mode_raw_pos);
        _pinyin_segmentation = ShuangpinUtil::pinyin_segmentation(_pure_pinyin_sequence, profile_);
        _pinyin_helpcodes = ShuangpinUtil::GetFullHelpCodes(_pinyin_sequence_with_cases);
        _cur_candidate_list = generate_with_helpcodes( //
            _pure_pinyin_sequence,                     //
            _pinyin_segmentation,                      //
            _pinyin_sequence,                          //
            _pinyin_helpcodes                          //
        );
    }
    else
    {
        // 不是全码辅助的情况：
        //   1. 奇数长度拼音序列，且双拼部分是完整的拼音，需要触发辅助码
        //   2. 偶数长度拼音序列，不需要触发辅助码
        if (_pinyin_sequence.size() % 2 == 1 && _pinyin_sequence.size() > 1)
        { /* 1. 奇数长度拼音序列 */
            _pure_pinyin_sequence = _pinyin_sequence.substr(0, _pinyin_sequence.size() - 1);
            _pinyin_segmentation = ShuangpinUtil::pinyin_segmentation(_pure_pinyin_sequence, profile_);
            if (ShuangpinUtil::is_all_complete_pinyin(_pure_pinyin_sequence, _pinyin_segmentation))
            { /* 双拼部分是完整的拼音，需要触发辅助码 */
                _pure_pinyin_sequence = _pinyin_sequence.substr(0, _pinyin_sequence.size() - 1);
                _pinyin_segmentation = ShuangpinUtil::pinyin_segmentation(_pure_pinyin_sequence, profile_);
                _pinyin_helpcodes = _pinyin_sequence_with_cases.substr(_pinyin_sequence_with_cases.size() - 1, 1);
                _cur_candidate_list = generate_with_helpcodes( //
                    _pure_pinyin_sequence,                     //
                    _pinyin_segmentation,                      //
                    _pinyin_sequence,                          //
                    _pinyin_helpcodes                          //
                );
            }
            else
            { /* 依然使用纯拼音，不触发辅助码模式 */
                _pinyin_segmentation = ShuangpinUtil::pinyin_segmentation(_pinyin_sequence, profile_);
                _cur_candidate_list = generateSeries(_pinyin_sequence, _pinyin_segmentation);
            }
        }
        else
        { /* 偶数长度拼音序列，不需要触发辅助码 */
            _pinyin_segmentation = ShuangpinUtil::pinyin_segmentation(_pinyin_sequence, profile_);
            _cur_candidate_list = generateSeries(_pinyin_sequence, _pinyin_segmentation);
        }
    }

    _pinyin_segmentation = ShuangpinUtil::pinyin_segmentation(_pinyin_sequence, profile_);

    return 0;
}

int ShuangpinDictionary::create_word(string pinyin, string word)
{
    return create_word_from_quanpin(shuangpin::normalize_input_with_delimiters(pinyin, profile_), std::move(word));
}

int ShuangpinDictionary::create_word_from_quanpin(string pinyin, string word)
{
    const auto segments = quanpin::split_segments(pinyin);
    if (!quanpin::has_expected_complete_pinyin_segments(segments, HelpcodeUtils::count_han_chars(word)))
    {
        return ERROR_CODE;
    }

    pinyin = quanpin::join_segments(segments);
    const string jp = quanpin::segments_to_jianpin(segments);
    if (!quanpin::has_valid_word_pinyin(pinyin, jp, word, true))
    {
        return ERROR_CODE;
    }
    const auto insert_result = quanpin::insert_word_if_missing(quanpin_db_.get(), pinyin, jp, word);
    if (insert_result == quanpin::WordInsertResult::Failed)
        return ERROR_CODE;
    if (insert_result == quanpin::WordInsertResult::Existing)
        return OK;
    (void)user_dictionary::record_user_insert(metasequoia::path_to_utf8(paths_.user(metasequoia::assets::user_journal)),
                                              user_dictionary::DictionaryKind::Pinyin, pinyin, word, 10000);
    /* 插入新词之后要清理缓存 */
    reset_cache();
    return OK;
}

int ShuangpinDictionary::update_weight_by_pinyin_and_word(string pinyin, string word)
{
    const auto direct_cuts =
        quanpin::cut_pinyin_by_mode(CommonUtils::remove_apostrophe_delimiters(pinyin), "correction");
    // A correction cut that merely reproduces the input may still consist of
    // incomplete initials (for example qbtmuo -> q'b't'mu'o). Only a complete
    // pinyin reading is safe to treat as an already-canonical key; otherwise
    // normalize the caller's Shuangpin input first.
    if (direct_cuts.empty() || !quanpin::has_only_complete_pinyin_segments(direct_cuts.front()) ||
        CommonUtils::remove_apostrophe_delimiters(quanpin::join_segments(direct_cuts.front())) !=
            CommonUtils::remove_apostrophe_delimiters(pinyin))
    {
        pinyin = normalize_shuangpin_to_quanpin_input(pinyin);
    }
    const auto cuts = quanpin::cut_pinyin_by_mode(CommonUtils::remove_apostrophe_delimiters(pinyin), "correction");
    if (cuts.empty())
        return ERROR_CODE;
    auto segments = cuts.front();
    const size_t han_count = HelpcodeUtils::count_han_chars(word);
    if (segments.size() > han_count)
        segments.resize(han_count);
    const std::string normalized = quanpin::join_segments(segments);
    if (!metasequoia::sqlite_execute_statement(quanpin_db_.get(),
                                               quanpin::build_sql_for_updating_word(normalized, word, true)))
    {
        return ERROR_CODE;
    }
    (void)user_dictionary::record_pinyin_upsert_from_database(
        quanpin_db_path_, normalized, word, metasequoia::path_to_utf8(paths_.user(metasequoia::assets::user_journal)));
    reset_cache();
    return OK;
}

int ShuangpinDictionary::delete_by_pinyin_and_word(string pinyin, string word)
{
    const auto direct_cuts =
        quanpin::cut_pinyin_by_mode(CommonUtils::remove_apostrophe_delimiters(pinyin), "correction");
    const auto normalized_shuangpin = normalize_shuangpin_to_quanpin_input(pinyin);
    const auto shuangpin_cuts = quanpin::cut_pinyin_by_mode(normalized_shuangpin, "correction");
    const size_t han_count = HelpcodeUtils::count_han_chars(word);
    const bool direct_key_matches_word = !direct_cuts.empty() && direct_cuts.front().size() == han_count;
    const bool shuangpin_key_matches_word = !shuangpin_cuts.empty() && shuangpin_cuts.front().size() == han_count;
    if ((!direct_key_matches_word && shuangpin_key_matches_word) ||
        (direct_cuts.empty() || CommonUtils::remove_apostrophe_delimiters(quanpin::join_segments(
                                    direct_cuts.front())) != CommonUtils::remove_apostrophe_delimiters(pinyin)))
    {
        pinyin = normalized_shuangpin;
    }
    const auto cuts = quanpin::cut_pinyin_by_mode(CommonUtils::remove_apostrophe_delimiters(pinyin), "correction");
    if (cuts.empty())
        return ERROR_CODE;
    const std::string normalized = quanpin::join_segments(cuts.front());
    if (!user_dictionary::delete_dictionary_candidate(
            quanpin_db_path_, metasequoia::path_to_utf8(paths_.user(metasequoia::assets::user_journal)),
            user_dictionary::DictionaryKind::Pinyin, normalized, word))
        return ERROR_CODE;
    reset_cache();
    return OK;
}

// generate_with_seg_pinyin

ShuangpinDictionary::~ShuangpinDictionary()
{
    quanpin_statement_cache_.clear();
    quanpin_db_.reset();
}

vector<ShuangpinDictionary::WordItem> ShuangpinDictionary::query_from_quanpin_database(
    const std::string &pinyin_sequence, const std::string &pinyin_segmentation)
{
    if (quanpin_db_ == nullptr || pinyin_segmentation.empty())
    {
        return {};
    }

    const std::string quanpin_segmentation =
        ShuangpinUtil::convert_seg_shuangpin_to_seg_complete_pinyin(pinyin_segmentation, profile_);
    const auto segments = quanpin::split_segments(quanpin_segmentation);
    if (segments.empty())
    {
        return {};
    }

    try
    {
        const auto flat_items = quanpin::query_segments_keyed_flat(
            segments, quanpin_db_.get(), quanpin_statement_cache_, INT_MAX, quanpin::QuerySource::Shuangpin);
        return make_database_candidates(pinyin_sequence, flat_items);
    }
    catch (const std::exception &ex)
    {
        (void)0;
    }

    return {};
}

std::optional<WordItem> ShuangpinDictionary::find_candidate(const std::string &key, const std::string &value)
{
    return quanpin::find_candidate(quanpin_db_.get(), key, value);
}

vector<ShuangpinDictionary::WordItem> ShuangpinDictionary::query_initial_from_quanpin_database(const std::string &code,
                                                                                               int limit)
{
    if (quanpin_db_ == nullptr || code.size() != 1 || code.front() < 'a' || code.front() > 'z')
    {
        return {};
    }

    const std::string initial = ShuangpinUtil::convert_seg_shuangpin_to_seg_complete_pinyin(code, profile_);
    const auto rows = quanpin::query_initial(quanpin_db_.get(), initial, limit);

    return make_database_candidates(code, rows);
}

std::string ShuangpinDictionary::normalize_shuangpin_to_quanpin_segmentation(const std::string &pinyin) const
{
    if (pinyin.empty())
    {
        return {};
    }

    return shuangpin::normalize_input_with_delimiters(pinyin, profile_);
}

std::string ShuangpinDictionary::normalize_shuangpin_to_quanpin_input(const std::string &pinyin) const
{
    return CommonUtils::remove_apostrophe_delimiters(normalize_shuangpin_to_quanpin_segmentation(pinyin));
}

void ShuangpinDictionary::reset_state()
{
    _is_full_help_mode = false;
    _help_mode_raw_pos = 0;
    _kb_input_sequence.clear();
    _pinyin_sequence = "";
    _pinyin_sequence_with_cases = "";
    _pure_pinyin_sequence = "";
    _pinyin_segmentation = "";
    _help_codes_sequence.fill(0);
    _cur_candidate_list.clear();
    _cur_page_candidate_list.clear();
}

void ShuangpinDictionary::reset_cache()
{
    _cached_buffer.clear();
    _cached_buffer_sgl.clear();
    _cached_buffer_sgl_reversed.clear();
    _cached_buffer_dbl.clear();
    _cached_buffer_series.clear();
}

int ShuangpinDictionary::insert_word_to_cached_buffer_series(const std::string &pinyin, const std::string &word,
                                                             CandidateSource source)
{
    if (is_online_candidate_source(source))
        return insert_word_to_cached_buffer_series(pinyin, std::vector<std::string>{word}, source);
    (void)0;
    if (pinyin.empty() || word.empty())
    {
        return -1;
    }

    return _cached_buffer_series.update_or_insert(pinyin,
                                                  [&](auto &list) {
                                                      insert_cached_candidate(list, pinyin, word, source);
                                                      return true;
                                                  })
               ? OK
               : ERROR_CODE;
}

int ShuangpinDictionary::insert_word_to_active_helpcode_cache(const std::string &pinyin, const std::string &word,
                                                              CandidateSource source,
                                                              const std::string &double_helpcodes)
{
    if (is_online_candidate_source(source))
        return insert_word_to_active_helpcode_cache(pinyin, std::vector<std::string>{word}, source, double_helpcodes);
    auto insert_into_cache = [&](auto &cache, const std::string &cache_key) {
        return cache.update_if_cached(cache_key, [&](auto &list) {
            insert_cached_candidate(list, pinyin, word, source);
            return true;
        });
    };

    if (!double_helpcodes.empty())
    {
        return insert_into_cache(_cached_buffer_dbl, double_helpcode_cache_key(pinyin, double_helpcodes)) ? 0 : -1;
    }
    const bool updated_single = insert_into_cache(_cached_buffer_sgl, pinyin);
    const bool updated_reversed_single = insert_into_cache(_cached_buffer_sgl_reversed, pinyin);
    return updated_single || updated_reversed_single ? 0 : -1;
}

int ShuangpinDictionary::insert_word_to_cached_buffer_series(const std::string &pinyin,
                                                             const std::vector<std::string> &words,
                                                             CandidateSource source)
{
    if (pinyin.empty())
        return -1;
    return _cached_buffer_series.update_or_insert(
               pinyin, [&](auto &list) { return replace_online_candidate_batch(list, pinyin, words, source); })
               ? OK
               : ERROR_CODE;
}

int ShuangpinDictionary::insert_word_to_active_helpcode_cache(const std::string &pinyin,
                                                              const std::vector<std::string> &words,
                                                              CandidateSource source,
                                                              const std::string &double_helpcodes)
{
    auto insert_into_cache = [&](auto &cache, const std::string &cache_key) {
        return cache.update_if_cached(
            cache_key, [&](auto &list) { return replace_online_candidate_batch(list, pinyin, words, source); });
    };
    if (!double_helpcodes.empty())
    {
        return insert_into_cache(_cached_buffer_dbl, double_helpcode_cache_key(pinyin, double_helpcodes)) ? 0 : -1;
    }
    const bool single = insert_into_cache(_cached_buffer_sgl, pinyin);
    const bool reversed = insert_into_cache(_cached_buffer_sgl_reversed, pinyin);
    return single || reversed ? 0 : -1;
}
