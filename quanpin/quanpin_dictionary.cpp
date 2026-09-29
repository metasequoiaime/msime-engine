#include "../core/online_candidate_batch.h"
#include "../core/candidate_utils.h"
#include "fuzzy_pinyin.h"
#include "quanpin_dictionary.h"
#include "../user_dictionary/user_dictionary_journal.h"

#include "../common/helpcode_utils.h"
#include "../common/sqlite_data_version.h"
#include "../common/sqlite_query.h"
#include "../common/string_utils.h"
#include "quanpin_query.h"
#include "quanpin_utils.h"
#include "lattice_rerank.h"
#include "../neural/neural_decoder.h"
#include "../shuangpin/shuangpin_utils.h"
#include <algorithm>
#include <climits>
#include <cstring>
#include <unordered_set>
#include <utf8/cpp17.h>

namespace
{
constexpr size_t kSparsePinyinFallbackThreshold = 8;
// 续接词只取前三档音节长度:再长的条目权重已经掉到几十,占位不如留给前缀单字。
constexpr size_t kLongerPhraseExtraSyllables = 3;
constexpr int kLongerPhraseLimit = 12;
constexpr size_t kSyllableGraphPathLimit = 32;
constexpr size_t kMaxSyllablesForMultipleSegmentations = 4;
constexpr int kAlternativeSegmentationCandidateLimit = 128;
constexpr size_t kBestAlternativeSegmentationMaxIndex = 1;
// The protection slot only pulls an alternative reading into the first page.
// Candidates already visible there keep their natural weight order, including
// ranks the user has earned through frequency adjustment.
constexpr size_t kAlternativeSegmentationFirstPageSize = 6;
constexpr std::int64_t kAlternativeSegmentationPromotionRatio = 100;

quanpin::Segments normalize_umlaut_aliases(quanpin::Segments segments)
{
    for (auto &segment : segments)
    {
        if (segment.size() == 3 && segment[1] == 'u' && segment[2] == 'e' && (segment[0] == 'n' || segment[0] == 'l'))
        {
            segment[1] = 'v';
        }
        else if (segment.size() >= 2 && segment[1] == 'v' &&
                 (segment[0] == 'j' || segment[0] == 'q' || segment[0] == 'x' || segment[0] == 'y'))
        {
            segment[1] = 'u';
        }
    }
    return segments;
}

const std::string &effective_segmentation(const std::string &raw_input, const std::string &segmentation)
{
    return segmentation.empty() ? raw_input : segmentation;
}

std::string series_cache_key(const std::string &raw_input, const std::string &segmentation)
{
    const char *prefix = CommonUtils::contains_apostrophe_delimiter(raw_input) ? "M:" : "A:";
    return prefix + effective_segmentation(raw_input, segmentation);
}

// Folds letters for autocorrect comparisons: lowercases and strips manual delimiters. v and u
// remain distinct so an alias rewrite is visible as a corrected candidate.
std::string fold_autocorrect_letters(const std::string &text)
{
    return CommonUtils::lowercase_ascii(CommonUtils::remove_apostrophe_delimiters(text));
}

struct SeriesQueryResolution
{
    std::string segmentation;
    std::string cache_key;
    quanpin::Segments corrected_segments;
    bool corrected_input = false;
};

SeriesQueryResolution resolve_series_query(const std::string &raw_input, const quanpin::Segments &segments,
                                           unsigned autocorrect_types)
{
    SeriesQueryResolution result;
    // Guard order matters: the jianpin-shape predicate runs before autocorrect_cut
    // so a guarded input never pays for the BFS. Both guards express "the user did
    // not mistype" and either one disables the rewrite entirely.
    result.corrected_input =
        autocorrect_types != 0 && !segments.empty() && !CommonUtils::contains_apostrophe_delimiter(raw_input) &&
        !quanpin::has_only_complete_pinyin_segments(segments) &&
        !quanpin::looks_like_syllable_with_jianpin_tail(raw_input) &&
        !(result.corrected_segments = quanpin::autocorrect_cut(raw_input, autocorrect_types)).empty();
    result.segmentation = result.corrected_input ? quanpin::join_segments(result.corrected_segments)
                                                 : (segments.empty() ? raw_input : quanpin::join_segments(segments));
    result.cache_key = (result.corrected_input ? "C:" : "") + series_cache_key(raw_input, result.segmentation);
    return result;
}

} // namespace

QuanpinDictionary::QuanpinDictionary(std::string db_path, metasequoia::RuntimePaths paths)
    : cache_(128), series_cache_(128), segmentation_cache_(128), paths_(std::move(paths)),
      decoder_(paths_.resource(metasequoia::assets::pinyin_model),
               paths_.user(metasequoia::assets::pinyin_user_dictionary)),
      neural_desktop_model_(neural::shared_sentence_model(
          metasequoia::path_to_utf8(paths_.resource(metasequoia::assets::neural_model_desktop)))),
      neural_keyboard_model_(neural::shared_sentence_model(
          metasequoia::path_to_utf8(paths_.resource(metasequoia::assets::neural_model_keyboard)))),
      db_path_(db_path.empty() ? metasequoia::path_to_utf8(paths_.dictionary(metasequoia::assets::main_dictionary))
                               : std::move(db_path))
{

    // No SQLITE_OPEN_CREATE: a missing dictionary must stay missing instead of being materialised as an empty file,
    // and the handle has to become null so the db_ == nullptr guards on the query paths actually fire.
    db_ = quanpin::open_dictionary_database(db_path_);

    quanpin::warm_up(db_.get(), statement_cache_);
    // Mapping the tables and checking they are sorted is a sequential pass over fifteen megabytes. Left to the first
    // query that wants them, that lands on a keystroke; here it joins the work of opening the dictionary, which the
    // host already does off the typing path.
    quanpin::NgramTable::shared(paths_.dictionary(quanpin::kBigramFileName));
    quanpin::NgramTable::shared(paths_.dictionary(quanpin::kTrigramFileName));
    metasequoia::reset_if_sqlite_data_version_changed(db_.get(), data_version_, [this] { reset_cache(); });
}

QuanpinDictionary::~QuanpinDictionary()
{
    statement_cache_.clear();
    db_.reset();
}

void QuanpinDictionary::set_sentence_alternatives(bool enabled)
{
    (void)update_sentence_alternatives(sentence_alternatives_, enabled, [this] {
        cache_.clear();
        series_cache_.clear();
    });
}

void QuanpinDictionary::set_sentence_association(const SentenceAssociationOptions &options)
{
    (void)update_sentence_association_options(sentence_association_, options, [this] { reset_cache(); });
}

void QuanpinDictionary::set_rescoring_context(const std::string &context)
{
    (void)update_rescoring_context(rescoring_context_, context, sentence_association_, neural_desktop_model_ != nullptr,
                                   neural_keyboard_model_ != nullptr, [this] { reset_cache(); });
}

std::vector<WordItem> QuanpinDictionary::query_exact(const std::string &raw_input, const std::string &segmentation,
                                                     unsigned autocorrect_types)
{
    if (raw_input.empty())
    {
        current_candidate_list_.clear();
        return {};
    }

    pinyin_sequence_ = raw_input;
    const auto segments = resolve_segments(raw_input, segmentation);

    // Typing autocorrection: when the spelling is not a legal pinyin
    // combination (the correction cut already fell back to greedy), try to
    // rewrite the whole string into legal syllables. The corrected
    // segmentation becomes the primary key so that selection and weight
    // updates land on the right dictionary entries, and the original
    // (garbage-leaning) candidates stay behind as a fallback tail.
    const auto resolution = resolve_series_query(raw_input, segments, autocorrect_types);
    pinyin_segmentation_ = resolution.segmentation;

    // Autocorrected results get their own cache slot so they never leak the
    // fallback tail into plain (correct) spellings sharing the same key.
    const bool neural_enabled = sentence_association_.neural_reranking_enabled(neural_desktop_model_ != nullptr,
                                                                               neural_keyboard_model_ != nullptr);
    if (!neural_enabled && series_cache_.contains(resolution.cache_key))
    {
        metasequoia::reset_if_sqlite_data_version_changed(db_.get(), data_version_, [this] { reset_cache(); });
        if (auto cached = series_cache_.get(resolution.cache_key))
        {
            // CircularBuffer::get returns std::optional<Value> by value, so `cached` already owns a private copy and
            // moving out of it cannot touch the cached entry.
            return std::move(*cached);
        }
    }

    std::vector<quanpin::Segments> alternative_segmentations;
    std::unordered_set<std::string> seen_segmentations = {pinyin_segmentation_};
    const auto append_alternative = [&](const quanpin::Segments &candidate) {
        const std::string key = quanpin::join_segments(candidate);
        if (!key.empty() && seen_segmentations.insert(key).second &&
            alternative_segmentations.size() < kSyllableGraphPathLimit)
        {
            alternative_segmentations.push_back(candidate);
        }
    };

    // Correction-mode segmentation is part of autocorrection and must follow the
    // same switch. query() defaults autocorrect_types to 0, so running this
    // unconditionally changed the default behaviour for every caller: a misspelled
    // input such as "sahng" started offering the corrected candidate even with
    // autocorrection explicitly turned off.
    if (autocorrect_types != 0)
    {
        const auto correction_paths = quanpin::cut_pinyin_by_mode(raw_input, "correction");
        for (const auto &candidate : correction_paths)
        {
            append_alternative(candidate);
        }
    }

    if (!CommonUtils::contains_apostrophe_delimiter(raw_input) &&
        segments.size() <= kMaxSyllablesForMultipleSegmentations &&
        quanpin::has_only_complete_pinyin_segments(segments))
    {
        const auto complete_paths = quanpin::enumerate_complete_segmentations(quanpin::build_syllable_graph(raw_input),
                                                                              kSyllableGraphPathLimit);
        for (const auto &candidate : complete_paths)
        {
            append_alternative(candidate);
        }
    }

    std::vector<WordItem> result;
    if (resolution.corrected_input)
    {
        result = query_series(raw_input, pinyin_segmentation_, resolution.corrected_segments);
        const std::string fallback_segmentation =
            segmentation.empty() ? quanpin::join_segments(segments) : segmentation;
        append_unique_candidates_copy(result, query_series(raw_input, fallback_segmentation, segments));
    }
    else
    {
        result = query_series(raw_input, pinyin_segmentation_, segments);
        if (!alternative_segmentations.empty())
        {
            result = merge_alternative_segmentations(raw_input, pinyin_segmentation_, segments,
                                                     alternative_segmentations, std::move(result));
        }
    }
    if (!neural_enabled)
        series_cache_.insert(resolution.cache_key, result);
    return result;
}

std::optional<WordItem> QuanpinDictionary::find_candidate(const std::string &key, const std::string &value)
{
    return quanpin::find_candidate(db_.get(), key, value);
}

bool QuanpinDictionary::expand_initial_candidates(const std::string &code, std::vector<WordItem> &candidates)
{
    const auto expanded = expand_limited_initial_candidates(code, candidates, [&] {
        auto result = query_initial(code, INT_MAX);
        for (auto &item : result)
        {
            item.canonical_pinyin = item.pinyin;
            item.pinyin = code;
        }
        return result;
    });
    if (!expanded)
        return false;

    cache_.insert(code, *expanded);
    series_cache_.insert(series_cache_key(pinyin_sequence_, pinyin_segmentation_), candidates);
    current_candidate_list_ = candidates;
    return true;
}

std::vector<WordItem> QuanpinDictionary::query_series(const std::string &raw_input, const std::string &segmentation,
                                                      const quanpin::Segments &segments)
{
    if (segments.empty())
    {
        return query_single_path(raw_input, segmentation, segments);
    }

    std::vector<WordItem> result;
    for (size_t count = segments.size(); count > 0; --count)
    {
        quanpin::Segments partial_segments(segments.begin(), segments.begin() + static_cast<std::ptrdiff_t>(count));
        const std::string partial_segmentation = quanpin::join_segments(partial_segments);
        const std::string partial_input = CommonUtils::remove_apostrophe_delimiters(partial_segmentation);
        auto partial_result = query_single_path(partial_input, partial_segmentation, partial_segments);
        if (count == segments.size())
        {
            // 同长度的表往往只剩边角词: ping'guo 整张表就是 苹果 1143881、评过 1180、平果 169、平锅 1,
            // 而 苹果电脑 21495、苹果公司 19725 按音节数分在 tbl_4_p,等长查询永远看不到它们。于是打完
            // 一个完整拼写后,第 2 位起就是冷僻同音词,再往后直接掉到只匹配首音节的单字。
            //
            // 续接词按权重和等长结果并成一组: 权重出自同一份语料,跨表可比。pinyin 仍是用户键入的那串
            // (组字推进必须只消费打出来的部分),canonical_pinyin 记完整读音,造词持久化用得上。
            auto longer = append_longer_phrase_candidates(partial_segmentation, partial_segments);
            if (!longer.empty())
            {
                partial_result.insert(partial_result.end(), std::make_move_iterator(longer.begin()),
                                      std::make_move_iterator(longer.end()));
                sort_candidates_by_weight(partial_result);
            }
        }
        else
        {
            // query_single_path answers an empty dictionary result with a decoder sentence, which is
            // right for the whole key and wrong for a prefix of it: the decoder pads the shorter
            // dictionary word with one arbitrary character, and the row lands at the head of the
            // prefix's group, above every real word there. 你好是 outranked 你好, 开会是 outranked
            // 开会, 版不是 outranked 颁布. Whatever such a row spells is already reachable by picking
            // the shorter word, so drop it and leave the whole-key sentence below as the only one.
            erase_candidates_by_source(partial_result, CandidateSource::Fallback);
        }
        result.insert(result.end(), partial_result.begin(), partial_result.end());
    }

    if (segments.size() >= 3 && quanpin::has_only_complete_pinyin_segments(segments))
    {
        // Keep one local Google-Pinyin sentence as the primary whole-sentence
        // suggestion.  The lattice is a secondary source; it should add only
        // its best path rather than filling the candidate page with near-
        // duplicate low-quality sentences.
        //
        // Both of them rank below a dictionary entry that already answers the
        // whole key, which is what word_lattice.h documents and what
        // whole_sentence_insert_position computes.  This used to insert at
        // index 0 unconditionally: an assembled sentence displaced the exact
        // word, turning 百依百顺 into 白一百顺 and 颁布实施 into 版不是是.
        std::string google_sentence;
        if (sentence_association_.google)
        {
            const std::string normalized =
                CommonUtils::remove_apostrophe_delimiters(effective_segmentation(raw_input, segmentation));
            google_sentence = decoder_.sentence(normalized);
        }
        if (!google_sentence.empty())
        {
            if (!contains_candidate_word(result, google_sentence))
            {
                // Whole-sentence fallbacks must carry their canonical quanpin
                // reading so creating-word learning can persist them.
                const auto at =
                    static_cast<std::ptrdiff_t>(quanpin::whole_sentence_insert_position(result, segments.size()));
                WordItem sentence(effective_segmentation(raw_input, segmentation), google_sentence, 1,
                                  CandidateSource::Fallback, segmentation);
                sentence.sentence_association = true;
                result.insert(result.begin() + at, std::move(sentence));
            }
        }

        const auto rerankers = quanpin::make_neural_rerankers(sentence_association_, neural_keyboard_model_,
                                                              neural_desktop_model_, rescoring_context_);
        const bool need_lattice = sentence_association_.word_lattice || !rerankers.empty();
        const auto lattice_options = quanpin::make_sentence_lattice_options(paths_, sentence_alternatives_);
        quanpin::WholeSentenceComparison sentences;
        if (need_lattice)
        {
            auto options = lattice_options;
            options.nbest =
                rerankers.empty() ? lattice_options.nbest : static_cast<int>(neural::RerankOptions{}.max_paths);
            options.include_lattice_best = sentence_association_.word_lattice;
            options.show_next_on_duplicate = sentence_association_.show_next_on_duplicate;
            quanpin::merge_lattice_candidates(result, segments,
                                              quanpin::make_lattice_db_lookup(db_.get(), statement_cache_,
                                                                              quanpin::QuerySource::Quanpin,
                                                                              options.span_limit),
                                              effective_segmentation(raw_input, segmentation), options, google_sentence,
                                              sentence_association_.google ? &sentences : nullptr, rerankers);
        }
        // A sentence neither source produced on its own, assembled from one source's frame and the
        // other's disputed span. It only exists when it outscored both, so it goes in front of them.
        if (sentences.hybrid_leads(lattice_options.repair_margin) &&
            !contains_candidate_word(result, sentences.best_hybrid))
        {
            const auto at =
                static_cast<std::ptrdiff_t>(quanpin::whole_sentence_insert_position(result, segments.size()));
            WordItem sentence(effective_segmentation(raw_input, segmentation), sentences.best_hybrid,
                              static_cast<std::int64_t>(*sentences.best_hybrid_score * 1000.0),
                              CandidateSource::Generated, segmentation);
            sentence.sentence_association = true;
            result.insert(result.begin() + at, std::move(sentence));
        }
        // The fallback used to be pushed back in front of the lattice unconditionally, which is an
        // argument about sources rather than about sentences: it wins every input where it exists,
        // including the ones where the lattice read the sentence correctly and it did not. Now both
        // are scored on the same terms and the better one leads; a fallback the dictionary cannot
        // spell has no score, and keeps the seat it always had.
        if (sentence_association_.google && !google_sentence.empty() &&
            !sentences.lattice_outranks_fallback(lattice_options.fallback_margin) &&
            !sentences.hybrid_leads(lattice_options.repair_margin))
        {
            // The lattice merge above may have inserted ahead of the fallback. Put the fallback
            // back in front of the lattice row, but still behind the exact dictionary hits.
            const auto boundary =
                static_cast<std::ptrdiff_t>(quanpin::whole_sentence_insert_position(result, segments.size()));
            move_candidate_to_position(result, google_sentence, CandidateSource::Fallback,
                                       static_cast<std::size_t>(boundary));
        }
    }

    if (result.size() < kSparsePinyinFallbackThreshold)
    {
        result = append_sparse_pinyin_fallbacks(segments, std::move(result));
    }

    return result;
}

std::vector<WordItem> QuanpinDictionary::append_longer_phrase_candidates(const std::string &segmentation,
                                                                         const quanpin::Segments &segments)
{
    if (db_ == nullptr)
    {
        return {};
    }
    const auto rows =
        quanpin::query_longer_phrases_keyed(normalize_umlaut_aliases(segments), db_.get(), statement_cache_,
                                            kLongerPhraseExtraSyllables, kLongerPhraseLimit);
    return make_database_candidates(segmentation, rows);
}

std::vector<WordItem> QuanpinDictionary::query_single_path(const std::string &raw_input,
                                                           const std::string &segmentation,
                                                           const quanpin::Segments &segments)
{
    const std::string cache_key = effective_segmentation(raw_input, segmentation);
    if (auto cached = cache_.get(cache_key))
    {
        return cached.value();
    }

    std::vector<WordItem> result = query_database(segments, segmentation);
    result = append_ime_fallback(raw_input, segmentation, std::move(result));
    cache_.insert(cache_key, result);
    return result;
}

quanpin::Segments QuanpinDictionary::resolve_segments(const std::string &raw_input, const std::string &segmentation)
{
    auto segments = segmentation.empty() ? get_or_compute_segments(raw_input) : quanpin::split_segments(segmentation);
    return normalize_umlaut_aliases(std::move(segments));
}

quanpin::Segments QuanpinDictionary::get_or_compute_segments(const std::string &raw_input)
{
    if (auto cached = segmentation_cache_.get(raw_input))
    {
        return cached.value();
    }

    const auto cuts = quanpin::cut_pinyin_by_mode(raw_input, "correction");
    const auto segments = cuts.empty() ? quanpin::Segments{} : cuts.front();
    segmentation_cache_.insert(raw_input, segments);
    return segments;
}

int QuanpinDictionary::handleVkCode(ImeKeyCode vk, ImeModifierMask modifiers_down, ImeCharacter wch)
{
    (void)modifiers_down;

    if (vk == ImeKey::Backspace)
    {
        if (!pinyin_sequence_.empty())
        {
            pinyin_sequence_.pop_back();
        }
    }
    else if (vk == ImeKey::Escape || vk == ImeKey::Return || vk == ImeKey::Space)
    {
        reset_state();
        return OK;
    }
    else if (vk == ImeKey::Apostrophe)
    {
        pinyin_sequence_.push_back('\'');
    }
    else if (ImeKey::is_ascii_letter(vk))
    {
        if (wch >= u'A' && wch <= u'Z')
        {
            pinyin_sequence_.push_back(CommonUtils::lowercase_ascii_char(static_cast<unsigned char>(wch)));
        }
        else if (wch >= u'a' && wch <= u'z')
        {
            pinyin_sequence_.push_back(static_cast<char>(wch));
        }
        else
        {
            pinyin_sequence_.push_back(static_cast<char>(vk + ('a' - 'A')));
        }
    }

    query(pinyin_sequence_);
    return OK;
}

std::vector<WordItem> QuanpinDictionary::query_database(const quanpin::Segments &segments,
                                                        const std::string &segmentation)
{
    if (db_ == nullptr)
    {
        return {};
    }

    try
    {
        if (segments.size() == 1 && segments.front().size() == 1)
        {
            constexpr int kInitialCandidateLimit = 24;
            auto result = query_initial(segments.front(), kInitialCandidateLimit);
            const std::string matched_code = segmentation.empty() ? segments.front() : segmentation;
            for (auto &item : result)
            {
                item.canonical_pinyin = item.pinyin;
                item.pinyin = matched_code;
            }
            return result;
        }

        const auto flat_items = quanpin::query_segments_keyed_flat(segments, db_.get(), statement_cache_, INT_MAX);
        const std::string code = quanpin::join_segments(segments);
        return make_database_candidates(code, flat_items);
    }
    catch (const std::exception &ex)
    {
        (void)0;
        return {};
    }
}

std::vector<WordItem> QuanpinDictionary::query_initial(const std::string &code, int limit)
{
    if (db_ == nullptr || code.size() != 1)
    {
        return {};
    }

    const auto rows = quanpin::query_initial(db_.get(), code, limit);
    return make_database_candidates(rows);
}

std::vector<WordItem> QuanpinDictionary::merge_alternative_segmentations(
    const std::string &raw_input, const std::string &primary_segmentation, const quanpin::Segments &primary_segments,
    const std::vector<quanpin::Segments> &alternative_segmentations, std::vector<WordItem> result)
{
    const auto alternative_items = quanpin::query_exact_segmentations_keyed_flat(
        alternative_segmentations, db_.get(), statement_cache_, kAlternativeSegmentationCandidateLimit);
    if (alternative_items.empty())
    {
        return result;
    }

    const auto primary_full = query_single_path(raw_input, primary_segmentation, primary_segments);
    auto alternative_full = make_database_candidates(alternative_items);

    std::vector<WordItem> merged_full = primary_full;
    merged_full.insert(merged_full.end(), alternative_full.begin(), alternative_full.end());
    sort_candidates_by_weight(merged_full);
    deduplicate_candidates_by_word(merged_full);

    // Promote an alternative only when its best weight is at least one percent
    // of the primary reading's top weight; rare re-segmentations are noise.
    const std::int64_t primary_top_weight = primary_full.empty() ? 0 : primary_full.front().weight;
    const bool promote_alternative =
        static_cast<std::int64_t>(alternative_items.front().weight) * kAlternativeSegmentationPromotionRatio >=
        primary_top_weight;
    const std::string &best_alternative_word = alternative_items.front().value;
    const auto best_alternative = std::find_if(merged_full.begin(), merged_full.end(), [&](const WordItem &item) {
        return item.word == best_alternative_word;
    });
    const size_t best_alternative_index =
        best_alternative == merged_full.end()
            ? 0
            : static_cast<size_t>(std::distance(merged_full.begin(), best_alternative));
    if (promote_alternative && best_alternative != merged_full.end() &&
        best_alternative_index >= kAlternativeSegmentationFirstPageSize)
    {
        WordItem promoted = std::move(*best_alternative);
        merged_full.erase(best_alternative);
        merged_full.insert(merged_full.begin() + static_cast<std::ptrdiff_t>(kBestAlternativeSegmentationMaxIndex),
                           std::move(promoted));
    }

    std::vector<WordItem> merged = std::move(merged_full);
    // Append all of result, not a suffix of it: query_series prepends whole-sentence candidates, so any index
    // arithmetic that assumes result starts with primary_full silently drops them. append_unique_candidates_copy dedups
    // by word, so the primary rows already in merged are not duplicated.
    append_unique_candidates_copy(merged, result);
    return merged;
}

std::vector<WordItem> QuanpinDictionary::append_ime_fallback(const std::string &raw_input,
                                                             const std::string &segmentation,
                                                             std::vector<WordItem> result)
{
    if (!result.empty())
    {
        return result;
    }

    if (!sentence_association_.google)
        return result;
    const std::string normalized =
        CommonUtils::remove_apostrophe_delimiters(effective_segmentation(raw_input, segmentation));
    const std::string sentence = decoder_.sentence(normalized);
    if (sentence.empty())
    {
        return result;
    }

    if (!contains_candidate_word(result, sentence))
    {
        // Keep the complete reading for the creating-word persistence path.
        result.emplace_back(effective_segmentation(raw_input, segmentation), sentence, 1, CandidateSource::Fallback,
                            segmentation);
        result.back().sentence_association = true;
    }
    return result;
}

std::vector<WordItem> QuanpinDictionary::append_sparse_pinyin_fallbacks(const quanpin::Segments &segments,
                                                                        std::vector<WordItem> result)
{
    for (const auto &fallback_segments : quanpin::sparse_pinyin_fallback_segments(segments))
    {
        if (fallback_segments.empty())
        {
            continue;
        }

        const std::string fallback_segmentation = quanpin::join_segments(fallback_segments);
        const std::string fallback_input = CommonUtils::remove_apostrophe_delimiters(fallback_segmentation);
        const auto fallback_result = query_single_path(fallback_input, fallback_segmentation, fallback_segments);
        append_unique_candidates_copy(result, fallback_result);
    }
    return result;
}

void QuanpinDictionary::mark_autocorrect_candidates(std::vector<WordItem> &candidates, const std::string &raw_input)
{
    // A candidate comes from the corrected interpretation exactly when its code
    // letters equal the primary segmentation letters while those differ from the
    // typed letters. The first condition alone would also sweep up prefix
    // candidates (keneng -> ke, single-letter jianpin expansions) that the user
    // spelled correctly; both rules together keep those unmarked. Deliberately
    // switch-independent: the scheme alias layer rewrites letters regardless of
    // the autocorrect switches, so an alias-corrected candidate is labelled as
    // such even with autocorrection off.
    const std::string primary_letters = fold_autocorrect_letters(pinyin_segmentation_);
    const std::string raw_letters = fold_autocorrect_letters(raw_input);
    if (primary_letters.empty() || primary_letters == raw_letters)
    {
        return;
    }
    for (auto &item : candidates)
    {
        if (!item.corrected_from.empty())
        {
            continue;
        }
        if (fold_autocorrect_letters(item.pinyin) == primary_letters)
        {
            item.corrected_from = raw_letters;
        }
    }
}

int QuanpinDictionary::create_word(std::string pinyin, std::string word)
{
    pinyin = CommonUtils::remove_apostrophe_delimiters(pinyin);
    const auto cuts = quanpin::cut_pinyin_by_mode(pinyin, "correction");
    if (cuts.empty())
    {
        return ERROR_CODE;
    }

    const auto segments = normalize_umlaut_aliases(cuts.front());
    pinyin = quanpin::join_segments(segments);
    const std::string jp = quanpin::segments_to_jianpin(segments);
    if (!quanpin::has_valid_word_pinyin(pinyin, jp, word))
    {
        return ERROR_CODE;
    }

    const auto insert_result = quanpin::insert_word_if_missing(db_.get(), pinyin, jp, word);
    return quanpin::complete_word_insert(insert_result,
                                         metasequoia::path_to_utf8(paths_.user(metasequoia::assets::user_journal)),
                                         pinyin, word, [this] { reset_cache(); })
               ? OK
               : ERROR_CODE;
}

int QuanpinDictionary::create_word_from_canonical_pinyin(std::string pinyin, std::string word)
{
    const auto segments = quanpin::split_segments(pinyin);
    if (!quanpin::has_expected_complete_pinyin_segments(segments, HelpcodeUtils::count_han_chars(word)))
    {
        return ERROR_CODE;
    }

    pinyin = quanpin::join_segments(segments);
    const std::string jp = quanpin::segments_to_jianpin(segments);
    // The caller supplied explicit canonical segmentation. Re-running the greedy correction
    // validator would erase those boundaries and reject valid readings such as qi'e'huan.
    const auto insert_result = quanpin::insert_word_if_missing(db_.get(), pinyin, jp, word);
    return quanpin::complete_word_insert(insert_result,
                                         metasequoia::path_to_utf8(paths_.user(metasequoia::assets::user_journal)),
                                         pinyin, word, [this] { reset_cache(); })
               ? OK
               : ERROR_CODE;
}

int QuanpinDictionary::update_weight_by_pinyin_and_word(std::string pinyin, std::string word)
{
    pinyin = CommonUtils::remove_apostrophe_delimiters(pinyin);
    const auto cuts = quanpin::cut_pinyin_by_mode(pinyin, "correction");
    if (cuts.empty())
        return ERROR_CODE;
    auto segments = cuts.front();
    segments = normalize_umlaut_aliases(std::move(segments));
    const size_t han_count = HelpcodeUtils::count_han_chars(word);
    if (segments.size() > han_count)
        segments.resize(han_count);
    const std::string normalized = quanpin::join_segments(segments);
    const bool updated =
        metasequoia::sqlite_execute_statement(db_.get(), quanpin::build_sql_for_updating_word(normalized, word));
    return quanpin::complete_pinyin_weight_update(
               updated, db_path_, normalized, word,
               metasequoia::path_to_utf8(paths_.user(metasequoia::assets::user_journal)), [this] { reset_cache(); })
               ? OK
               : ERROR_CODE;
}

int QuanpinDictionary::delete_by_pinyin_and_word(std::string pinyin, std::string word)
{
    pinyin = CommonUtils::remove_apostrophe_delimiters(pinyin);
    const auto cuts = quanpin::cut_pinyin_by_mode(pinyin, "correction");
    if (cuts.empty())
        return ERROR_CODE;
    const std::string normalized = quanpin::join_segments(cuts.front());
    if (!user_dictionary::delete_dictionary_candidate(
            db_path_, metasequoia::path_to_utf8(paths_.user(metasequoia::assets::user_journal)),
            user_dictionary::DictionaryKind::Pinyin, normalized, word))
        return ERROR_CODE;
    reset_cache();
    return OK;
}

int QuanpinDictionary::insert_word_to_series_cache(const std::string &pinyin, const std::string &word,
                                                   CandidateSource source)
{
    if (pinyin.empty() || word.empty())
    {
        return ERROR_CODE;
    }

    const auto cuts = quanpin::cut_pinyin_by_mode(pinyin, "correction");
    const std::string segmentation = cuts.empty() ? pinyin : quanpin::join_segments(cuts.front());
    const std::string cache_key = series_cache_key(pinyin, segmentation);
    return insert_word_to_series_cache_key(cache_key, pinyin, word, source);
}

int QuanpinDictionary::insert_word_to_series_cache(const std::string &raw_input, const std::string &segmentation,
                                                   unsigned autocorrect_types, const std::string &word,
                                                   CandidateSource source)
{
    if (raw_input.empty() || word.empty())
    {
        return ERROR_CODE;
    }

    const auto segments = resolve_segments(raw_input, segmentation);
    const auto resolution = resolve_series_query(raw_input, segments, autocorrect_types);
    return insert_word_to_series_cache_key(resolution.cache_key, raw_input, word, source);
}

int QuanpinDictionary::insert_word_to_series_cache_key(const std::string &cache_key, const std::string &pinyin,
                                                       const std::string &word, CandidateSource source)
{
    if (is_online_candidate_source(source))
        return insert_word_to_series_cache_key(cache_key, pinyin, std::vector<std::string>{word}, source);
    return series_cache_.update_or_insert(cache_key,
                                          [&](auto &list) {
                                              insert_cached_candidate(list, pinyin, word, source);
                                              return true;
                                          })
               ? OK
               : ERROR_CODE;
}

void QuanpinDictionary::reset_state()
{
    pinyin_sequence_.clear();
    pinyin_segmentation_.clear();
    current_candidate_list_.clear();
}

void QuanpinDictionary::reset_cache()
{
    cache_.clear();
    series_cache_.clear();
    segmentation_cache_.clear();
}

std::vector<WordItem> QuanpinDictionary::fuzzy_candidates(const std::string &segmentation,
                                                          metasequoia::FuzzyPinyinOptions options)
{
    std::vector<WordItem> result;
    if (!options.rules || !db_)
        return result;
    metasequoia::reset_if_sqlite_data_version_changed(db_.get(), data_version_, [this] { reset_cache(); });
    const auto cache_key = "fuzzy:" + std::to_string(options.rules) + ":" + segmentation;
    if (const auto cached = series_cache_.get(cache_key))
        return *cached;
    const auto segments = quanpin::split_segments(segmentation);
    std::size_t budget = 128;
    for (std::size_t count = segments.size(); count > 0 && budget > 1; --count)
    {
        const quanpin::Segments prefix(segments.begin(), segments.begin() + count);
        const auto paths = quanpin::fuzzy_segmentations(prefix, options, std::min<std::size_t>(64, budget));
        if (paths.empty())
            continue;
        budget -= paths.size();
        const auto rows = quanpin::query_exact_segmentations_keyed_flat(paths, db_.get(), statement_cache_, 128);
        for (const auto &row : rows)
        {
            WordItem item(quanpin::join_segments(prefix), row.value, row.weight, CandidateSource::Database, row.key);
            item.fuzzy = true;
            result.push_back(std::move(item));
        }
    }
    series_cache_.insert(cache_key, result);
    return result;
}

std::vector<WordItem> QuanpinDictionary::query(const std::string &raw_input, const std::string &segmentation,
                                               unsigned autocorrect, metasequoia::FuzzyPinyinOptions fuzzy)
{
    auto result = query_exact(raw_input, segmentation, autocorrect);
    if (fuzzy.rules && !raw_input.empty())
    {
        // Keep ordinary cache slots free of preference-specific candidates.
        const auto typed =
            segmentation.empty() ? quanpin::join_segments(resolve_segments(raw_input, segmentation)) : segmentation;
        append_unique_candidates_copy(result, fuzzy_candidates(typed, fuzzy));
        const auto matched_letters = [](const WordItem &item) {
            size_t letters = 0;
            for (const char ch : item.pinyin)
                letters += ch != '\'';
            return letters;
        };
        std::stable_sort(result.begin(), result.end(),
                         [&](const auto &a, const auto &b) { return matched_letters(a) > matched_letters(b); });
    }
    // Labeling runs after every mutation (including the fuzzy merge) so the
    // returned list and the published candidate list always agree.
    mark_autocorrect_candidates(result, raw_input);
    current_candidate_list_ = result;
    return result;
}

int QuanpinDictionary::insert_word_to_series_cache(const std::string &pinyin, const std::vector<std::string> &words,
                                                   CandidateSource source)
{
    if (pinyin.empty() || words.empty())
    {
        return ERROR_CODE;
    }

    const auto cuts = quanpin::cut_pinyin_by_mode(pinyin, "correction");
    const std::string segmentation = cuts.empty() ? pinyin : quanpin::join_segments(cuts.front());
    const std::string cache_key = series_cache_key(pinyin, segmentation);
    return insert_word_to_series_cache_key(cache_key, pinyin, words, source);
}

int QuanpinDictionary::insert_word_to_series_cache(const std::string &raw_input, const std::string &segmentation,
                                                   unsigned autocorrect_types, const std::vector<std::string> &words,
                                                   CandidateSource source)
{
    if (raw_input.empty() || words.empty())
    {
        return ERROR_CODE;
    }

    const auto segments = resolve_segments(raw_input, segmentation);
    const auto resolution = resolve_series_query(raw_input, segments, autocorrect_types);
    return insert_word_to_series_cache_key(resolution.cache_key, raw_input, words, source);
}

int QuanpinDictionary::insert_word_to_series_cache_key(const std::string &cache_key, const std::string &pinyin,
                                                       const std::vector<std::string> &words, CandidateSource source)
{
    return series_cache_.update_or_insert(
               cache_key, [&](auto &list) { return replace_online_candidate_batch(list, pinyin, words, source); })
               ? OK
               : ERROR_CODE;
}
