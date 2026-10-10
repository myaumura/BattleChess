#ifndef BC_MAC_BITMAP_FONT_H
#define BC_MAC_BITMAP_FONT_H

#include <SDL3/SDL.h>
#include "bitmap_fonts.hpp"
#include <string>
#include <string_view>

// Consume one UTF-8 character and return its Mac Roman code, or -1 if invalid or unmapped.
// On failure, bytes already consumed remain removed from text.
int macBitmapCharacter(std::string_view &text);

// Replace controls and unavailable glyphs with '?' while converting Mac Roman to UTF-8.
// A null glyph table skips availability checks.
std::string macBitmapDecodeRoman(const MacBitmapGlyph *glyphs, std::string_view text);

// Return the sum of glyph advances, or -1 for invalid text or unavailable glyphs.
int macBitmapWidth(const MacBitmapGlyph *glyphs, std::string_view text);

// Return the nearest UTF-8 byte boundary, stopping before invalid or unavailable glyphs.
size_t macBitmapCaret(const MacBitmapGlyph *glyphs, std::string_view text, int x);

// Validate all text before painting; return false for invalid text or an SDL draw failure.
bool macBitmapDraw(SDL_Renderer *renderer, const MacBitmapGlyph *glyphs,
                   const unsigned char *pixels, float x, float baseline, std::string_view text);

#endif
