#include "ApplicationInternal.h"
#include "mac_bitmap_font.h"
#include "hint_outline.h"
#include "presentation.h"
#include <cstdlib>
#include <stdexcept>

/* Original: DRAWSQUA, file offset 0xb62e. Outline the selected board square
 * using the original perspective/flat polygon and doubled edge path. */
static void drawSelection(SDL_Renderer *renderer, int square, bool flat, unsigned pen = 0) {
    int displaySquare = engine_to_display(square), row = displaySquare / 8,
        column = displaySquare % 8;
    const int *xCoordinates = flat ? flat_hit_x : perspective_hit_x;
    const int *yCoordinates = flat ? flat_hit_y : perspective_hit_y;
    int topLeftX = xCoordinates[row * 9 + column] - 1,
        topRightX = xCoordinates[row * 9 + column + 1],
        bottomLeftX = xCoordinates[(row + 1) * 9 + column] - 1,
        bottomRightX = xCoordinates[(row + 1) * 9 + column + 1];
    int top = 20 + yCoordinates[row], bottom = 21 + yCoordinates[row + 1];
    /* Original: DRAW, file offset 0xbf0e. Keep the original >= tie decisions
     * so diagonal selection borders rasterize on the same pixels. */
    auto line = [&](int x, int y, int endx, int endy) {
        int dx = std::abs(endx - x), dy = std::abs(endy - y), sx = x < endx ? 1 : -1,
            sy = y < endy ? 1 : -1, err = dx - dy;
        for (;;) {
            if (pen)
                setGrayscale(renderer, original_outline_gray(pen, x));
            require(SDL_RenderPoint(renderer, x, y), "Selection");
            if (x == endx && y == endy)
                break;
            int twice = 2 * err;
            if (twice >= -dy) {
                err -= dy;
                x += sx;
            }
            if (twice <= dx) {
                err += dx;
                y += sy;
            }
        }
    };
    setGrayscale(renderer, 255);
    line(topLeftX, top, topRightX, top);
    line(topRightX, top, bottomRightX, bottom);
    line(bottomRightX, bottom, bottomLeftX, bottom);
    line(bottomLeftX, bottom, topLeftX, top);
    line(topLeftX + 1, top + 1, topRightX - 1, top + 1);
    line(topRightX - 1, top + 1, topRightX - 1, top);
    line(topRightX - 1, top, bottomRightX - 1, bottom);
    line(bottomRightX - 1, bottom, bottomRightX - 1, bottom - 1);
    line(bottomRightX - 1, bottom - 1, bottomLeftX + 1, bottom - 1);
    line(bottomLeftX + 1, bottom - 1, bottomLeftX + 1, bottom);
    line(bottomLeftX + 1, bottom, topLeftX + 1, top);
}

void Application::renderBoard() {
    setGrayscale(rendererPtr, 255);
    require(SDL_RenderClear(rendererPtr), "Clear");
    SDL_FRect src{0, 0, 512, 330}, dst{0, 20, 512, 330};
    require(SDL_RenderTexture(rendererPtr,
                              flat ? textures.flatBoard.get() : textures.perspective.get(), &src,
                              &dst),
            "Board");
    if (session.selected >= 0)
        drawSelection(rendererPtr, session.selected, flat);
    /* Original PRINTHIN (0x66d0): source-first, twenty two-tick phases;
     * destination retains the previous pen until its next scheduled draw. */
    if (hint) {
        Uint64 ticks = (SDL_GetTicks() - hintStarted) * 60 / 1000;
        auto pens = original_hint_pens(uint32_t(ticks));
        if (!pens.from)
            hint.reset();
        else {
            drawSelection(rendererPtr, hint->from, flat, pens.from);
            if (pens.to)
                drawSelection(rendererPtr, hint->to, flat, pens.to);
        }
    }
    bool originalScene = animation.busy() || session.promotionWalk || session.mateAnimationDone ||
                         animation.displays(session.game.position, flat);
    if (originalScene)
        animation.draw(false);
    const Position &shown = editing ? setup.position : session.game.position;
    uint8_t display[64];
    setup_display_board(&shown, display);
    // PLACE / INSERTFO: back ranks first, equal-y sprites newest first.
    if (!originalScene)
        for (int row = 0; row < 8; ++row)
            for (int col = 7; col >= 0; --col) {
                int i = row * 8 + col;
                if (!display[i])
                    continue;
                int shape = (flat ? flat_shapes : perspective_shapes)[display[i] & 63] - 17;
                int count =
                    flat ? int(std::size(flat_piece_shapes)) : int(std::size(standing_shapes));
                if (shape < 0 || shape >= count)
                    throw std::runtime_error("Invalid original shape selector");
                auto s = (flat ? flat_piece_shapes : standing_shapes)[shape];
                int side = shown.board[display_to_engine(i)].side;
                auto &t = (flat ? textures.flatPieces : textures.standing)[side * count + shape];
                drawTexture(rendererPtr, t.get(), (flat ? flat_x : perspective_x)[i] - s.hx,
                            20 + (flat ? flat_y : perspective_y)[row] - s.hy, s.width, s.height);
            }
    if (editing)
        for (int side = 0; side < 2; ++side)
            for (int piece = 1; piece <= 6; ++piece) {
                Position sample{};
                insert_piece(&sample, piece, side, 0);
                uint8_t codes[64];
                setup_display_board(&sample, codes);
                int shape = flat_shapes[codes[56] & 63] - 17;
                auto g = flat_piece_shapes[shape];
                drawTexture(rendererPtr, textures.flatPieces[side * 6 + shape].get(),
                            (side == 1 ? 30 : 474) - g.hx, 20 + 30 + (piece - 1) * 40 - g.hy,
                            g.width, g.height);
            }
    if (editing && setup.heldPiece) {
        Position sample{};
        insert_piece(&sample, setup.heldPiece, setup.heldSide, 0);
        uint8_t codes[64];
        setup_display_board(&sample, codes);
        int shape = flat_shapes[codes[56] & 63] - 17;
        auto g = flat_piece_shapes[shape];
        drawTexture(rendererPtr, textures.flatPieces[setup.heldSide * 6 + shape].get(),
                    mouseX - g.hx, mouseY - g.hy, g.width, g.height);
    }
}

void Application::renderMenus() {
    setGrayscale(rendererPtr, 255);
    drawRectangle(rendererPtr, 0, 0, logicalWidth, 20);
    setGrayscale(rendererPtr, 0);
    drawRectangle(rendererPtr, 0, 19, logicalWidth, 1);
    // Native MenuSelect highlight adapter (original call 0x103f6).
    for (int i = 0; i < 7; ++i)
        if (editing ? i == 5 : i != 5 && (i != 6 || modem.connected())) {
            if (i == menu) {
                setGrayscale(rendererPtr, 0);
                drawRectangle(rendererPtr, menuX[i], 0, menuWidth[i], 19);
            }
            setGrayscale(rendererPtr, i == menu ? 255 : 0);
            drawSystemText(rendererPtr, menuX[i], chicago_available ? 1 : 6, menuResource(i).title);
        }
    if (menu >= 0) {
        auto &m = menuResource(menu);
        setGrayscale(rendererPtr, 255);
        drawRectangle(rendererPtr, menuX[menu], 20, 250, m.count * 18 + 4);
        setGrayscale(rendererPtr, 0);
        drawRectangle(rendererPtr, menuX[menu], 20, 250, m.count * 18 + 4, false);
        for (int i = 0; i < m.count; ++i) {
            bool selected = i == menuItem && menuItemEnabled(menu, i);
            if (selected) {
                setGrayscale(rendererPtr, 0);
                drawRectangle(rendererPtr, menuX[menu] + 1, 22 + i * 18, 248, 18);
            }
            setGrayscale(rendererPtr, selected ? 255 : menuItemEnabled(menu, i) ? 0 : 150);
            const char *label = m.items[i].label;
            if (std::string(label) == "-") {
                drawRectangle(rendererPtr, menuX[menu] + 3, 34 + i * 18, 244, 1);
                continue;
            }
            if (menu == 2 && i == 0)
                label = animation.sound_enabled ? "Turn Sound Off" : "Turn Sound On";
            if (menu == 2 && i == 1)
                label = flat ? "Switch to 3D Board" : "Switch to 2D Board";
            if ((menu == 2 && (i == 2 + settings.white_player || i == 5 + settings.black_player)) ||
                (menu == 3 && i == settings.level) || (menu == 6 && i == 2 && autoAnswer))
                drawSystemText(rendererPtr, menuX[menu] + 4, (chicago_available ? 22 : 26) + i * 18,
                               "+");
            drawSystemText(rendererPtr, menuX[menu] + 16, (chicago_available ? 22 : 26) + i * 18,
                           label);
            if (m.items[i].shortcut) {
                char key[] = {char(m.items[i].shortcut), 0};
                drawSystemText(rendererPtr, menuX[menu] + 234,
                               (chicago_available ? 22 : 26) + i * 18, key);
            }
        }
    }
}

/* Native resource adapter; no original entry point. Resolve PICT 400..403 for
 * the PAWNSELE dialog (original file offset 0x3d1c). */
static SDL_Texture *promotionPicture(void *context, int id) {
    auto &pictures = *static_cast<std::vector<Texture> *>(context);
    return id >= 400 && id <= 403 ? pictures[id - 400].get() : nullptr;
}

void Application::renderDialogs() {
    if (!session.promotion.empty() && !animation.busy() && !session.pending) {
        require(resource_dialog_draw(rendererPtr, *resource_dialog_find(408, false), nullptr,
                                     promotionPicture, &textures.promote),
                "Promotion dialog");
    }
    if (textDialog.open) {
        const auto &dialog = *resource_dialog_find(textDialog.resourceId, false);
        require(resource_dialog_draw(rendererPtr, dialog), "Time dialog");
        const auto &field =
            original_dialog_items[dialog.first + (textDialog.resourceId == 404 ? 3 : 1)];
        int x = dialog.left + field.left + 4;
        int y = dialog.top + field.top + (chicago_available ? 1 : 4);
        setGrayscale(rendererPtr, 0);
        drawSystemText(rendererPtr, x, y, textDialog.text.c_str());
        size_t first = std::min(textDialog.cursor, textDialog.anchor);
        size_t count = std::max(textDialog.cursor, textDialog.anchor) - first;
        const int prefixWidth = systemTextWidth(std::string_view(textDialog.text).substr(0, first));
        const int textHeight = chicago_available ? chicago_ascent + chicago_descent : 8;
        if (count) {
            const int selectedWidth =
                systemTextWidth(std::string_view(textDialog.text).substr(first, count));
            drawRectangle(rendererPtr, x + prefixWidth, y, selectedWidth, textHeight);
            setGrayscale(rendererPtr, 255);
            drawSystemText(rendererPtr, x + prefixWidth, y,
                           textDialog.text.substr(first, count).c_str());
        } else {
            drawRectangle(rendererPtr, x + prefixWidth, y, 1, textHeight);
        }
    }
    if (alertId) {
        const char *params[] = {message.c_str(), "", "", ""};
        require(resource_dialog_draw(rendererPtr, *resource_dialog_find(alertId, true), params),
                "Alert dialog");
    }
}

void Application::captureScreenshot() {
    if (!options.screenshotPath.empty() && (rendered == 0 || (options.smoke && rendered >= 2))) {
        Surface s(SDL_RenderReadPixels(rendererPtr, nullptr), SDL_DestroySurface);
        require(bool(s), "Screenshot");
        auto output = rendered == 0 ? options.screenshotPath
                                    : options.screenshotPath.parent_path() /
                                          (options.screenshotPath.stem().string() + "-" +
                                           std::to_string(rendered) + ".png");
        require(SDL_SavePNG(s.get(), output.string().c_str()), "Save screenshot");
    }
}

void Application::render() {
    renderBoard();
    renderMenus();
    renderDialogs();
    captureScreenshot();
    require(SDL_RenderPresent(rendererPtr), "Present");
}
