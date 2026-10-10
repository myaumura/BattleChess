#include "SDLHelpers.h"
#include "MacBitmapFont.h"
#include "presentation.h"
#include <fstream>
#include <stdexcept>

/* Native error boundary; no original entry point. Stop on failed platform
 * operations instead of drawing or saving incomplete results. */
void require(bool ok, const std::string &what) {
    if (!ok)
        throw std::runtime_error(what + ": " + SDL_GetError());
}

/* Native SDL adapter; no original entry point. Own a texture and retain
 * nearest-neighbor scaling for the original pixel artwork. */
Texture createTexture(SDL_Renderer *renderer, SDL_Surface *surface) {
    Texture result(SDL_CreateTextureFromSurface(renderer, surface), SDL_DestroyTexture);
    require(bool(result), "Create texture");
    require(SDL_SetTextureScaleMode(result.get(), SDL_SCALEMODE_NEAREST), "Nearest scaling");
    return result;
}

/* Native asset loader; no original entry point. Load an extracted PNG and
 * transfer its pixels to a renderer-owned texture. */
Texture loadTexture(SDL_Renderer *renderer, const fs::path &path) {
    Surface image(SDL_LoadPNG(path.string().c_str()), SDL_DestroySurface);
    require(bool(image), "Load " + path.string());
    return createTexture(renderer, image.get());
}

/* Native asset conversion; no direct original entry point. Expand packed
 * two-bit shapes with CHANGECO coloring (file offset 0xa450), keeping transparency. */
std::vector<Texture> loadPieceTextures(SDL_Renderer *renderer, const fs::path &path,
                                       const OriginalShape *table, int count) {
    std::ifstream in(path, std::ios::binary);
    if (!in)
        throw std::runtime_error("Missing " + path.string());
    std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(in)), {});
    std::vector<Texture> textures;
    for (int side = 0; side < 2; ++side)
        for (int shapeIndex = 0; shapeIndex < count; ++shapeIndex) {
            auto shape = table[shapeIndex];
            int stride = (shape.width + 3) / 4;
            if (shape.offset + stride * shape.height > bytes.size())
                throw std::runtime_error("Truncated shape data");
            Surface image(SDL_CreateSurface(shape.width, shape.height, SDL_PIXELFORMAT_RGBA32),
                          SDL_DestroySurface);
            require(bool(image), "Shape surface");
            for (int y = 0; y < shape.height; ++y)
                for (int x = 0; x < shape.width; ++x) {
                    auto pixel = original_pixel(
                        (bytes[shape.offset + y * stride + x / 4] >> (6 - 2 * (x % 4))) & 3, side);
                    auto *rgba = static_cast<Uint8 *>(image->pixels) + y * image->pitch + 4 * x;
                    rgba[0] = rgba[1] = rgba[2] = pixel == 1 ? 0 : 255;
                    rgba[3] = pixel == 3 ? 0 : 255;
                }
            textures.push_back(createTexture(renderer, image.get()));
        }
    return textures;
}

/* Native SDL adapter; no original entry point. Set the monochrome drawing color. */
void setGrayscale(SDL_Renderer *renderer, int gray) {
    require(SDL_SetRenderDrawColor(renderer, gray, gray, gray, 255), "Color");
}

/* Native SDL adapter; no original entry point. Draw filled panels or control borders. */
void drawRectangle(SDL_Renderer *renderer, float x, float y, float width, float height, bool fill) {
    SDL_FRect bounds{x, y, width, height};
    require(fill ? SDL_RenderFillRect(renderer, &bounds) : SDL_RenderRect(renderer, &bounds),
            "Rectangle");
}

/* Native font adapter; System7 Chicago12 pixels/advances replace Toolbox text output. */
void drawSystemText(SDL_Renderer *renderer, int x, int y, const char *label) {
    require(chicago_available ? macBitmapDraw(renderer, chicago_glyphs, chicago_pixels, x,
                                              y + chicago_ascent, label)
                              : SDL_RenderDebugText(renderer, x, y, label),
            "Text");
}

/* Native text-layout adapter; measure the same advances used for glyph rendering. */
int systemTextWidth(std::string_view label) {
    int width = chicago_available ? macBitmapWidth(chicago_glyphs, label) : int(label.size() * 8);
    require(width >= 0, "Unrepresentable Macintosh text");
    return width;
}

/* Native SDL adapter; no original entry point. Place an image in logical
 * Macintosh coordinates without changing its recovered pixel data. */
void drawTexture(SDL_Renderer *renderer, SDL_Texture *image, float x, float y, float width,
                 float height) {
    SDL_FRect bounds{x, y, width, height};
    require(SDL_RenderTexture(renderer, image, nullptr, &bounds), "Image");
}
