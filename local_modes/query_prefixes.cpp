#include "query_prefixes.h"

#include "../common/string_utils.h"
#include "../shuangpin/shuangpin_query.h"

namespace metasequoia::local_modes
{
std::vector<std::string> normalized_query_prefixes(const std::string &code, SchemeType scheme,
                                                   const ShuangpinProfile &profile)
{
    const std::string lower = CommonUtils::lowercase_ascii(code);
    std::vector<std::string> prefixes{lower};
    if (scheme == SchemeType::Shuangpin)
    {
        const std::string quanpin = shuangpin::normalize_input(lower, profile);
        if (!quanpin.empty() && quanpin != lower)
        {
            prefixes.push_back(quanpin);
        }
    }
    return prefixes;
}
} // namespace metasequoia::local_modes
