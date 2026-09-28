#pragma once

#include "../core/scheme_type.h"
#include "../shuangpin/shuangpin_profile.h"

#include <string>
#include <vector>

namespace metasequoia::local_modes
{
std::vector<std::string> normalized_query_prefixes(const std::string &code, SchemeType scheme,
                                                   const ShuangpinProfile &profile);
} // namespace metasequoia::local_modes
