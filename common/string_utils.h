#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace CommonUtils
{
std::wstring string_to_wstring(const std::string &str);
std::string wstring_to_string(const std::wstring &wstr);
bool starts_with(std::string_view text, std::string_view prefix);
bool is_ascii_lowercase(unsigned char character);
bool is_ascii_uppercase(unsigned char character);
bool is_ascii_letter(unsigned char character);
bool is_ascii_digit(unsigned char character);
bool is_ascii_hex_digit(unsigned char character);
bool is_ascii_lowercase_string(std::string_view text);
char lowercase_ascii_char(unsigned char character);
char uppercase_ascii_char(unsigned char character);
std::string::size_type count_utf8_chars(const std::string &text);
std::string last_utf8_characters(std::string_view text, std::size_t count);
std::size_t utf8_code_point_length(std::string_view text);
std::vector<std::size_t> utf8_boundaries(const std::string &text);
std::vector<std::string> split_by_delimiter(const std::string &text, char delimiter);
std::string remove_apostrophe_delimiters(const std::string &text);
std::string escape_sql_literal(std::string text);
bool is_ascii_letters_or_apostrophe(const std::string &text);
std::string lowercase_ascii(std::string text);
// Returns the exclusive upper bound for a prefix over lowercase ASCII keys.
std::string ascii_prefix_upper_bound(const std::string &prefix);
const std::string &raw_input_with_cases_or_raw(const std::string &raw_input, const std::string &raw_input_with_cases);
} // namespace CommonUtils
