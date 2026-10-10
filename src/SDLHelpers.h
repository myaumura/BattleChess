#ifndef BC_SDL_HELPERS_H
#define BC_SDL_HELPERS_H

#include <SDL3/SDL.h>
#include "original.hpp"
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace fs = std::filesystem;
template <class T, auto destroy> using Handle = std::unique_ptr<T, decltype(destroy)>;
using Texture = Handle<SDL_Texture, SDL_DestroyTexture>;
using Surface = Handle<SDL_Surface, SDL_DestroySurface>;
using Cursor = Handle<SDL_Cursor, SDL_DestroyCursor>;

void require(bool ok, const std::string &what);
Texture createTexture(SDL_Renderer *, SDL_Surface *);
Texture loadTexture(SDL_Renderer *, const fs::path &);
Cursor loadCursor(const fs::path &);
std::vector<Texture> loadPieceTextures(SDL_Renderer *, const fs::path &, const OriginalShape *,
                                       int count);
void setGrayscale(SDL_Renderer *, int gray);
void drawRectangle(SDL_Renderer *, float x, float y, float width, float height, bool fill = true);
void drawSystemText(SDL_Renderer *, int x, int y, const char *label);
int systemTextWidth(std::string_view label);
void drawTexture(SDL_Renderer *, SDL_Texture *, float x, float y, float width, float height);

#endif
