#include "candidate_utils.h"
#include "nine_key_session.h"
#include "../quanpin/quanpin_utils.h"
#include "../user_dictionary/user_dictionary_journal.h"
#include "../common/helpcode_utils.h"
#include "../common/string_utils.h"
#include <algorithm>
#include <filesystem>
#include <system_error>
#include "contracts/assets/assets.h"
#include <unordered_set>

namespace metasequoia
{
namespace
{
const char *frequency_mode_name(FrequencyAdjustmentMode mode)
{
    switch (mode)
    {
    case FrequencyAdjustmentMode::Disabled:
        return "disabled";
    case FrequencyAdjustmentMode::Pin:
        return "pin";
    case FrequencyAdjustmentMode::Halve:
        return "halve";
    case FrequencyAdjustmentMode::Linear:
        return "linear";
    case FrequencyAdjustmentMode::Promote:
        return "promote";
    }
    return "disabled";
}
constexpr std::size_t kPathLimit = 48;
constexpr std::size_t kDigitLimit = 32;
std::string encode(const std::string &pinyin)
{
    constexpr char keys[] = "22233344455566677778889999";
    std::string result;
    for (char c : pinyin)
        if (CommonUtils::is_ascii_lowercase(static_cast<unsigned char>(c)))
            result += keys[c - 'a'];
    return result;
}
struct Spelling
{
    std::string text, digits;
};
const std::vector<Spelling> &spellings()
{
    static const auto values = [] {
        std::vector<Spelling> result;
        std::unordered_set<std::string> seen;
        // Include partial final syllables, but never invent non-pinyin letter combinations.
        for (const auto &syllable : quanpin::intact_pinyin_list())
            for (std::size_t n = 1; n <= syllable.size(); ++n)
            {
                auto prefix = syllable.substr(0, n);
                if (seen.insert(prefix).second)
                    result.push_back({prefix, encode(prefix)});
            }
        return result;
    }();
    return values;
}

// 九键上的字母只印在键面上,打字时按的是数字,所以英文候选得把数字还原成字母才查得到。
// T9 expansion multiplies by three or four per digit, so only the leading digits are expanded and
// the dictionary's own prefix search carries the rest; a word is then kept only if its whole code
// starts with what was typed.
constexpr const char *kDigitLetters[] = {"", "", "abc", "def", "ghi", "jkl", "mno", "pqrs", "tuv", "wxyz"};

std::string LettersForDigit(char digit)
{
    return digit >= '2' && digit <= '9' ? kDigitLetters[digit - '0'] : std::string{};
}

std::string DigitsForWord(const std::string &word)
{
    std::string code;
    code.reserve(word.size());
    for (const unsigned char raw : word)
    {
        const char letter = CommonUtils::lowercase_ascii_char(raw);
        if (!CommonUtils::is_ascii_lowercase(static_cast<unsigned char>(letter)))
            return {};
        for (char digit = '2'; digit <= '9'; ++digit)
        {
            const auto letters = LettersForDigit(digit);
            if (letters.find(letter) != std::string::npos)
            {
                code.push_back(digit);
                break;
            }
        }
    }
    return code;
}

std::vector<std::string> LetterPrefixes(const std::string &digits, std::size_t budget)
{
    std::vector<std::string> prefixes{std::string{}};
    for (const char digit : digits)
    {
        const auto letters = LettersForDigit(digit);
        if (letters.empty() || prefixes.size() * letters.size() > budget)
            break;
        std::vector<std::string> grown;
        grown.reserve(prefixes.size() * letters.size());
        for (const auto &prefix : prefixes)
            for (const char letter : letters)
                grown.push_back(prefix + letter);
        prefixes = std::move(grown);
    }
    return prefixes.size() == 1 && prefixes.front().empty() ? std::vector<std::string>{} : prefixes;
}

using Path = std::vector<std::string>;
std::vector<Path> paths(const std::string &digits)
{
    std::vector<std::vector<Path>> suffix(digits.size() + 1);
    suffix.back().push_back({});
    for (std::size_t offset = digits.size(); offset-- > 0;)
    {
        auto &result = suffix[offset];
        for (const auto &spelling : spellings())
        {
            const auto end = offset + spelling.digits.size();
            if (end > digits.size() || digits.compare(offset, spelling.digits.size(), spelling.digits) != 0)
                continue;
            if (end != digits.size() && !quanpin::intact_pinyin_set().count(spelling.text))
                continue;
            for (const auto &tail : suffix[end])
            {
                Path path{spelling.text};
                path.insert(path.end(), tail.begin(), tail.end());
                result.push_back(std::move(path));
            }
        }
        std::stable_sort(result.begin(), result.end(), [](const Path &a, const Path &b) {
            if (a.size() != b.size())
                return a.size() < b.size();
            const bool ac = quanpin::intact_pinyin_set().count(a.back());
            const bool bc = quanpin::intact_pinyin_set().count(b.back());
            if (ac != bc)
                return ac;
            return a < b;
        });
        if (result.size() > kPathLimit)
            result.resize(kPathLimit);
    }
    return suffix.front();
}
} // namespace

std::size_t NineKeySession::locked_length() const
{
    std::size_t count = 0;
    for (const auto &part : locked_)
        count += part.size();
    return count;
}

std::vector<WordItem> NineKeySession::english_candidates()
{
    // In English-only mode the words are the whole answer rather than an addition to the pinyin list, so neither the
    // mixed-candidate setting nor the prefix length that keeps a mixed list readable applies: one digit already narrows
    // the alphabet enough to be worth showing.
    if (english_only_)
    {
        if (digits_.empty())
            return {};
    }
    else if (!english_.mixed_candidates || !locked_.empty() || digits_.size() < english_.minimum_prefix)
        return {};
    if (!english_dictionary_)
    {
        const auto path = paths_.dictionary(assets::english_dictionary);
        std::error_code code;
        if (!std::filesystem::exists(path, code))
            return {};
        english_dictionary_ = std::make_unique<EnglishDictionary>(path.string(), false);
    }
    std::vector<WordItem> words;
    std::unordered_set<std::string> seen;
    for (const auto &prefix : LetterPrefixes(digits_, 64))
    {
        for (auto &word : english_dictionary_->query_prefix(prefix, 5))
        {
            // 只有整串编码对得上的才算,否则展开之外的字母会混进来。
            if (!CommonUtils::starts_with(DigitsForWord(word.word), digits_) || !seen.insert(word.word).second)
                continue;
            words.push_back(std::move(word));
        }
    }
    // 打满几位就先给几个字母的词,同长度再比词频。
    //
    // Prefix matches belong in the list -- a nine-key code is also the start of longer words -- but
    // not ahead of the word the code spells exactly. Ranked on frequency alone, typing 65 for "ok"
    // led with "old", which is simply the more common word and not the one being asked for.
    //
    // A word the frequency table scores at zero does not get that privilege. Spelling the code
    // exactly is evidence the user meant it only when the word is one people type: 64426 is 你好,
    // and `ogham` -- the name of an early Irish alphabet -- is the only five-letter English word
    // those keys can spell, so it won this comparison against every commoner prefix match and led
    // the English list outright.
    const auto typed = digits_.size();
    std::stable_sort(words.begin(), words.end(), [typed](const WordItem &a, const WordItem &b) {
        const bool a_exact = a.word.size() == typed && a.weight > 0;
        const bool b_exact = b.word.size() == typed && b.weight > 0;
        if (a_exact != b_exact)
            return a_exact;
        return a.weight != b.weight ? a.weight > b.weight : a.word.size() < b.word.size();
    });
    if (words.size() > 5)
        words.resize(5);
    return words;
}

void NineKeySession::refresh()
{
    candidates_.clear();
    spellings_.clear();
    if (!active())
        return;
    if (english_only_)
    {
        // No syllables to offer and no pinyin to look up: the digits stand for letters only.
        candidates_ = english_candidates();
        return;
    }
    if (!dictionary_)
        dictionary_ = std::make_unique<QuanpinDictionary>(std::string{}, paths_);
    const auto remaining = digits_.substr(locked_length());
    for (const auto &syllable : quanpin::intact_pinyin_list())
    {
        const auto code = encode(syllable);
        if (!remaining.empty() && locked_length() + std::max(remaining.size(), code.size()) <= kDigitLimit &&
            (CommonUtils::starts_with(remaining, code) || CommonUtils::starts_with(code, remaining)))
            spellings_.push_back(syllable);
    }
    std::stable_sort(spellings_.begin(), spellings_.end(), [&remaining](const auto &a, const auto &b) {
        // 九键输入长度以数字位数计算；直接比较拼音字母数会把同一数字前缀下
        // 的候选顺序排错（例如一个两位数字才能完成的音节被提前）。
        const auto al = std::min(encode(a).size(), remaining.size()), bl = std::min(encode(b).size(), remaining.size());
        return al != bl ? al > bl : a < b;
    });
    auto alternatives = paths(remaining);
    // Even an unfinished/invalid tail must still offer the leading syllable for partial selection.
    for (const auto &spelling : spellings_)
        if (spelling.size() <= remaining.size())
            alternatives.push_back({spelling});
    if (remaining.empty())
        alternatives = {{}};
    const auto locked_key = quanpin::join_segments(locked_);
    std::unordered_set<std::string> queried;
    for (auto path : alternatives)
    {
        path.insert(path.begin(), locked_.begin(), locked_.end());
        const auto key = quanpin::join_segments(path);
        if (key.empty() || !queried.insert(key).second)
            continue;
        for (auto candidate : dictionary_->query(key, key, false, fuzzy_))
        {
            const auto &canonical = candidate_canonical_pinyin(candidate);
            if (std::count(canonical.begin(), canonical.end(), '\'') >= static_cast<long>(path.size()))
                continue;
            const auto matched = candidate.fuzzy ? candidate.pinyin : canonical;
            const auto code = encode(matched);
            if (code.empty() || (!CommonUtils::starts_with(code, digits_) && !CommonUtils::starts_with(digits_, code)))
                continue;
            if (!locked_key.empty() && matched != locked_key && !CommonUtils::starts_with(matched, locked_key + "'") &&
                !CommonUtils::starts_with(locked_key, matched + "'"))
                continue;
            candidate.pinyin = digits_.substr(0, std::min(code.size(), digits_.size()));
            candidate.canonical_pinyin = canonical;
            candidates_.push_back(std::move(candidate));
        }
    }
    // 合成候选与词典词条的 weight 不是同一个尺度：词典的是语料词频，合成整句的是解码得分。两者放在
    // 一起按 weight 比大小时，吃掉全部按键的「你好哦」(56084) 会压过同样吃掉全部按键的真词「你敢」
    // (35600)、「你搞」(9350) 和「蜜柑」(7539)。64426 因此出成「你好 / 你好哦 / 你敢哦 / 米糕哦 /
    // 密函哦 / 你敢」——四个拼出来的东西挤在真词前面，而它们没有一个是词。
    //
    // 按来源分档，覆盖按键数相同时词典词条先行。整句输入不受影响：长输入里能吃掉全部按键的词条本来
    // 就没有，合成候选仍然排第一（96436426 出「我很好」，9664486 出「用户名」）。
    const auto synthesised = [](CandidateSource source) { return is_generated_or_fallback_source(source); };
    std::stable_sort(candidates_.begin(), candidates_.end(), [&synthesised](const auto &a, const auto &b) {
        if (a.pinyin.size() != b.pinyin.size())
            return a.pinyin.size() > b.pinyin.size();
        if (synthesised(a.source) != synthesised(b.source))
            return !synthesised(a.source);
        if (a.fuzzy != b.fuzzy)
            return !a.fuzzy;
        return a.weight > b.weight;
    });
    deduplicate_candidates_by_word(candidates_);
    if (candidates_.size() > 128)
        candidates_.resize(128);
    auto english = english_candidates();
    if (!english.empty())
    {
        // Second place is ahead of every pinyin reading but the first, which is worth it for a word
        // the user is plainly spelling and not for one the frequency table has never seen. A
        // zero-weight word still belongs in the list -- the digits do spell it -- at the end of it.
        const auto slot =
            english.front().weight > 0 ? std::min<std::size_t>(1, candidates_.size()) : candidates_.size();
        candidates_.insert(candidates_.begin() + static_cast<std::ptrdiff_t>(slot), english.front());
        candidates_.insert(candidates_.end(), std::make_move_iterator(english.begin() + 1),
                           std::make_move_iterator(english.end()));
    }
    user_dictionary::apply_fixed_positions(path_to_utf8(paths_.user(assets::user_journal)), ranking_context(),
                                           candidates_, false);
    // Keep the most likely reading visible without requiring a horizontal scroll.
    if (!candidates_.empty())
    {
        const auto &canonical = candidates_.front().canonical_pinyin;
        const auto offset = locked_key.empty() ? 0 : locked_key.size() + 1;
        if (offset < canonical.size())
        {
            const auto preferred = canonical.substr(offset, canonical.find('\'', offset) - offset);
            const auto found = std::find(spellings_.begin(), spellings_.end(), preferred);
            if (found != spellings_.end())
                std::rotate(spellings_.begin(), found, found + 1);
        }
    }
}
KeyResult NineKeySession::character(char digit)
{
    if (digit < '2' || digit > '9')
        return {};
    if (digits_.size() >= kDigitLimit)
        return {true, {}, "九键最多输入 32 位，请先选择候选"};
    digits_ += digit;
    refresh();
    return {true, {}, {}};
}
KeyResult NineKeySession::select(std::size_t index)
{
    if (index >= candidates_.size())
        return {};
    const auto selected = candidates_[index];
    std::optional<std::string> diagnostic;
    if (learning_ && frequency_.mode != FrequencyAdjustmentMode::Disabled && index != 0 &&
        selected.fixed_position == 0 && editable(index))
        diagnostic = adjust_frequency(index, false);
    digits_.erase(0, selected.pinyin.size());
    auto consumed = selected.pinyin.size();
    while (!locked_.empty() && consumed >= locked_.front().size())
    {
        consumed -= locked_.front().size();
        locked_.erase(locked_.begin());
    }
    refresh();
    return {true, selected.word, std::move(diagnostic)};
}
KeyResult NineKeySession::finish(std::size_t index)
{
    if (!active())
        return {};
    std::string commit;
    std::optional<std::string> diagnostic;
    if (index >= candidates_.size())
        return command(Command::CommitRaw);
    while (active())
    {
        if (candidates_.empty())
        {
            commit += digits_;
            break;
        }
        const auto result = select(index);
        if (!diagnostic && result.diagnostic)
            diagnostic = result.diagnostic;
        if (result.commit)
            commit += *result.commit;
        index = 0;
    }
    command(Command::Cancel);
    return {true, commit, std::move(diagnostic)};
}

std::string NineKeySession::ranking_context() const
{
    return "nine-key:" + digits_ + ":" + quanpin::join_segments(locked_);
}

bool NineKeySession::editable(std::size_t index) const
{
    if (index >= candidates_.size())
        return false;
    const auto &item = candidates_[index];
    return !item.canonical_pinyin.empty() && is_dictionary_candidate_source(item.source);
}

std::optional<std::string> NineKeySession::adjust_frequency(std::size_t index, bool force_top)
{
    const auto &item = candidates_[index];
    bool changed = false;
    if (!user_dictionary::adjust_candidate_ranking(
            path_to_utf8(paths_.dictionary(assets::main_dictionary)), path_to_utf8(paths_.user(assets::user_journal)),
            ranking_context(), candidates_, item.canonical_pinyin, item.word,
            force_top ? "pin" : frequency_mode_name(frequency_.mode), frequency_.linear_step, frequency_.trigger_count,
            force_top, &changed))
        return "Unable to persist nine-key candidate frequency adjustment.";
    if (changed && dictionary_)
        dictionary_->reset_cache();
    return {};
}

KeyResult NineKeySession::pin(std::size_t index)
{
    if (!editable(index))
        return {};
    auto diagnostic = adjust_frequency(index, true);
    refresh();
    return {true, {}, std::move(diagnostic)};
}

KeyResult NineKeySession::remove(std::size_t index)
{
    if (!editable(index) || CommonUtils::count_utf8_chars(candidates_[index].word) <= 1)
        return {};
    const auto item = candidates_[index];
    if (!user_dictionary::delete_dictionary_candidate(
            path_to_utf8(paths_.dictionary(assets::main_dictionary)), path_to_utf8(paths_.user(assets::user_journal)),
            user_dictionary::DictionaryKind::Pinyin, item.canonical_pinyin, item.word))
        return {true, {}, "Unable to persist nine-key candidate removal."};
    dictionary_->reset_cache();
    refresh();
    return {true, {}, {}};
}

KeyResult NineKeySession::set_position(std::size_t index, int position)
{
    if (!editable(index) || position < 0 || position > 5)
        return {};
    const auto &item = candidates_[index];
    const auto journal = path_to_utf8(paths_.user(assets::user_journal));
    const bool saved = position == 0 ? user_dictionary::clear_fixed_position(journal, ranking_context(),
                                                                             item.canonical_pinyin, item.word)
                                     : user_dictionary::set_fixed_position(journal, ranking_context(),
                                                                           item.canonical_pinyin, item.word, position);
    if (!saved)
        return {true, {}, "Unable to persist nine-key candidate position."};
    refresh();
    return {true, {}, {}};
}
KeyResult NineKeySession::command(Command command)
{
    if (!active())
        return {};
    if (command == Command::CommitCandidate)
        return select(0);
    if (command == Command::CommitRaw)
    {
        auto raw = digits_;
        this->command(Command::Cancel);
        return {true, raw, {}};
    }
    if (command == Command::Cancel)
    {
        digits_.clear();
        locked_.clear();
    }
    else if (command == Command::Backspace)
    {
        digits_.pop_back();
        while (locked_length() > digits_.size())
            locked_.pop_back();
    }
    else
        return {};
    refresh();
    return {true, {}, {}};
}
KeyResult NineKeySession::choose_spelling(std::size_t index)
{
    if (index >= spellings_.size())
        return {};
    const auto spelling = spellings_[index];
    const auto offset = locked_length();
    digits_.replace(offset, std::min(spelling.size(), digits_.size() - offset), encode(spelling));
    locked_.push_back(spelling);
    refresh();
    return {true, {}, {}};
}
SessionSnapshot NineKeySession::snapshot() const
{
    SessionSnapshot result{};
    result.scheme = SchemeType::Quanpin;
    result.local_mode = LocalInputMode::None;
    // The grid's snapshot stands in for the whole session's while it is composing, so it has to carry the mode too: a
    // host that draws its English keys from this flag would otherwise put Chinese ones back the moment the first digit
    // arrived.
    result.dedicated_english = english_only_;
    result.preedit = quanpin::join_segments(locked_);
    if (active() && locked_length() < digits_.size())
    {
        if (!result.preedit.empty())
            result.preedit += "'";
        result.preedit += digits_.substr(locked_length());
    }
    result.editing_text = digits_;
    result.caret_position = digits_.size();
    result.candidates = candidates_;
    result.candidate_sources.reserve(result.candidates.size());
    for (const auto &candidate : result.candidates)
        result.candidate_sources.push_back(candidate.source);
    result.candidate_annotations.reserve(result.candidates.size());
    for (const auto &candidate : result.candidates)
        result.candidate_annotations.push_back(candidate.corrected_from);
    // The grid consumes digits rather than letters, so the test is the one `select` performs:
    // `digits_.erase(0, candidate.pinyin.size())` empties the buffer exactly when the candidate's
    // code covers everything still typed.
    result.candidate_answers_key.reserve(result.candidates.size());
    for (const auto &candidate : result.candidates)
        result.candidate_answers_key.push_back(candidate.pinyin.size() >= digits_.size());
    result.nine_key_spellings = spellings_;
    return result;
}
} // namespace metasequoia
