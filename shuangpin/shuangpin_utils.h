#pragma once

#include "shuangpin_profile.h"
#include <string>

class ShuangpinUtil
{
  public:
    static std::string get_local_appdata_path();

    static std::string cvt_single_sp_to_pinyin(std::string sp_str,
                                               const ShuangpinProfile &profile = GetXiaoheShuangpinProfile());
    static std::string pinyin_segmentation(std::string sp_str,
                                           const ShuangpinProfile &profile = GetXiaoheShuangpinProfile());
    // True when a delimiter-free chunk decodes to a syllable the shuangpin
    // segmentation accepts. pinyin_segmentation and the raw unit boundaries
    // (shuangpin::segment_raw_boundaries) both decide with this predicate so a
    // deleted unit can never disagree with the displayed segmentation.
    static bool is_accepted_syllable_code(const std::string &sp_str,
                                          const ShuangpinProfile &profile = GetXiaoheShuangpinProfile());
    static bool is_all_complete_pinyin(std::string pure_pinyin, std::string seg_pinyin);
    static std::string convert_seg_shuangpin_to_seg_complete_pinyin(
        std::string seg_shangpin, const ShuangpinProfile &profile = GetXiaoheShuangpinProfile());

    static bool IsFullHelpMode(std::string pinyin, const ShuangpinProfile &profile = GetXiaoheShuangpinProfile());
    static std::string GetFullHelpCodes(std::string pinyin);
};
