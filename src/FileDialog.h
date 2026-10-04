#ifndef BC_FILE_DIALOG_H
#define BC_FILE_DIALOG_H

#include <SDL3/SDL.h>
#include <string>

struct FileResult {
    int operation;
    std::string path;
    std::string error;
};

Uint32 registerFileDialogEvent();
void showFileDialog(SDL_Window *, bool write, const std::string &currentPath);

#endif
