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
    for (char character : input)
        if (resourceId == 403
                ? ((character < '0' || character > '9') && character != '-' && character != '+')
                : (character < 32 || character >= 127))
            return;
    const size_t first = std::min(cursor, anchor);
    const size_t count = std::max(cursor, anchor) - first;
    const size_t limit = resourceId == 403 ? 10 : resourceId == 404 ? 38 : 40;
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
