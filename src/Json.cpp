#include "Json.h"
#include <cmath>
#include <cstdlib>

class JsonParser
{
public:
    explicit JsonParser(const std::string& text) : text(text)
    {
    }

    bool document(Json& out)
    {
        if (!value(out, 0))
            return false;
        space();
        if (at != text.size())
            return fail("Trailing characters");
        return true;
    }

    std::string error;

private:
    const std::string& text;
    size_t at = 0;
    static constexpr int kMaxDepth = 64;

    bool fail(const char* message)
    {
        if (error.empty())
            error = std::string(message) + " at offset " + std::to_string(at);
        return false;
    }

    void space()
    {
        while (at < text.size() &&
            (text[at] == ' ' || text[at] == '\t' || text[at] == '\n' || text[at] == '\r'))
            ++at;
    }

    bool literal(const char* word)
    {
        size_t length = std::char_traits<char>::length(word);
        if (text.compare(at, length, word) != 0)
            return fail("Unknown literal");
        at += length;
        return true;
    }

    static void appendUtf8(std::string& out, unsigned code)
    {
        if (code < 0x80)
        {
            out += char(code);
        }
        else if (code < 0x800)
        {
            out += char(0xc0 | (code >> 6));
            out += char(0x80 | (code & 0x3f));
        }
        else if (code < 0x10000)
        {
            out += char(0xe0 | (code >> 12));
            out += char(0x80 | ((code >> 6) & 0x3f));
            out += char(0x80 | (code & 0x3f));
        }
        else
        {
            out += char(0xf0 | (code >> 18));
            out += char(0x80 | ((code >> 12) & 0x3f));
            out += char(0x80 | ((code >> 6) & 0x3f));
            out += char(0x80 | (code & 0x3f));
        }
    }

    bool hex4(unsigned& code)
    {
        if (at + 4 > text.size())
            return fail("Short unicode escape");
        code = 0;
        for (int i = 0; i < 4; ++i)
        {
            char c = text[at++];
            code <<= 4;
            if (c >= '0' && c <= '9')
                code |= unsigned(c - '0');
            else if (c >= 'a' && c <= 'f')
                code |= unsigned(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F')
                code |= unsigned(c - 'A' + 10);
            else
                return fail("Bad unicode escape");
        }
        return true;
    }

    bool string(std::string& out)
    {
        ++at; // Opening quote.
        while (at < text.size())
        {
            char c = text[at++];
            if (c == '"')
                return true;
            if (static_cast<unsigned char>(c) < 0x20)
                return fail("Control character in string");
            if (c != '\\')
            {
                out += c;
                continue;
            }
            if (at >= text.size())
                break;
            char e = text[at++];
            switch (e)
            {
            case '"': out += '"';
                break;
            case '\\': out += '\\';
                break;
            case '/': out += '/';
                break;
            case 'b': out += '\b';
                break;
            case 'f': out += '\f';
                break;
            case 'n': out += '\n';
                break;
            case 'r': out += '\r';
                break;
            case 't': out += '\t';
                break;
            case 'u':
                {
                    unsigned code;
                    if (!hex4(code))
                        return false;
                    if (code >= 0xd800 && code < 0xdc00 && text.compare(at, 2, "\\u") == 0)
                    {
                        at += 2;
                        unsigned low;
                        if (!hex4(low))
                            return false;
                        code = 0x10000 + ((code - 0xd800) << 10) + (low - 0xdc00);
                    }
                    appendUtf8(out, code);
                    break;
                }
            default:
                return fail("Bad escape");
            }
        }
        return fail("Unterminated string");
    }

    bool value(Json& out, int depth)
    {
        if (depth > kMaxDepth)
            return fail("Nesting too deep");
        space();
        if (at >= text.size())
            return fail("Unexpected end");
        char c = text[at];
        if (c == '{')
        {
            out.kind = Json::Type::object;
            ++at;
            space();
            if (at < text.size() && text[at] == '}')
            {
                ++at;
                return true;
            }
            for (;;)
            {
                space();
                if (at >= text.size() || text[at] != '"')
                    return fail("Expected key");
                std::string key;
                if (!string(key))
                    return false;
                space();
                if (at >= text.size() || text[at] != ':')
                    return fail("Expected ':'");
                ++at;
                Json member;
                if (!value(member, depth + 1))
                    return false;
                out.members[key] = std::move(member);
                space();
                if (at < text.size() && text[at] == ',')
                {
                    ++at;
                    continue;
                }
                if (at < text.size() && text[at] == '}')
                {
                    ++at;
                    return true;
                }
                return fail("Expected ',' or '}'");
            }
        }
        if (c == '[')
        {
            out.kind = Json::Type::array;
            ++at;
            space();
            if (at < text.size() && text[at] == ']')
            {
                ++at;
                return true;
            }
            for (;;)
            {
                Json item;
                if (!value(item, depth + 1))
                    return false;
                out.array.push_back(std::move(item));
                space();
                if (at < text.size() && text[at] == ',')
                {
                    ++at;
                    continue;
                }
                if (at < text.size() && text[at] == ']')
                {
                    ++at;
                    return true;
                }
                return fail("Expected ',' or ']'");
            }
        }
        if (c == '"')
        {
            out.kind = Json::Type::string;
            return string(out.text);
        }
        if (c == 't')
        {
            out.kind = Json::Type::boolean;
            out.flag = true;
            return literal("true");
        }
        if (c == 'f')
        {
            out.kind = Json::Type::boolean;
            return literal("false");
        }
        if (c == 'n')
        {
            out.kind = Json::Type::null;
            return literal("null");
        }
        if (c == '-' || (c >= '0' && c <= '9'))
        {
            const char* begin = text.c_str() + at;
            char* end = nullptr;
            double number = std::strtod(begin, &end);
            if (end == begin || !std::isfinite(number))
                return fail("Bad number");
            at += size_t(end - begin);
            out.kind = Json::Type::number;
            out.number = number;
            return true;
        }
        return fail("Unexpected character");
    }
};

std::optional<Json> Json::parse(const std::string& text, std::string* error)
{
    Json out;
    JsonParser parser(text);
    if (!parser.document(out))
    {
        if (error)
            *error = parser.error;
        return std::nullopt;
    }
    return out;
}

bool Json::has(const std::string& key) const
{
    return kind == Type::object && members.count(key);
}

const Json& Json::operator[](const std::string& key) const
{
    static const Json null;
    if (kind != Type::object)
        return null;
    auto found = members.find(key);
    return found == members.end() ? null : found->second;
}

std::string Json::str(const std::string& fallback) const
{
    return kind == Type::string ? text : fallback;
}

double Json::num(double fallback) const
{
    return kind == Type::number ? number : fallback;
}

long long Json::integer(long long fallback) const
{
    return kind == Type::number ? (long long)std::llround(number) : fallback;
}

bool Json::boolean(bool fallback) const
{
    return kind == Type::boolean ? flag : fallback;
}

Json Json::makeString(std::string value)
{
    Json out;
    out.kind = Type::string;
    out.text = std::move(value);
    return out;
}
