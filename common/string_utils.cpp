#include "string_utils.h"

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
} // namespace CommonUtils
