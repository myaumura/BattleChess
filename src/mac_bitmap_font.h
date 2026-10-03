#ifndef BC_MAC_BITMAP_FONT_H
#define BC_MAC_BITMAP_FONT_H

#include <SDL3/SDL.h>
#include "bitmap_fonts.hpp"
#include <string_view>
#include <string>

// Native adapter (no original entrypoint): map validated UTF-8 text back to Mac Roman glyphs.
inline int mac_bitmap_character(std::string_view &text) {
    if (text.empty())
        return -1;
    unsigned value = static_cast<unsigned char>(text.front());
    text.remove_prefix(1);
    unsigned continuation = 0;
    if (value >= 0xc2 && value <= 0xdf) {
        value &= 31;
        continuation = 1;
    } else if (value >= 0xe0 && value <= 0xef) {
        value &= 15;
        continuation = 2;
    } else if (value >= 0xf0 && value <= 0xf4) {
        value &= 7;
        continuation = 3;
    } else if (value >= 128)
        return -1;
    unsigned count = continuation;
    while (continuation--) {
        if (text.empty() || (static_cast<unsigned char>(text.front()) & 192) != 128)
            return -1;
        value = (value << 6) | (static_cast<unsigned char>(text.front()) & 63);
        text.remove_prefix(1);
    }
    if ((count == 1 && value < 128) || (count == 2 && value < 2048) ||
        (count == 3 && value < 65536) || value > 0x10ffff || (value >= 0xd800 && value <= 0xdfff))
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
inline std::string mac_bitmap_decode_roman(const MacBitmapGlyph *glyphs, std::string_view text) {
    std::string result;
    for (unsigned char byte : text) {
        if (byte < 32 || byte == 127 || (glyphs && glyphs[byte].advance <= 0))
            byte = '?';
        unsigned value = byte < 128 ? byte : mac_roman_unicode[byte - 128];
        if (value < 128)
            result += char(value);
        else if (value < 2048) {
            result += char(0xc0 | (value >> 6));
            result += char(0x80 | (value & 63));
        } else {
            result += char(0xe0 | (value >> 12));
            result += char(0x80 | ((value >> 6) & 63));
            result += char(0x80 | (value & 63));
        }
    }
    return result;
}

// Native adapter (no original entrypoint): use stored advances, rejecting unavailable glyphs.
inline int mac_bitmap_width(const MacBitmapGlyph *glyphs, std::string_view text) {
    int width = 0;
    while (!text.empty()) {
        int code = mac_bitmap_character(text);
        if (code < 0 || glyphs[code].advance <= 0)
            return -1;
        width += glyphs[code].advance;
    }
    return width;
}

// Native text-edit adapter: choose the nearest UTF-8 boundary from actual glyph advances.
inline size_t mac_bitmap_caret(const MacBitmapGlyph *glyphs, std::string_view text, int x) {
    const size_t size = text.size();
    int position = 0;
    while (!text.empty()) {
        const size_t before = size - text.size();
        int code = mac_bitmap_character(text);
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
inline bool mac_bitmap_draw(SDL_Renderer *renderer, const MacBitmapGlyph *glyphs,
                            const unsigned char *pixels, float x, float baseline,
                            std::string_view text) {
    if (mac_bitmap_width(glyphs, text) < 0)
        return false;
    while (!text.empty()) {
        int code = mac_bitmap_character(text);
        const auto &g = glyphs[code];
        for (int row = 0; row < g.height; ++row)
            for (int col = 0; col < g.width; ++col)
                if (pixels[g.offset + row * g.width + col] &&
                    !SDL_RenderPoint(renderer, x + g.x + col, baseline - g.y + row))
                    return false;
        x += g.advance;
    }
    return true;
}

#endif
