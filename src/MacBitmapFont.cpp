#include "MacBitmapFont.h"

// Native adapter (no original entrypoint): map validated UTF-8 text back to Mac Roman glyphs.
int macBitmapCharacter(std::string_view &text) {
    if (text.empty())
        return -1;
    unsigned value = static_cast<unsigned char>(text.front());
    text.remove_prefix(1);
    unsigned continuationCount = 0;
    if (value >= 0xc2 && value <= 0xdf) {
        value &= 0x1f;
        continuationCount = 1;
    } else if (value >= 0xe0 && value <= 0xef) {
        value &= 0x0f;
        continuationCount = 2;
    } else if (value >= 0xf0 && value <= 0xf4) {
        value &= 0x07;
        continuationCount = 3;
    } else if (value >= 128)
        return -1;
    for (unsigned byteIndex = 0; byteIndex < continuationCount; ++byteIndex) {
        if (text.empty() || (static_cast<unsigned char>(text.front()) & 0xc0) != 0x80)
            return -1;
        value = (value << 6) | (static_cast<unsigned char>(text.front()) & 0x3f);
        text.remove_prefix(1);
    }
    if ((continuationCount == 1 && value < 128) || (continuationCount == 2 && value < 2048) ||
        (continuationCount == 3 && value < 65536) || value > 0x10ffff ||
        (value >= 0xd800 && value <= 0xdfff))
        return -1;
    if (value < 128)
        return int(value);
    for (int i = 0; i < 128; ++i)
        if (mac_roman_unicode[i] == value)
            return i + 128;
    return -1;
}

// Native READBLOC text boundary (0x1117a): convert original Mac Roman serial bytes
// to UTF-8; unavailable control/glyph cells use the strike's question mark.
std::string macBitmapDecodeRoman(const MacBitmapGlyph *glyphs, std::string_view text) {
    std::string result;
    for (unsigned char byte : text) {
        if (byte < 32 || byte == 127 || (glyphs && glyphs[byte].advance <= 0))
            byte = '?';
        unsigned value = byte < 128 ? byte : mac_roman_unicode[byte - 128];
        if (value < 128)
            result += char(value);
        else if (value < 2048) {
            result += char(0xc0 | (value >> 6));
            result += char(0x80 | (value & 0x3f));
        } else {
            result += char(0xe0 | (value >> 12));
            result += char(0x80 | ((value >> 6) & 0x3f));
            result += char(0x80 | (value & 0x3f));
        }
    }
    return result;
}

// Native adapter (no original entrypoint): use stored advances, rejecting unavailable glyphs.
int macBitmapWidth(const MacBitmapGlyph *glyphs, std::string_view text) {
    int width = 0;
    while (!text.empty()) {
        int code = macBitmapCharacter(text);
        if (code < 0 || glyphs[code].advance <= 0)
            return -1;
        width += glyphs[code].advance;
    }
    return width;
}

// Native text-edit adapter: choose the nearest UTF-8 boundary from actual glyph advances.
size_t macBitmapCaret(const MacBitmapGlyph *glyphs, std::string_view text, int x) {
    const size_t size = text.size();
    int position = 0;
    while (!text.empty()) {
        const size_t before = size - text.size();
        int code = macBitmapCharacter(text);
        if (code < 0 || glyphs[code].advance <= 0)
            return before;
        const int advance = glyphs[code].advance;
        if (x < position + (advance + 1) / 2)
            return before;
        position += advance;
    }
    return size;
}

// Native adapter (no original entrypoint): paint source bitmap pixels at their baseline bearings.
bool macBitmapDraw(SDL_Renderer *renderer, const MacBitmapGlyph *glyphs,
                   const unsigned char *pixels, float x, float baseline, std::string_view text) {
    if (macBitmapWidth(glyphs, text) < 0)
        return false;
    while (!text.empty()) {
        int code = macBitmapCharacter(text);
        const auto &glyph = glyphs[code];
        for (int row = 0; row < glyph.height; ++row)
            for (int column = 0; column < glyph.width; ++column)
                if (pixels[glyph.offset + row * glyph.width + column] &&
                    !SDL_RenderPoint(renderer, x + glyph.x + column, baseline - glyph.y + row))
                    return false;
        x += glyph.advance;
    }
    return true;
}
