#include "string_utils.h"

#include <algorithm>
#include <cctype>
#include <utf8.h>

namespace CommonUtils
{
std::wstring string_to_wstring(const std::string &str)
{
    std::u16string utf16result;
    utf8::utf8to16(str.begin(), str.end(), std::back_inserter(utf16result));
    return std::wstring(utf16result.begin(), utf16result.end());
}

std::string wstring_to_string(const std::wstring &wstr)
{
    std::string result;
    utf8::utf16to8(wstr.begin(), wstr.end(), std::back_inserter(result));
    return result;
}

bool starts_with(std::string_view text, std::string_view prefix)
{
    return text.size() >= prefix.size() && text.compare(0, prefix.size(), prefix) == 0;
}

bool is_ascii_lowercase(unsigned char character)
{
    return character >= 'a' && character <= 'z';
}

bool is_ascii_uppercase(unsigned char character)
{
    return character >= 'A' && character <= 'Z';
}

bool is_ascii_letter(unsigned char character)
{
    return is_ascii_lowercase(character) || is_ascii_uppercase(character);
}

char lowercase_ascii_char(unsigned char character)
{
    return static_cast<char>(std::tolower(character));
}

char uppercase_ascii_char(unsigned char character)
{
    return static_cast<char>(std::toupper(character));
}

std::string::size_type count_utf8_chars(const std::string &text)
{
    return utf8::distance(text.begin(), text.end());
}

std::vector<std::string> split_by_delimiter(const std::string &text, char delimiter)
{
    std::vector<std::string> parts;
    std::size_t start = 0;
    while (true)
    {
        const std::size_t position = text.find(delimiter, start);
        if (position == std::string::npos)
        {
            parts.push_back(text.substr(start));
            return parts;
        }
        parts.push_back(text.substr(start, position - start));
        start = position + 1;
    }
}

std::string remove_apostrophe_delimiters(const std::string &text)
{
    std::string result;
    result.reserve(text.size());
    for (const char character : text)
    {
        if (character != '\'')
            result.push_back(character);
    }
    return result;
}

std::string escape_sql_literal(std::string text)
{
    std::size_t position = 0;
    while ((position = text.find('\'', position)) != std::string::npos)
    {
        text.insert(position, 1, '\'');
        position += 2;
    }
    return text;
}

bool is_ascii_letters_or_apostrophe(const std::string &text)
{
    return !text.empty() && std::all_of(text.begin(), text.end(), [](unsigned char character) {
        return is_ascii_letter(character) || character == '\'';
    });
}

std::string lowercase_ascii(std::string text)
{
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char character) { return lowercase_ascii_char(character); });
    return text;
}
} // namespace CommonUtils
