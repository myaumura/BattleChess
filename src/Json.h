#ifndef BC_JSON_H
#define BC_JSON_H

#include <map>
#include <optional>
#include <string>
#include <vector>

/* Native minimal JSON reader for Lichess NDJSON lines; no original entry point.
 * Numbers are doubles (Lichess clocks in milliseconds fit exactly). Missing keys and
 * type mismatches read as defaults, so callers check only what they rely on. */
class Json
{
public:
    enum class Type { null, boolean, number, string, array, object };

    static std::optional<Json> parse(const std::string& text, std::string* error = nullptr);

    Type type() const
    {
        return kind;
    }

    bool isObject() const
    {
        return kind == Type::object;
    }

    bool has(const std::string& key) const;
    // Missing members and non-objects give a shared null value.
    const Json& operator[](const std::string& key) const;

    const std::vector<Json>& items() const
    {
        return array;
    }

    std::string str(const std::string& fallback = "") const;
    double num(double fallback = 0) const;
    long long integer(long long fallback = 0) const;
    bool boolean(bool fallback = false) const;

    // Form helpers for tests.
    static Json makeString(std::string value);

private:
    Type kind = Type::null;
    bool flag = false;
    double number = 0;
    std::string text;
    std::vector<Json> array;
    std::map<std::string, Json> members;

    friend class JsonParser;
};

#endif
