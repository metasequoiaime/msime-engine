#pragma once

#include <string>

namespace CommonUtils
{
std::wstring string_to_wstring(const std::string &str);
std::string wstring_to_string(const std::wstring &wstr);
std::string remove_apostrophe_delimiters(const std::string &text);
std::string escape_sql_literal(std::string text);
bool is_ascii_letters_or_apostrophe(const std::string &text);
std::string lowercase_ascii(std::string text);
} // namespace CommonUtils
