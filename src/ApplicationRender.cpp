#include "ApplicationInternal.h"
#include "MacBitmapFont.h"
#include "hint_outline.h"
#include "presentation.h"
#include <algorithm>
#include <cmath>
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

/* Set Up Board edits on the single flat view with palettes at both content edges. */
bool Application::dualView() const {
    return bothBoards && !editing;
}

int Application::viewWidth() const {
    return dualView() ? logicalWidth + kFlatViewWidth : logicalWidth;
}

/* Native presentation adapter; no original entry point. Widen the logical canvas for
 * the side-by-side view and, when requested, resize the window to fit the display.
 * Whole-pixel scaling keeps the 1-bit artwork crisp; only a window smaller than the
 * canvas falls back to fractional letterboxing. */
void Application::applyLayout(bool resizeWindow) {
    const int width = viewWidth(), height = viewHeight();
    int outputWidth = 0, outputHeight = 0;
    SDL_GetRenderOutputSize(rendererPtr, &outputWidth, &outputHeight);
    auto mode = outputWidth >= width && outputHeight >= height
                    ? SDL_LOGICAL_PRESENTATION_INTEGER_SCALE
                    : SDL_LOGICAL_PRESENTATION_LETTERBOX;
    if (width != presentedWidth || height != presentedHeight || mode != presentedMode) {
        require(SDL_SetRenderLogicalPresentation(rendererPtr, width, height, mode),
                "Logical coordinates");
        presentedWidth = width;
        presentedHeight = height;
        presentedMode = mode;
    }
    if (!resizeWindow || fullscreen())
        return;
    double scale = 2;
    SDL_Rect usable;
    if (SDL_DisplayID display = SDL_GetDisplayForWindow(window);
        display && SDL_GetDisplayUsableBounds(display, &usable))
        scale = std::min({scale, double(usable.w) / width, double(usable.h) / height});
    if (scale >= 1)
        scale = std::floor(scale);
    SDL_SetWindowSize(window, int(width * scale), int(height * scale));
}

/* While a move animates on the perspective board, keep the flat view on the
 * pre-move position so both boards agree; afterwards show the committed position. */
const Position &Application::flatViewPosition() const {
    // Online the flat board shows the true position at once, so the player can think
    // and move there while the 3D board still animates.
    if (lichess.inGame && !session.promotionWalk)
        return session.game.position;
    if (animation.busy() || session.pending || session.promotionWalk)
        return session.animationBefore;
    return session.game.position;
}

/* Native side-by-side adapter; no original entry point. The primary view keeps
 * the original single-board path; the flat view is drawn into a second viewport. */
void Application::renderBoard() {
    setGrayscale(rendererPtr, 255);
    require(SDL_RenderClear(rendererPtr), "Clear");
    renderBoardView(flat, true);
    if (!dualView())
        return;
    // Shift the flat port so its board grid starts at the seam, and clip away its
    // left margin so it cannot paint over the perspective board.
    const int height = 20 + original_content_height;
    SDL_Rect port{logicalWidth - kFlatViewLeft, 0, kFlatViewLeft + kFlatViewWidth, height};
    SDL_Rect clip{kFlatViewLeft, 0, kFlatViewWidth, height};
    require(SDL_SetRenderViewport(rendererPtr, &port), "Flat view");
    require(SDL_SetRenderClipRect(rendererPtr, &clip), "Flat view clip");
    renderBoardView(true, false);
    require(SDL_SetRenderClipRect(rendererPtr, nullptr), "Reset clip");
    require(SDL_SetRenderViewport(rendererPtr, nullptr), "Reset view");
}

void Application::renderBoardView(bool viewFlat, bool primary) {
    SDL_FRect src{0, 0, 512, 330}, dst{0, 20, 512, 330};
    require(SDL_RenderTexture(rendererPtr,
                              viewFlat ? textures.flatBoard.get() : textures.perspective.get(),
                              &src, &dst),
            "Board");
    if (session.selected >= 0)
        drawSelection(rendererPtr, session.selected, viewFlat);
    /* Original PRINTHIN (0x66d0): source-first, twenty two-tick phases;
     * destination retains the previous pen until its next scheduled draw. */
    if (hint) {
        Uint64 ticks = (SDL_GetTicks() - hintStarted) * 60 / 1000;
        auto pens = original_hint_pens(uint32_t(ticks));
        if (!pens.from)
            hint.reset();
        else {
            drawSelection(rendererPtr, hint->from, viewFlat, pens.from);
            if (pens.to)
                drawSelection(rendererPtr, hint->to, viewFlat, pens.to);
        }
    }
    bool originalScene =
        primary && (animation.busy() || session.promotionWalk || session.mateAnimationDone ||
                    animation.displays(session.game.position, viewFlat));
    if (originalScene)
        animation.draw(false);
    const Position &shown =
        editing ? setup.position : primary ? session.game.position : flatViewPosition();
    uint8_t display[64];
    setup_display_board(&shown, display);
    // Native idle clip: replaces the resting bishop in the original draw order.
    auto idleFrame = primary && !viewFlat && !editing ? idle.current() : std::nullopt;
    // PLACE / INSERTFO: back ranks first, equal-y sprites newest first.
    if (!originalScene)
        for (int row = 0; row < 8; ++row)
            for (int col = 7; col >= 0; --col) {
                int i = row * 8 + col;
                if (!display[i])
                    continue;
                if (idleFrame && idleFrame->square == i) {
                    animation.drawFrame(idleFrame->frame, idleFrame->white, perspective_x[i],
                                        perspective_y[row]);
                    continue;
                }
                int shape = (viewFlat ? flat_shapes : perspective_shapes)[display[i] & 63] - 17;
                int count =
                    viewFlat ? int(std::size(flat_piece_shapes)) : int(std::size(standing_shapes));
                if (shape < 0 || shape >= count)
                    throw std::runtime_error("Invalid original shape selector");
                auto s = (viewFlat ? flat_piece_shapes : standing_shapes)[shape];
                int side = shown.board[display_to_engine(i)].side;
                auto &pieces = viewFlat ? textures.flatPieces : textures.standing;
                auto &t = pieces[side * count + shape];
                drawTexture(rendererPtr, t.get(), (viewFlat ? flat_x : perspective_x)[i] - s.hx,
                            20 + (viewFlat ? flat_y : perspective_y)[row] - s.hy, s.width,
                            s.height);
            }
    if (primary && editing)
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
    if (primary && editing && setup.heldPiece) {
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
    drawRectangle(rendererPtr, 0, 0, viewWidth(), 20);
    setGrayscale(rendererPtr, 0);
    drawRectangle(rendererPtr, 0, 19, viewWidth(), 1);
    // Native MenuSelect highlight adapter (original call 0x103f6).
    for (int i = 0; i < kMenuCount; ++i)
        if (editing ? i == 5
                    : i != 5 && (i != 6 || modem.connected()) &&
                          (i != kLichessMenu || lichessMenuVisible())) {
            if (i == menu) {
                setGrayscale(rendererPtr, 0);
                drawRectangle(rendererPtr, menuX[i], 0, menuWidth[i], 19);
            }
            setGrayscale(rendererPtr, i == menu ? 255 : 0);
            drawSystemText(rendererPtr, menuX[i], chicago_available ? 1 : 6, menuResource(i).title);
        }
    renderLichessClocks();
    if (menu >= 0) {
        auto &m = menuResource(menu);
        setGrayscale(rendererPtr, 255);
        drawRectangle(rendererPtr, menuLeft(menu), 20, 250, m.count * 18 + 4);
        setGrayscale(rendererPtr, 0);
        drawRectangle(rendererPtr, menuLeft(menu), 20, 250, m.count * 18 + 4, false);
        for (int i = 0; i < m.count; ++i) {
            bool selected = i == menuItem && menuItemEnabled(menu, i);
            if (selected) {
                setGrayscale(rendererPtr, 0);
                drawRectangle(rendererPtr, menuLeft(menu) + 1, 22 + i * 18, 248, 18);
            }
            setGrayscale(rendererPtr, selected ? 255 : menuItemEnabled(menu, i) ? 0 : 150);
            const char *label = m.items[i].label;
            if (std::string(label) == "-") {
                drawRectangle(rendererPtr, menuLeft(menu) + 3, 34 + i * 18, 244, 1);
                continue;
            }
            if (menu == 2 && i == 0)
                label = animation.soundEnabled ? "Turn Sound Off" : "Turn Sound On";
            if (menu == 2 && i == 1)
                label = flat ? "Switch to 3D Board" : "Switch to 2D Board";
            if (menu == kLichessMenu)
                label = lichessMenuLabel(i);
            if (menu == 2 && i == kFullScreenItem)
                label = fullscreen() ? "Exit Full Screen" : "Enter Full Screen";
            if ((menu == 2 && (i == 2 + settings.white_player || i == 5 + settings.black_player)) ||
                (menu == 2 && i == kIdleAnimationsItem && idle.enabled()) ||
                (menu == 3 && i == settings.level) || (menu == 6 && i == 2 && autoAnswer) ||
                (menu == 2 && i == kBothBoardsItem && bothBoards))
                drawSystemText(rendererPtr, menuLeft(menu) + 4, (chicago_available ? 22 : 26) + i * 18,
                               "+");
            drawSystemText(rendererPtr, menuLeft(menu) + 16, (chicago_available ? 22 : 26) + i * 18,
                           label);
            if (menu == 2 && i == kAnimationSpeedItem)
                drawSystemText(rendererPtr, menuLeft(menu) + 234,
                               (chicago_available ? 22 : 26) + i * 18, ">");
            if (m.items[i].shortcut) {
                char key[] = {char(m.items[i].shortcut), 0};
                drawSystemText(rendererPtr, menuLeft(menu) + 234,
                               (chicago_available ? 22 : 26) + i * 18, key);
            }
        }
        if (menu == 2 && menuItem == kAnimationSpeedItem &&
            menuItemEnabled(2, kAnimationSpeedItem)) {
            auto bounds = speedMenuBounds();
            setGrayscale(rendererPtr, 255);
            drawRectangle(rendererPtr, bounds.x, bounds.y, bounds.w, bounds.h);
            setGrayscale(rendererPtr, 0);
            drawRectangle(rendererPtr, bounds.x, bounds.y, bounds.w, bounds.h, false);
            constexpr const char *labels[] = {"1x", "1.25x", "1.5x", "1.75x", "2x"};
            for (int i = 0; i < 5; ++i) {
                int y = bounds.y + 2 + i * 18;
                bool selected = speedMenuItem == i;
                if (selected) {
                    setGrayscale(rendererPtr, 0);
                    drawRectangle(rendererPtr, bounds.x + 1, y, bounds.w - 2, 18);
                }
                setGrayscale(rendererPtr, selected ? 255 : 0);
                int textY = y + (chicago_available ? 0 : 4);
                if (animation.speed == 1 + i * 0.25)
                    drawSystemText(rendererPtr, bounds.x + 4, textY, "+");
                drawSystemText(rendererPtr, bounds.x + 16, textY, labels[i]);
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
        require(drawResourceDialog(rendererPtr,
                                   *findResourceDialog(408, ResourceDialogKind::dialog), nullptr,
                                   promotionPicture, &textures.promote),
                "Promotion dialog");
    }
    if (lichess.lobbyOpen)
        renderLichessLobby();
    if (textDialog.open && textDialog.resourceId >= kTokenPrompt) {
        renderLichessPrompt();
    } else if (textDialog.open) {
        const auto &dialog = *findResourceDialog(textDialog.resourceId, ResourceDialogKind::dialog);
        require(drawResourceDialog(rendererPtr, dialog), "Time dialog");
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
        require(drawResourceDialog(rendererPtr,
                                   *findResourceDialog(alertId, ResourceDialogKind::alert), params),
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
    // GETCOMMA 0x2744 / CHANGECU 0x10cd6: System CURS4 while input is blocked,
    // CURS400 during search, CURS401 in check. Native dialogs retain the arrow.
    SDL_Cursor *cursor = SDL_GetDefaultCursor();
    if (mouseX >= 0 && mouseX < viewWidth() && mouseY >= 20 &&
        mouseY < 20 + original_content_height && menu < 0 && !alertId && !textDialog.open &&
        !fileWaiting && !editing && session.promotion.empty()) {
        if (computer.busy())
            cursor = thinkingCursor.get();
        else if (animation.busy() || session.pending || modem.busy() ||
                 playerForSide(settings, session.game.position.side) == 2)
            cursor = blockedCursor.get();
        else if (bcGameInCheck(&session.game))
            cursor = checkedCursor.get();
    }
    if (SDL_GetCursor() != cursor)
        require(SDL_SetCursor(cursor), "Set cursor");
    applyLayout(false);
    renderBoard();
    renderPanel();
    renderMenus();
    renderDialogs();
    captureScreenshot();
    require(SDL_RenderPresent(rendererPtr), "Present");
}
