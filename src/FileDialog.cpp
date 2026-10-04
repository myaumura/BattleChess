#include "FileDialog.h"
#include "SDLHelpers.h"
#include <cstdint>

static Uint32 fileEvent;

/* Native SDL callback; no original entry point. Copy the picker result onto the
 * main event queue so a platform callback never mutates game state. */
static void SDLCALL fileSelected(void *operation, const char *const *files, int) {
    auto *result = new FileResult{int(reinterpret_cast<intptr_t>(operation)), {}, {}};
    if (!files)
        result->error = SDL_GetError();
    else if (files[0])
        result->path = files[0];
    SDL_Event event{};
    event.type = fileEvent;
    event.user.data1 = result;
    if (!SDL_PushEvent(&event))
        delete result;
}

Uint32 registerFileDialogEvent() {
    fileEvent = SDL_RegisterEvents(1);
    require(fileEvent != Uint32(-1), "File dialog event");
    return fileEvent;
}

/* Native OS picker; no original entry point. Replace Standard File selection. */
void showFileDialog(SDL_Window *window, bool write, const std::string &currentPath) {
    if (write)
        SDL_ShowSaveFileDialog(fileSelected, reinterpret_cast<void *>(intptr_t(2)), window, nullptr,
                               0, currentPath.empty() ? nullptr : currentPath.c_str());
    else
        SDL_ShowOpenFileDialog(fileSelected, reinterpret_cast<void *>(intptr_t(1)), window, nullptr,
                               0, nullptr, false);
}
