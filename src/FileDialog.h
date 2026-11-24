#ifndef BC_FILE_DIALOG_H
#define BC_FILE_DIALOG_H

#include <SDL3/SDL.h>
#include <string>

/* FileResult::operation values; open and save keep their original numbering. */
enum class FileOperation : int {
    open = 1,
    save = 2,
    exportPgn = 3,
    importPgn = 4,
    exportGif = 5,
};

struct FileResult {
    int operation;
    std::string path;
    std::string error;
};

Uint32 registerFileDialogEvent();
void showFileDialog(SDL_Window *, bool write, const std::string &currentPath);
void showPgnExportDialog(SDL_Window *, const std::string &suggestedPath);
void showPgnImportDialog(SDL_Window *);
void showGifExportDialog(SDL_Window *, const std::string &suggestedPath);

#endif
