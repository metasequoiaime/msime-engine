#pragma once

// Minimal, self-contained JSON parser for the safetensors header.
//
// The core engine target does not depend on a JSON library (nlohmann is only pulled in by the
// optional `voice` feature), and the safetensors header is small and regular. Rather than add a
// build-wide dependency, this parses the handful of shapes the loader needs: the flat object of
// tensor descriptors, plus the `__metadata__` block whose `config` and `vocab` values are
// themselves JSON strings parsed a second time.
//
// It is a straightforward recursive-descent parser over UTF-8 text. serde_json (which writes the
// files) emits raw UTF-8 for non-ASCII, but `\uXXXX` escapes are handled anyway so a re-encoded
// file still loads.

#include <cstdint>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace neural::json
{

struct Value
{
    enum class Type
    {
        Null,
        Bool,
        Number,
        String,
        Array,
        Object,
    };

    Type type = Type::Null;
    bool boolean = false;
    double number = 0.0;
    std::string string;
    std::vector<Value> array;
    std::vector<std::pair<std::string, Value>> object;

    bool is_object() const
    {
        return type == Type::Object;
    }
    bool is_array() const
    {
        return type == Type::Array;
    }
    bool is_string() const
    {
        return type == Type::String;
    }
    bool is_number() const
    {
        return type == Type::Number;
    }

    // Linear lookup: objects here have at most a few dozen keys, and the tensor table is read once.
    const Value *find(const std::string &key) const
    {
        for (const auto &entry : object)
        {
            if (entry.first == key)
            {
                return &entry.second;
            }
        }
        return nullptr;
    }
};

class ParseError : public std::runtime_error
{
  public:
    explicit ParseError(const std::string &message) : std::runtime_error(message)
    {
    }
};

namespace detail
{

class Parser
{
  public:
    explicit Parser(const std::string &text) : text_(text)
    {
    }

    Value parse()
    {
        skip_whitespace();
        Value value = parse_value();
        skip_whitespace();
        if (position_ != text_.size())
        {
            fail("trailing characters after JSON value");
        }
        return value;
    }

  private:
    const std::string &text_;
    size_t position_ = 0;

    [[noreturn]] void fail(const std::string &message) const
    {
        throw ParseError("json: " + message + " at offset " + std::to_string(position_));
    }

    char peek() const
    {
        if (position_ >= text_.size())
        {
            fail("unexpected end of input");
        }
        return text_[position_];
    }

    char next()
    {
        char c = peek();
        ++position_;
        return c;
    }

    void expect(char c)
    {
        if (next() != c)
        {
            fail(std::string("expected '") + c + "'");
        }
    }

    void skip_whitespace()
    {
        while (position_ < text_.size())
        {
            char c = text_[position_];
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r')
            {
                ++position_;
            }
            else
            {
                break;
            }
        }
    }

    Value parse_value()
    {
        skip_whitespace();
        char c = peek();
        switch (c)
        {
        case '{':
            return parse_object();
        case '[':
            return parse_array();
        case '"': {
            Value value;
            value.type = Value::Type::String;
            value.string = parse_string();
            return value;
        }
        case 't':
        case 'f':
            return parse_bool();
        case 'n':
            return parse_null();
        default:
            return parse_number();
        }
    }

    Value parse_object()
    {
        Value value;
        value.type = Value::Type::Object;
        expect('{');
        skip_whitespace();
        if (peek() == '}')
        {
            ++position_;
            return value;
        }
        while (true)
        {
            skip_whitespace();
            std::string key = parse_string();
            skip_whitespace();
            expect(':');
            Value child = parse_value();
            value.object.emplace_back(std::move(key), std::move(child));
            skip_whitespace();
            char c = next();
            if (c == ',')
            {
                continue;
            }
            if (c == '}')
            {
                break;
            }
            fail("expected ',' or '}' in object");
        }
        return value;
    }

    Value parse_array()
    {
        Value value;
        value.type = Value::Type::Array;
        expect('[');
        skip_whitespace();
        if (peek() == ']')
        {
            ++position_;
            return value;
        }
        while (true)
        {
            value.array.push_back(parse_value());
            skip_whitespace();
            char c = next();
            if (c == ',')
            {
                continue;
            }
            if (c == ']')
            {
                break;
            }
            fail("expected ',' or ']' in array");
        }
        return value;
    }

    Value parse_bool()
    {
        Value value;
        value.type = Value::Type::Bool;
        if (text_.compare(position_, 4, "true") == 0)
        {
            position_ += 4;
            value.boolean = true;
        }
        else if (text_.compare(position_, 5, "false") == 0)
        {
            position_ += 5;
            value.boolean = false;
        }
        else
        {
            fail("invalid literal");
        }
        return value;
    }

    Value parse_null()
    {
        Value value;
        value.type = Value::Type::Null;
        if (text_.compare(position_, 4, "null") == 0)
        {
            position_ += 4;
        }
        else
        {
            fail("invalid literal");
        }
        return value;
    }

    Value parse_number()
    {
        size_t start = position_;
        if (position_ < text_.size() && (text_[position_] == '-' || text_[position_] == '+'))
        {
            ++position_;
        }
        while (position_ < text_.size())
        {
            char c = text_[position_];
            if ((c >= '0' && c <= '9') || c == '.' || c == 'e' || c == 'E' || c == '+' || c == '-')
            {
                ++position_;
            }
            else
            {
                break;
            }
        }
        if (position_ == start)
        {
            fail("invalid number");
        }
        Value value;
        value.type = Value::Type::Number;
        try
        {
            value.number = std::stod(text_.substr(start, position_ - start));
        }
        catch (const std::exception &)
        {
            fail("invalid number");
        }
        return value;
    }

    std::string parse_string()
    {
        expect('"');
        std::string out;
        while (true)
        {
            if (position_ >= text_.size())
            {
                fail("unterminated string");
            }
            char c = text_[position_++];
            if (c == '"')
            {
                break;
            }
            if (c == '\\')
            {
                if (position_ >= text_.size())
                {
                    fail("unterminated escape");
                }
                char escape = text_[position_++];
                switch (escape)
                {
                case '"':
                    out.push_back('"');
                    break;
                case '\\':
                    out.push_back('\\');
                    break;
                case '/':
                    out.push_back('/');
                    break;
                case 'b':
                    out.push_back('\b');
                    break;
                case 'f':
                    out.push_back('\f');
                    break;
                case 'n':
                    out.push_back('\n');
                    break;
                case 'r':
                    out.push_back('\r');
                    break;
                case 't':
                    out.push_back('\t');
                    break;
                case 'u':
                    append_unicode(out);
                    break;
                default:
                    fail("invalid escape");
                }
            }
            else
            {
                out.push_back(c);
            }
        }
        return out;
    }

    unsigned read_hex4()
    {
        if (position_ + 4 > text_.size())
        {
            fail("truncated \\u escape");
        }
        unsigned code = 0;
        for (int i = 0; i < 4; ++i)
        {
            char c = text_[position_++];
            code <<= 4;
            if (c >= '0' && c <= '9')
            {
                code |= static_cast<unsigned>(c - '0');
            }
            else if (c >= 'a' && c <= 'f')
            {
                code |= static_cast<unsigned>(c - 'a' + 10);
            }
            else if (c >= 'A' && c <= 'F')
            {
                code |= static_cast<unsigned>(c - 'A' + 10);
            }
            else
            {
                fail("invalid hex digit in \\u escape");
            }
        }
        return code;
    }

    void append_unicode(std::string &out)
    {
        unsigned code = read_hex4();
        if (code >= 0xD800 && code <= 0xDBFF)
        {
            // High surrogate: a low surrogate must follow to form a code point above the BMP.
            if (position_ + 2 > text_.size() || text_[position_] != '\\' || text_[position_ + 1] != 'u')
            {
                fail("lone high surrogate in \\u escape");
            }
            position_ += 2;
            unsigned low = read_hex4();
            if (low < 0xDC00 || low > 0xDFFF)
            {
                fail("invalid low surrogate in \\u escape");
            }
            code = 0x10000 + ((code - 0xD800) << 10) + (low - 0xDC00);
        }
        append_utf8(out, code);
    }

    static void append_utf8(std::string &out, unsigned code)
    {
        if (code <= 0x7F)
        {
            out.push_back(static_cast<char>(code));
        }
        else if (code <= 0x7FF)
        {
            out.push_back(static_cast<char>(0xC0 | (code >> 6)));
            out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
        }
        else if (code <= 0xFFFF)
        {
            out.push_back(static_cast<char>(0xE0 | (code >> 12)));
            out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
        }
        else
        {
            out.push_back(static_cast<char>(0xF0 | (code >> 18)));
            out.push_back(static_cast<char>(0x80 | ((code >> 12) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
        }
    }
};

} // namespace detail

// Parses `text` as JSON. Throws ParseError on malformed input.
inline Value parse(const std::string &text)
{
    return detail::Parser(text).parse();
}

} // namespace neural::json
