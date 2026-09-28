#pragma once

#include <cstdint>

#ifdef _WIN32
#include <Windows.h>

using ImeKeyCode = UINT;
using ImeModifierMask = UINT;
using ImeCharacter = WCHAR;
#else
using ImeKeyCode = std::uint32_t;
using ImeModifierMask = std::uint32_t;
using ImeCharacter = char16_t;
#endif

namespace ImeKey
{
inline constexpr ImeKeyCode Backspace = 0x08;
inline constexpr ImeKeyCode Tab = 0x09;
inline constexpr ImeKeyCode Return = 0x0D;
inline constexpr ImeKeyCode Shift = 0x10;
inline constexpr ImeKeyCode Escape = 0x1B;
inline constexpr ImeKeyCode Space = 0x20;
inline constexpr ImeKeyCode Semicolon = 0xBA;
inline constexpr ImeKeyCode Minus = 0xBD;
inline constexpr ImeKeyCode Apostrophe = 0xDE;
inline constexpr bool is_ascii_letter(ImeKeyCode key)
{
    return key >= 'A' && key <= 'Z';
}
} // namespace ImeKey
