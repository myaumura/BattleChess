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

/* Native typed pickers; no original entry point. SDL keeps the filter pointer until the
 * dialog completes, so every filter list is static. */
static void showTypedDialog(SDL_Window *window, FileOperation operation, bool write,
                            const SDL_DialogFileFilter *filter, const std::string &suggestedPath) {
    void *userdata = reinterpret_cast<void *>(intptr_t(operation));
    const char *location = suggestedPath.empty() ? nullptr : suggestedPath.c_str();
    if (write)
        SDL_ShowSaveFileDialog(fileSelected, userdata, window, filter, 1, location);
    else
        SDL_ShowOpenFileDialog(fileSelected, userdata, window, filter, 1, location, false);
}

void showPgnExportDialog(SDL_Window *window, const std::string &suggestedPath) {
    static const SDL_DialogFileFilter pgn{"PGN chess game", "pgn"};
    showTypedDialog(window, FileOperation::exportPgn, true, &pgn, suggestedPath);
}

void showPgnImportDialog(SDL_Window *window) {
    static const SDL_DialogFileFilter pgn{"PGN chess game", "pgn"};
    showTypedDialog(window, FileOperation::importPgn, false, &pgn, {});
}

void showGifExportDialog(SDL_Window *window, const std::string &suggestedPath) {
    static const SDL_DialogFileFilter gif{"Animated GIF", "gif"};
    showTypedDialog(window, FileOperation::exportGif, true, &gif, suggestedPath);
}
