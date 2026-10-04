#ifndef BC_TEXT_DIALOG_H
#define BC_TEXT_DIALOG_H

#include <SDL3/SDL.h>
#include <cstddef>
#include <string>

/*
 * Native editable-field state for SETTIME 0x381e, DIALNUMB 0x39ca and SENDMESS 0x3bce.
 * SDL supplies text events; resource IDs retain the original numeric, dial-number and chat input
 * limits. No Toolbox pointer is retained.
 */
struct TextDialog {
    int resourceId = 403;
    bool open = false;
    std::string text;
    size_t cursor = 0;
    size_t anchor = 0;

    void selectAll();

    void replace(const std::string &input);

    void key(SDL_Keycode key, SDL_Keymod modifiers);
};

#endif
