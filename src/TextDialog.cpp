#include "TextDialog.h"
#include <algorithm>

/* Native selection adapter; no original entry point. Opening SETTIME
 * selects the existing value so the next input replaces it. */
void TextDialog::selectAll() {
    cursor = text.size();
    anchor = 0;
}

/* Native input guard; no original entry point. Validate the whole input
 * before replacing the selection, preserving each resource's ASCII limit. */
void TextDialog::replace(const std::string &input) {
    // Native Lichess prompts (900 token, 901 username) accept only what Lichess allows.
    auto word = [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
               c == '_';
    };
    for (char character : input)
        if (resourceId == 900   ? !word(character)
            : resourceId == 901 ? !word(character) && character != '-'
            : resourceId == 403
                ? ((character < '0' || character > '9') && character != '-' && character != '+')
                : (character < 32 || character >= 127))
            return;
    const size_t first = std::min(cursor, anchor);
    const size_t count = std::max(cursor, anchor) - first;
    // Lichess tokens may grow; the API asks clients to allow at least 512 characters.
    const size_t limit = resourceId == 900   ? 1024
                         : resourceId == 901 ? 30
                         : resourceId == 403 ? 10
                         : resourceId == 404 ? 38
                                             : 40;
    if (text.size() - count + input.size() > limit)
        return;
    text.replace(first, count, input);
    cursor = anchor = first + input.size();
}

/* Native keyboard adapter; no original entry point. Cursor/selection
 * edits stay local; Enter completion remains with the dialog's caller. */
void TextDialog::key(SDL_Keycode key, SDL_Keymod modifiers) {
    if (key == SDLK_A && (modifiers & (SDL_KMOD_CTRL | SDL_KMOD_GUI))) {
        selectAll();
    } else if (key == SDLK_V && (modifiers & (SDL_KMOD_CTRL | SDL_KMOD_GUI)) && resourceId >= 900) {
        // Native paste for Lichess prompts only; surrounding whitespace is dropped.
        if (char *clipboard = SDL_GetClipboardText()) {
            std::string text = clipboard;
            SDL_free(clipboard);
            text.erase(0, text.find_first_not_of(" \t\r\n"));
            text.erase(text.find_last_not_of(" \t\r\n") + 1);
            replace(text);
        }
    } else if (key == SDLK_BACKSPACE || key == SDLK_DELETE) {
        if (anchor == cursor) {
            if (key == SDLK_BACKSPACE && anchor)
                --anchor;
            if (key == SDLK_DELETE && anchor < text.size())
                ++anchor;
        }
        replace("");
    } else if (key == SDLK_LEFT || key == SDLK_RIGHT || key == SDLK_HOME || key == SDLK_END) {
        if (key == SDLK_LEFT && cursor)
            --cursor;
        if (key == SDLK_RIGHT && cursor < text.size())
            ++cursor;
        if (key == SDLK_HOME)
            cursor = 0;
        if (key == SDLK_END)
            cursor = text.size();
        if (!(modifiers & SDL_KMOD_SHIFT))
            anchor = cursor;
    }
}
