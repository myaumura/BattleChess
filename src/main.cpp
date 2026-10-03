/* 
 * SDL platform replacement. 
 * Original tables and translated rules remain separate.
 * Original animation, move generation, evaluation and selective search remain in the core. 
 */
#include <SDL3/SDL.h>
#include "game.h"
#include "time_control.h"
#include <algorithm>
#include <cstdlib>
#include "adjudication.h"
#include "save_game.h"
#include "setup_ui.h"
#include "resource_dialog.h"
#include "mac_bitmap_font.h"
#include "animation_host.h"
#include "computer_player.h"
#include "modem_host.h"
#include <cstring>
#include "hint_outline.h"
#include <optional>
#include "presentation.h"
#include "original.hpp"
#include "assets.hpp"
#include <array>
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs = std::filesystem;

struct FileResult {
    int operation;
    std::string path, error;
};

static Uint32 file_event;

/* Native SDL callback; no original entry point. Copy the picker result onto the
 * main event queue so a platform callback never mutates game state. */
static void SDLCALL file_selected(void *operation, const char *const *files, int) {
    auto *result = new FileResult{int(reinterpret_cast<intptr_t>(operation)), {}, {}};
    if (!files)
        result->error = SDL_GetError();
    else if (files[0])
        result->path = files[0];
    SDL_Event event{};
    event.type = file_event;
    event.user.data1 = result;
    if (!SDL_PushEvent(&event))
        delete result;
}

template <class T, auto D> using Handle = std::unique_ptr<T, decltype(D)>;

using Texture = Handle<SDL_Texture, SDL_DestroyTexture>;
using Surface = Handle<SDL_Surface, SDL_DestroySurface>;

/* Native resource adapter; no original entry point. Resolve PICT 400..403 for
 * the PAWNSELE dialog (original file offset 0x3d1c). */
static SDL_Texture *promotion_picture(void *context, int id) {
    auto &pictures = *static_cast<std::vector<Texture> *>(context);
    return id >= 400 && id <= 403 ? pictures[id - 400].get() : nullptr;
}

/* Native error boundary; no original entry point. Stop on failed platform
 * operations instead of drawing or saving incomplete results. */
static void require(bool ok, const std::string &what) {
    if (!ok)
        throw std::runtime_error(what + ": " + SDL_GetError());
}

/* Native SDL adapter; no original entry point. Own a texture and retain
 * nearest-neighbor scaling for the original pixel artwork. */
static Texture create_texture(SDL_Renderer *renderer, SDL_Surface *surface) {
    Texture result(SDL_CreateTextureFromSurface(renderer, surface), SDL_DestroyTexture);
    require(bool(result), "Create texture");
    require(SDL_SetTextureScaleMode(result.get(), SDL_SCALEMODE_NEAREST), "Nearest scaling");
    return result;
}

/* Native asset loader; no original entry point. Load an extracted PNG and
 * transfer its pixels to a renderer-owned texture. */
static Texture load_texture(SDL_Renderer *renderer, const fs::path &path) {
    Surface image(SDL_LoadPNG(path.string().c_str()), SDL_DestroySurface);
    require(bool(image), "Load " + path.string());
    return create_texture(renderer, image.get());
}

/* Native asset conversion; no direct original entry point. Expand packed
 * two-bit shapes with CHANGECO coloring (file offset 0xa450), keeping transparency. */
static std::vector<Texture> load_piece_textures(SDL_Renderer *renderer, 
                                                const fs::path &path,
                                                const OriginalShape *table, 
                                                int count) {
    std::ifstream in(path, std::ios::binary);
    if (!in)
        throw std::runtime_error("Missing " + path.string());
    std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(in)), {});
    std::vector<Texture> textures;
    for (int side = 0; side < 2; ++side)
        for (int shape_index = 0; shape_index < count; ++shape_index) {
            auto shape = table[shape_index];
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
            textures.push_back(create_texture(renderer, image.get()));
        }
    return textures;
}

/* Native SDL adapter; no original entry point. Set the monochrome drawing color. */
static void set_grayscale(SDL_Renderer *renderer, int gray) {
    require(SDL_SetRenderDrawColor(renderer, gray, gray, gray, 255), "Color");
}

/* Native SDL adapter; no original entry point. Draw filled panels or control borders. */
static void draw_rectangle(SDL_Renderer *renderer, float x, float y, float width, float height,
                           bool fill = true) {
    SDL_FRect bounds{x, y, width, height};
    require(fill ? SDL_RenderFillRect(renderer, &bounds) : SDL_RenderRect(renderer, &bounds),
            "Rectangle");
}

/* Native font adapter; System7 Chicago12 pixels/advances replace Toolbox text output. */
static void draw_system_text(SDL_Renderer *renderer, int x, int y, const char *label) {
    require(chicago_available ? mac_bitmap_draw(renderer, chicago_glyphs, chicago_pixels, x,
                                                y + chicago_ascent, label)
                              : SDL_RenderDebugText(renderer, x, y, label),
            "Text");
}
/* Native text-layout adapter; measure the same advances used for glyph rendering. */
static int system_text_width(std::string_view label) {
    int width = chicago_available ? mac_bitmap_width(chicago_glyphs, label) : int(label.size() * 8);
    require(width >= 0, "Unrepresentable Macintosh text");
    return width;
}
/* Native SDL adapter; no original entry point. Place an image in logical
 * Macintosh coordinates without changing its recovered pixel data. */
static void draw_texture(SDL_Renderer *renderer, SDL_Texture *image, float x, float y, float width,
                         float height) {
    SDL_FRect bounds{x, y, width, height};
    require(SDL_RenderTexture(renderer, image, nullptr, &bounds), "Image");
}
/* Original: DRAWSQUA, file offset 0xb62e. Outline the selected board square
 * using the original perspective/flat polygon and doubled edge path. */
static void draw_selection(SDL_Renderer *renderer, int square, bool flat, unsigned pen = 0) {
    int display_square = engine_to_display(square), row = display_square / 8,
        column = display_square % 8;
    const int *x_coordinates = flat ? flat_hit_x : perspective_hit_x;
    const int *y_coordinates = flat ? flat_hit_y : perspective_hit_y;
    int top_left_x = x_coordinates[row * 9 + column] - 1,
        top_right_x = x_coordinates[row * 9 + column + 1],
        bottom_left_x = x_coordinates[(row + 1) * 9 + column] - 1,
        bottom_right_x = x_coordinates[(row + 1) * 9 + column + 1];
    int top = 20 + y_coordinates[row], bottom = 21 + y_coordinates[row + 1];
    /* Original: DRAW, file offset 0xbf0e. Keep the original >= tie decisions
     * so diagonal selection borders rasterize on the same pixels. */
    auto line = [&](int x, int y, int endx, int endy) {
        int dx = std::abs(endx - x), dy = std::abs(endy - y), sx = x < endx ? 1 : -1,
            sy = y < endy ? 1 : -1, err = dx - dy;
        for (;;) {
            if (pen)
                set_grayscale(renderer, original_outline_gray(pen, x));
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
    set_grayscale(renderer, 255);
    line(top_left_x, top, top_right_x, top);
    line(top_right_x, top, bottom_right_x, bottom);
    line(bottom_right_x, bottom, bottom_left_x, bottom);
    line(bottom_left_x, bottom, top_left_x, top);
    line(top_left_x + 1, top + 1, top_right_x - 1, top + 1);
    line(top_right_x - 1, top + 1, top_right_x - 1, top);
    line(top_right_x - 1, top, bottom_right_x - 1, bottom);
    line(bottom_right_x - 1, bottom, bottom_right_x - 1, bottom - 1);
    line(bottom_right_x - 1, bottom - 1, bottom_left_x + 1, bottom - 1);
    line(bottom_left_x + 1, bottom - 1, bottom_left_x + 1, bottom);
    line(bottom_left_x + 1, bottom, top_left_x + 1, top);
}

/* Native editable-field state for SETTIME 0x381e, DIALNUMB 0x39ca and
 * SENDMESS 0x3bce. SDL supplies text events; resource IDs retain the original
 * numeric, dial-number and chat input limits. No Toolbox pointer is retained. */
struct TextDialog {
    int resource_id = 403;
    bool open = false;
    std::string text;
    size_t cursor = 0;
    size_t anchor = 0;

    /* Native selection adapter; no original entry point. Opening SETTIME
     * selects the existing value so the next input replaces it. */
    void select_all() {
        cursor = text.size();
        anchor = 0;
    }

    /* Native input guard; no original entry point. Validate the whole input
     * before replacing the selection, preserving each resource's ASCII limit. */
    void replace(const std::string &input) {
        for (char character : input)
            if (resource_id == 403
                    ? ((character < '0' || character > '9') && character != '-' && character != '+')
                    : (character < 32 || character >= 127))
                return;
        const size_t first = std::min(cursor, anchor);
        const size_t count = std::max(cursor, anchor) - first;
        const size_t limit = resource_id == 403 ? 10 : resource_id == 404 ? 38 : 40;
        if (text.size() - count + input.size() > limit)
            return;
        text.replace(first, count, input);
        cursor = anchor = first + input.size();
    }

    /* Native keyboard adapter; no original entry point. Cursor/selection
     * edits stay local; Enter completion remains with the dialog's caller. */
    void key(SDL_Keycode key, SDL_Keymod modifiers) {
        if (key == SDLK_A && (modifiers & (SDL_KMOD_CTRL | SDL_KMOD_GUI))) {
            select_all();
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
};

/* Native player-settings lookup for GETSETTI 0x74b4. Retain original control
 * codes (Human 0, Mac 1, Modem 2) when deciding whose turn to schedule. */
static uint8_t player_for_side(const BCSaveSettings &settings, unsigned side) {
    return side ? settings.black_player : settings.white_player;
}

struct Play {
    BCGame game{};
    std::vector<BCGame> past, future;
    std::vector<BCMove> promotion;
    int selected = -1;
    bool mate_animation_done = false, ending_announced = false, promotion_walk = false;
    bool computer_requested = false, book_eligible = true, pending_sent = false;
    std::optional<BCMove> pending;
    Position animation_before{};
    /* Native session initializer; no direct original entry point. Start from
     * RESETGAM (file offset 0x5724) with empty undo/presentation state. */
    Play() {
        bc_game_init(&game);
    }
    /* Native move coordinator; related to CHECKOPT at file offset 0x58bc.
     * Keep an undo snapshot and stage presentation only after a legal move commits. */
    bool apply(BCMove move, bool animate = true, bool computer_move = false) {
        BCGame before = game;
        bool committed = computer_move ? bc_game_commit_search_move(&game, move)
                                       : bc_game_apply(&game, move, nullptr);
        if (committed) {
            computer_requested = false;
            ending_announced = false;
            past.push_back(before);
            future.clear();
            selected = -1;
            animation_before = before.position;
            if (animate)
                pending = move;
        }
        promotion.clear();
        promotion_walk = false;
        return committed;
    }
    /* Native input adapter for CHECKOPT (file offset 0x58bc). Resolve source
     * and destination; WALKIFPR (0x75c4) travels as a pawn before PAWNSELE. */
    void click(int display) {
        if (display < 0)
            return;
        int square = display_to_engine(display);
        if (selected >= 0) {
            BCMove moves[80];
            auto move_count = bc_game_legal_moves(&game, moves);
            std::vector<BCMove> matching;
            for (size_t i = 0; i < move_count; ++i)
                if (moves[i].from == selected && moves[i].to == square)
                    matching.push_back(moves[i]);
            if (matching.size() == 1) {
                apply(matching[0]);
                return;
            }
            if (matching.size() > 1) {
                selected = -1;
                promotion = matching;
                animation_before = game.position;
                BCMove pawn = matching[0];
                pawn.piece = 6;
                pawn.special = 0;
                pending = pawn;
                promotion_walk = true;
                return;
            }
        }
        auto piece = game.position.board[square];
        selected = piece.piece && piece.side == game.position.side ? square : -1;
    }
    /* Native snapshot adapter; no one-to-one original address. Restore the
     * previous position through the original quick-placement behavior, without animation. */
    void undo() {
        if (past.empty())
            return;
        computer_requested = false;
        future.push_back(game);
        game = past.back();
        past.pop_back();
        selected = -1;
        promotion.clear();
        ending_announced = false;
    }
    /* Native snapshot adapter; no one-to-one original address. Reapply a
     * position undone in this session without generating a new move or animation. */
    void replay() {
        if (future.empty())
            return;
        computer_requested = false;
        past.push_back(game);
        game = future.back();
        future.pop_back();
        selected = -1;
        ending_announced = false;
    }
};
/* Native application host; no single original entry point. Replace Toolbox
 * startup and event dispatch while calling the recovered core and resource UI. */
static int run(int argc, char **argv) {
    fs::path data, screenshot_path;
    std::string serial_path;
    bool modem_quit_window = false;
    bool smoke = false, check = false, flat = false, animation_check = false, modem_check = false;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if ((a == "--data" || a == "--screenshot") && i + 1 < argc) {
            if (a == "--data")
                data = argv[++i];
            else
                screenshot_path = argv[++i];
        } else if (a == "--serial" && i + 1 < argc)
            serial_path = argv[++i];
        else if (a == "--animation-check")
            animation_check = true;
        else if (a == "--modem-quit-window")
            modem_quit_window = true;
        else if (a == "--modem-check")
            modem_check = true;
        else if (a == "--smoke")
            smoke = true;
        else if (a == "--check-assets")
            check = true;
        else if (a == "--flat")
            flat = true;
        else if (a == "--help") {
            std::cout << "--data PATH [--flat] [--smoke] [--check-assets] [--screenshot "
                         "FILE] [--serial DEVICE]\nClick a piece, then its destination. File / New "
                         "Game; Move / Take "
                         "Back, Replay, Force Move or Suggest Move.\nOriginal computer search; "
                         "Human White and Mac Black by default.\n";
            return 0;
        } else
            throw std::runtime_error("Unknown argument: " + a);
    }
    if (data.empty())
        throw std::runtime_error("Use run.sh with BC_DATA_DIR or --data PATH");
    const fs::path assets = data / "assets";
    require(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO), "SDL initialization");
    struct Quit {
        // Native lifetime guard; no original address. Release SDL even after an exception.
        ~Quit() {
            SDL_Quit();
        }
    } quit;
    // WIND400 (GetNewWindow at file0xffa8) defines the visible board port.
    // The native menu bar remains a separate twenty-pixel SDL adapter above it.
    const int logical_width = original_content_width;
    const int logical_height = 20 + original_content_height;
    Handle<SDL_Window, SDL_DestroyWindow> window(
        SDL_CreateWindow("Battle Chess", 2 * logical_width, 2 * logical_height,
                         SDL_WINDOW_RESIZABLE |
                             ((smoke || animation_check || modem_check) ? SDL_WINDOW_HIDDEN : 0)),
        SDL_DestroyWindow);
    require(bool(window), "Window");
    // Native window metadata; no original address. Use the project's original knight artwork.
    if (const char *base = SDL_GetBasePath()) {
        Surface icon(SDL_LoadPNG((fs::path(base) / "battlechess-icon.png").string().c_str()),
                     SDL_DestroySurface);
        if (icon)
            SDL_SetWindowIcon(window.get(), icon.get());
    }
    Handle<SDL_Renderer, SDL_DestroyRenderer> renderer(SDL_CreateRenderer(window.get(), nullptr),
                                                       SDL_DestroyRenderer);
    require(bool(renderer), "Renderer");
    auto *renderer_ptr = renderer.get();
    require(SDL_SetRenderLogicalPresentation(renderer_ptr, logical_width, logical_height,
                                             SDL_LOGICAL_PRESENTATION_LETTERBOX),
            "Logical coordinates");
    if (!smoke && !animation_check && !modem_check) {
        auto title = load_texture(renderer_ptr, assets / "external_graphics/screens/title.png");
        set_grayscale(renderer_ptr, 255);
        require(SDL_RenderClear(renderer_ptr), "Title clear");
        draw_texture(renderer_ptr, title.get(), 0, 20, 512, 330);
        require(SDL_RenderPresent(renderer_ptr), "Title");
    }
    auto perspective =
             load_texture(renderer_ptr, assets / "external_graphics/screens/perspective_board.png"),
         flat_board =
             load_texture(renderer_ptr, assets / "external_graphics/screens/flat_board.png");
    auto standing =
        load_piece_textures(renderer_ptr, assets / "external_graphics/standing/decoded.2bpp",
                            standing_shapes, int(std::size(standing_shapes)));
    auto flat_pieces = load_piece_textures(
        renderer_ptr, assets / "external_graphics/flat_pieces/decoded.2bpp", flat_piece_shapes, 6);
    std::vector<Texture> promote;
    for (int i = 400; i <= 403; ++i)
        promote.push_back(load_texture(renderer_ptr, assets / "pictures" /
                                                         ("PICT_" + std::to_string(i) + ".png")));
    if (check)
        for (auto &f : frames) {
            auto t = load_texture(renderer_ptr, assets / f.path);
            float x, y;
            require(SDL_GetTextureSize(t.get(), &x, &y), "Asset size");
            if (x != f.width || y != f.height)
                throw std::runtime_error("Asset dimensions differ");
        }
    // Original initialized DATA 0xffd14; book, search and fades share one stream.
    uint32_t random_state = 1;
    AnimationHost animation(renderer_ptr, assets, data / "animation_pixels.bin", random_state);
    animation.fast = smoke || animation_check;
    if (animation_check) {
        unsigned cases = 0;
        /* Native integration check; no original address. Drive a real recovered
         * animation to completion and reject stalled graphs or a lost mover. */
        auto exercise = [&](const Position &p, BCMove m, bool flat) {
            animation.begin(p, m, flat);
            unsigned ticks = 0;
            while (animation.busy() && ticks++ < 20000) {
                animation.tick(animation.scene.previous_ticks + 6);
                if (ticks % 7 == 0)
                    animation.draw();
                if (!screenshot_path.empty() && m.piece == 6 && m.captured == 6 && ticks == 70) {
                    animation.draw();
                    Surface image(SDL_RenderReadPixels(renderer_ptr, nullptr), SDL_DestroySurface);
                    require(bool(image), "Animation screenshot");
                    require(SDL_SavePNG(image.get(), screenshot_path.string().c_str()),
                            "Animation screenshot save");
                }
            }
            if (animation.busy())
                throw std::runtime_error(
                    "Animation integration timeout case=" + std::to_string(cases) +
                    " piece=" + std::to_string(m.piece) + " victim=" + std::to_string(m.captured) +
                    " from=" + std::to_string(m.from) + " to=" + std::to_string(m.to) +
                    " head=" + std::to_string(animation.scene.head) +
                    " type=" + std::to_string(animation.scene.active[animation.scene.head].type));
            int dest = engine_to_display(m.to);
            if (!animation.scene.board[dest])
                throw std::runtime_error("Animation lost moving piece");
            ++cases;
        };
        Position p{};
        reset_board(&p);
        exercise(p, {0x34, 0x14, 0, 6, 0}, false);
        exercise(p, {0x34, 0x14, 0, 6, 0}, true);
        for (unsigned side = 0; side < 2; ++side)
            for (unsigned piece = 1; piece <= 6; ++piece)
                for (unsigned victim = 1; victim <= 6; ++victim) {
                    if (piece == 1 && victim == 1)
                        continue;
                    uint16_t to = piece == 4 || piece == 6 ? (side ? 0x22 : 0x44)
                                  : piece == 5             ? 0x54
                                  : piece == 1             ? 0x34
                                                           : 0x37;
                    p = {};
                    insert_piece(&p, piece, side, 0x33);
                    insert_piece(&p, victim, 1 - side, to);
                    exercise(p, {to, 0x33, 0, uint8_t(piece), uint8_t(victim)}, false);
                }
        for (unsigned piece = 1; piece <= 6; ++piece)
            for (int to : {0x22, 0x23, 0x24, 0x32, 0x34, 0x42, 0x43, 0x44, 0x12, 0x14, 0x21, 0x25,
                           0x41, 0x45, 0x52, 0x54}) {
                int dx = std::abs((to & 7) - 3), dy = std::abs((to >> 4) - 3);
                if ((piece == 1 && (dx > 1 || dy > 1)) || (piece == 3 && dx && dy) ||
                    (piece == 4 && dx != dy) || (piece == 5 && dx * dy != 2) ||
                    (piece == 6 && to != 0x43) || (piece == 2 && dx && dy && dx != dy))
                    continue;
                p = {};
                insert_piece(&p, piece, 0, 0x33);
                exercise(p, {uint16_t(to), 0x33, 0, uint8_t(piece), 0}, false);
            }
        reset_board(&p);
        for (BCMove m : std::initializer_list<BCMove>{{0x20, 1, 0, 5, 0},
                                                      {0x22, 1, 0, 5, 0},
                                                      {0x25, 6, 0, 5, 0},
                                                      {0x27, 6, 0, 5, 0},
                                                      {0x50, 0x71, 0, 5, 0},
                                                      {0x52, 0x71, 0, 5, 0}})
            exercise(p, m, false);
        p = {};
        insert_piece(&p, 1, 0, 4);
        insert_piece(&p, 3, 0, 7);
        exercise(p, {6, 4, 1, 1, 0}, false);
        p = {};
        insert_piece(&p, 6, 0, 0x44);
        insert_piece(&p, 6, 1, 0x43);
        exercise(p, {0x53, 0x44, 1, 6, 0}, false);
        std::cout << "Animation integration: " << cases
                  << " move/capture/turn/special cases terminated and rendered\n";
        return 0;
    }
    Play play;
    ComputerPlayer computer(random_state);
    ModemHost modem(serial_path);
    require(serial_path.empty() || modem.connected(), "Open serial device");
    bool auto_answer = false, waiting_for_remote_end = false;
    std::vector<BCMove> remote_promotion;
    uint8_t remote_promotion_piece = 0;
    std::optional<BCMove> hint;
    Uint64 hint_started = 0;
    unsigned computer_ending = 0;
    int menu = -1, menu_item = -1;
    bool running = true, quitting = false;
    Uint64 quit_deadline = 0;
    int rendered = 0;
    BCSaveFile saved{};
    BCSaveSettings settings{0, 0, uint8_t(smoke ? 0 : 1), 0, 1, 0};
    /* Quit 0x2cd4/0x3cbe: both native quit paths send the original modem line. */
    auto request_quit = [&]() {
        if (!quitting && modem.connected() &&
            (settings.white_player == 2 || settings.black_player == 2)) {
            quitting = true;
            quit_deadline = SDL_GetTicks() + 2000;
            modem.quit();
        } else if (!quitting)
            running = false;
    };
    BCThinkingTime thinking_time{0, 6};
    TextDialog text_dialog;
    fs::path current_file;
    float mouse_x = 0, mouse_y = 0;
    bool file_waiting = false, editing = false, previous_flat = false;
    BCSetupUI setup{};
    int alert_id = 0, confirm_action = 0;
    ResourceDialogButton dialog_button;
    std::string message;
    file_event = SDL_RegisterEvents(1);
    require(file_event != Uint32(-1), "File dialog event");
    const int menu_ids[] = {401, 402, 403, 404, 400, 405, 406};
    const int menu_x[] = {30, 80, 130, 220, 8, 30, 275};
    const int menu_width[] = {45, 45, 80, 45, 18, 160, 60};
    /* Native MENU-resource lookup; no original address. Keep titles, actions
     * and shortcuts tied to the extracted menu inventory. */
    auto menu_for = [&](int index) -> const OriginalMenu & {
        for (auto &m : original_menus)
            if (m.id == menu_ids[index])
                return m;
        throw std::runtime_error("Missing MENU");
    };
    /* Native host for CHESSALE (file offset 0x3aba), using ALRT 406.
     * Queue a message instead of blocking the SDL event loop. */
    auto alert = [&](const std::string &value) {
        dialog_button.cancel();
        alert_id = 406;
        message = value;
        confirm_action = 0;
    };
    /* SENDBOAR 0x112c2: only the original 33-byte FILLSAVE body. */
    auto send_board = [&]() {
        if (modem.connected() && (settings.white_player == 2 || settings.black_player == 2)) {
            uint8_t board[33];
            fill_save(&play.game.position, board);
            modem.packet(0xa1, board, sizeof board);
        }
    };
    /* Native new-game coordinator; RESETGAM is at file offset 0x5724.
     * Drop session history, pending presentation and the previous save association. */
    auto reset = [&]() {
        computer.cancel();
        hint.reset();
        computer_ending = 0;
        animation.cancel();
        play = Play();
        play.computer_requested = settings.white_player == 1;
        saved = {};
        current_file.clear();
        remote_promotion.clear();
        remote_promotion_piece = 0;
        modem.clear_ended();
    };
    /* Native host for SETTIME, file offset 0x381e. DLOG 403 starts with all
     * existing minute text selected, so typing replaces the old value. */
    auto open_time_dialog = [&]() {
        dialog_button.cancel();
        text_dialog.resource_id = 403;
        text_dialog.text = std::to_string(bc_time_dialog_minutes(&thinking_time));
        text_dialog.select_all();
        text_dialog.open = true;
        require(SDL_StartTextInput(window.get()), "Start time input");
    };
    /* Native dialog completion for SETTIME (0x381e) / SETLEVEL (0x6838).
     * The original dialog has OK only; clamp numeric input before storing seconds. */
    auto accept_text_dialog = [&]() {
        dialog_button.cancel();
        if (text_dialog.resource_id != 403) {
            if (!text_dialog.text.empty()) {
                if (text_dialog.resource_id == 405)
                    modem.text("ATDT " + text_dialog.text + "\r");
                else
                    modem.text(text_dialog.text == "+++"
                                   ? text_dialog.text
                                   : text_dialog.text + std::string("\0\r", 2));
            }
            text_dialog.open = false;
            require(SDL_StopTextInput(window.get()), "Stop modem input");
            return;
        }
        long long minutes = std::strtoll(text_dialog.text.c_str(), nullptr, 10);
        bc_time_set_minutes(&thinking_time, int32_t(std::clamp(minutes, -1LL, 10001LL)));
        settings.level = thinking_time.level;
        text_dialog.open = false;
        require(SDL_StopTextInput(window.get()), "Stop time input");
    };
    /* Native dialog command adapter for SETTIME 0x381e / SETLEVEL 0x6838.
     * Complete on Enter; ordinary editing only changes the local text field. */
    auto text_key = [&](SDL_Keycode key, SDL_Keymod modifiers) {
        if (key == SDLK_RETURN || key == SDLK_KP_ENTER)
            accept_text_dialog();
        else
            text_dialog.key(key, modifiers);
    };
    /* Native file adapter for SAVEREQ (file offset 0x8d34). Preserve opaque
     * original bytes and change the active filename only after an atomic write. */
    auto save = [&](const fs::path &path) {
        BCSaveFile candidate = saved;
        settings.board_2d = flat;
        settings.sound = animation.sound_enabled;
        if (!bc_save_encode(&candidate, &play.game.position, &settings) ||
            !bc_save_write(path.string().c_str(), &candidate)) {
            alert("Unable to save game.");
            return;
        }
        saved = candidate;
        current_file = path;
    };
    /* Native event adapter for READLOAD (file offset 0x8c9e). Commit a
     * decoded file only after validation; do not silently replace unsupported player modes. */
    auto receive_file = [&](const FileResult &result) {
        file_waiting = false;
        if (!result.error.empty()) {
            alert(result.error);
            return;
        }
        if (result.path.empty())
            return;
        if (result.operation == 2) {
            save(result.path);
            return;
        }
        BCSaveFile candidate{};
        Position position{};
        BCSaveSettings loaded{};
        if (!bc_save_read(result.path.c_str(), &candidate, &position, &loaded)) {
            alert("Unable to open game.");
            return;
        }
        // Preserve unsupported mode data by refusing the load, rather than changing the game
        // silently.
        if (loaded.white_player > 2 || loaded.black_player > 2) {
            alert("Unsupported player control in saved game.");
            return;
        }
        if (loaded.level > 10) {
            alert("Unsupported level in saved game.");
            return;
        }
        computer.cancel();
        hint.reset();
        computer_ending = 0;
        animation.cancel();
        play = Play();
        // LOADREQ expands the board before GETSETTI replaces player controls.
        play.computer_requested = player_for_side(settings, position.side) == 1;
        play.book_eligible = false;
        bc_setup_commit(&play.game, &position);
        saved = candidate;
        send_board();
        settings = loaded;
        flat = loaded.board_2d;
        animation.sound_enabled = loaded.sound;
        current_file = result.path;
        // GETSETTI (0x74b4) invokes SETLEVEL. Custom minutes are not stored in
        // the 78-byte file, so loading level 10 opens SETTIME again.
        if (loaded.level == 10)
            open_time_dialog();
        else
            bc_time_select_level(&thinking_time, loaded.level);
    };
    /* Native OS picker; no original entry point. Replace Standard File
     * selection while keeping the original save data format. */
    auto file_dialog = [&](bool write) {
        file_waiting = true;
        std::string path = current_file.string();
        if (write)
            SDL_ShowSaveFileDialog(file_selected, reinterpret_cast<void *>(intptr_t(2)),
                                   window.get(), nullptr, 0, path.empty() ? nullptr : path.c_str());
        else
            SDL_ShowOpenFileDialog(file_selected, reinterpret_cast<void *>(intptr_t(1)),
                                   window.get(), nullptr, 0, nullptr, false);
    };
    /* Native menu availability; no direct original entry point. Protect modal
     * transitions while allowing force/cancel commands during asynchronous search. */
    auto enabled = [&](int m, int item) {
        if (modem.busy() || file_waiting || alert_id || text_dialog.open || animation.busy() ||
            play.pending || !play.promotion.empty())
            return false;
        if (editing)
            return m == 5 && item >= 0 && item < 3;
        if (computer.busy())
            return (m == 1 && item == 0) || (m == 0 && (item == 0 || item == 1 || item == 6)) ||
                   (m == 2 && (item == 2 || item == 3 || item == 5 || item == 6));
        return (m == 0 && (item <= 4 || item == 6)) ||
               (m == 1 && ((item == 0 && player_for_side(settings, play.game.position.side) == 1) ||
                           item == 3 || (item == 1 && !play.past.empty()) ||
                           (item == 2 && !play.future.empty()))) ||
               (m == 2 && (item == 0 || item == 1 || item == 2 || item == 3 || item == 5 ||
                           item == 6 || (modem.connected() && (item == 4 || item == 7)))) ||
               (m == 6 && modem.connected() && item >= 0 && item < 4) || (m == 4 && item == 0) ||
               (m == 3 && item >= 0 && item <= 10);
    };
    /* Native dispatch for CHECKOPT (file offset 0x58bc) and BOARDSET
     * (0x2d9c). Route original menu items to recovered behavior or platform adapters. */
    auto action = [&](int m, int item) {
        if (!enabled(m, item))
            return;
        // HANDLEME 0x10604 clears menu highlighting for both MenuSelect and MenuKey.
        menu = menu_item = -1;
        if (m == 0) {
            if (item == 0) {
                alert_id = 402;
                message = original_message_new_game;
                confirm_action = 1;
            }
            if (item == 1)
                file_dialog(false);
            if (item == 2) {
                if (current_file.empty())
                    file_dialog(true);
                else
                    save(current_file);
            }
            if (item == 3)
                file_dialog(true);
            if (item == 4) {
                computer.cancel();
                hint.reset();
                computer_ending = 0;
                animation.cancel();
                previous_flat = flat;
                flat = true;
                editing = true;
                bc_setup_ui_begin(&setup, &play.game.position);
                play.selected = -1;
            }
            if (item == 6)
                request_quit();
        }
        if (m == 1 && item == 0) {
            if (computer.busy())
                computer.force();
            else
                play.computer_requested = true;
        }
        if (m == 1 && item == 3) {
            hint.reset();
            play.selected = -1;
            play.computer_requested = false;
            computer.start(play.game, play.past, play.book_eligible, settings.level,
                           bc_time_search_seconds(&thinking_time), true);
        }
        if (m == 1 && item == 1) {
            computer.cancel();
            hint.reset();
            computer_ending = 0;
            animation.cancel();
            play.mate_animation_done = false;
            play.undo();
        }
        if (m == 1 && item == 2) {
            computer.cancel();
            hint.reset();
            computer_ending = 0;
            animation.cancel();
            play.mate_animation_done = false;
            play.replay();
        }
        if (m == 2 && item == 0)
            animation.sound_enabled = !animation.sound_enabled;
        if (m == 2 && item == 1) {
            animation.cancel();
            flat = !flat;
            play.selected = -1;
        }
        if (m == 2 && item >= 2 && item <= 7) {
            computer.cancel();
            hint.reset();
            play.selected = -1;
            if (item < 5)
                settings.white_player = uint8_t(item - 2);
            else
                settings.black_player = uint8_t(item - 5);
            if (item == 4 || item == 7)
                modem.text("ATE\r");
            play.computer_requested = player_for_side(settings, play.game.position.side) == 1;
        }
        if (m == 6) {
            if (item == 0 || item == 3) {
                text_dialog.resource_id = item == 0 ? 405 : 404;
                text_dialog.text.clear();
                text_dialog.cursor = text_dialog.anchor = 0;
                text_dialog.open = true;
                require(SDL_StartTextInput(window.get()), "Start modem input");
            }
            if (item == 1)
                modem.hang_up();
            if (item == 2) {
                auto_answer = !auto_answer;
                modem.text(auto_answer ? "ATS0=1\r" : "ATS0=0\r");
            }
        }
        if (m == 3) {
            if (item == 10)
                open_time_dialog();
            else if (bc_time_select_level(&thinking_time, unsigned(item)))
                settings.level = thinking_time.level;
        }
        if (m == 4) {
            alert_id = 400;
            message.clear();
        }
        if (m == 5) {
            if (item == 0) {
                alert_id = 402;
                message = original_message_clear_board;
                confirm_action = 2;
            }
            if (item == 1)
                bc_setup_ui_restore(&setup);
            if (item == 2) {
                if (auto error = bc_setup_ui_done(&setup, &play.game))
                    alert(error);
                else {
                    play.past.clear();
                    play.future.clear();
                    play.book_eligible = false;
                    play.computer_requested =
                        player_for_side(settings, play.game.position.side) == 1;
                    play.mate_animation_done = false;
                    play.ending_announced = false;
                    editing = false;
                    flat = previous_flat;
                    send_board();
                }
            }
        }
    };
    /* Native alert acknowledgement; DOCHECKM (file offset 0x716a) calls
     * the reset at 0x5692 after terminal messages. Cancel must not change the board. */
    auto dismiss = [&](int item) {
        dialog_button.cancel();
        if (item == 1 && confirm_action == 3 && modem.connected() &&
            (settings.white_player == 2 || settings.black_player == 2) && !modem.ended()) {
            waiting_for_remote_end = true;
            return;
        }
        int command = confirm_action;
        alert_id = 0;
        confirm_action = 0;
        if (item == 1) {
            if (command == 1) {
                reset();
                send_board();
            }
            if (command == 2)
                bc_setup_ui_clear(&setup);
            if (command == 3)
                reset();
        }
    };
    /* Native MenuSelect adapter (original call 0x103f6): hit-test visible headings. */
    auto menu_heading = [&](int x, int y) {
        if (y < 0 || y >= 20)
            return -1;
        for (int i = 0; i < 7; ++i)
            if ((editing ? i == 5 : i != 5 && (i != 6 || modem.connected())) && x >= menu_x[i] &&
                x < menu_x[i] + menu_width[i])
                return i;
        return -1;
    };
    /* Native MenuSelect adapter (0x103f6): track headings and enabled rows while held. */
    auto track_menu = [&](int x, int y) {
        if (menu < 0)
            return;
        int heading = menu_heading(x, y);
        if (heading >= 0)
            menu = heading;
        menu_item = -1;
        if (x >= menu_x[menu] && x < menu_x[menu] + 250 && y >= 22) {
            int item = (y - 22) / 18;
            if (item < menu_for(menu).count && enabled(menu, item) &&
                std::string(menu_for(menu).items[item].label) != "-")
                menu_item = item;
        }
    };
    /* Native ModalDialog/MenuSelect adapter (0x3894/0x103f6): dispatch mouse
     * commands only on release; a drag cannot activate a different button. */
    auto release_pointer = [&](int x, int y) {
        const auto *dialog = text_dialog.open ? resource_dialog_find(text_dialog.resource_id, false)
                             : alert_id       ? resource_dialog_find(alert_id, true)
                                              : nullptr;
        int item = dialog_button.release(dialog, x, y);
        if (dialog) {
            if (item == 1 && text_dialog.open)
                accept_text_dialog();
            else if (item == 2 && text_dialog.open && text_dialog.resource_id == 404) {
                text_dialog.open = false;
                require(SDL_StopTextInput(window.get()), "Stop modem input");
            } else if (alert_id && item)
                dismiss(item);
            return;
        }
        if (menu < 0)
            return;
        track_menu(x, y);
        int selected_menu = menu, selected_item = menu_item;
        menu = menu_item = -1;
        if (selected_item >= 0)
            action(selected_menu, selected_item);
    };
    /* Native scheduling bridge to DOWALK (file offset 0xd3bc). Start each
     * committed move once, using its saved pre-move position. */
    /* DOWALK 0xd3bc / FUN0x7630 send to a modem opponent, never echo remote turns. */
    auto send_move = [&](BCMove move, unsigned moving_side) {
        if (modem.connected() &&
            (moving_side ? settings.white_player : settings.black_player) == 2 &&
            (moving_side ? settings.black_player : settings.white_player) != 2) {
            uint8_t squares[] = {uint8_t(move.to), uint8_t(move.from)};
            modem.packet(0xa2, squares, 2);
            if (move.special && move.piece != 1 && move.piece != 6)
                modem.packet(0xa9, &move.piece, 1);
        }
    };
    auto sync_animation = [&]() {
        if (play.pending) {
            if (!play.pending_sent) {
                if (!play.promotion_walk)
                    send_move(*play.pending, play.animation_before.side);
                play.pending_sent = true;
            }
            if (modem.busy() || animation.busy())
                return;
            animation.begin(play.animation_before, *play.pending, flat);
            play.pending.reset();
            play.pending_sent = false;
        }
    };
    /* Native hit-test dispatcher; no single original entry point. Modal
     * dialogs and setup consume clicks before ordinary board interaction. */
    auto click = [&](int x, int y) {
        if (file_waiting || modem.busy())
            return;
        if (text_dialog.open) {
            const auto &dialog = *resource_dialog_find(text_dialog.resource_id, false);
            dialog_button.press(dialog, x, y);
            int item = resource_dialog_hit(dialog, x, y);
            if (item == (text_dialog.resource_id == 404 ? 4 : 2)) {
                const auto &field =
                    original_dialog_items[dialog.first + (text_dialog.resource_id == 404 ? 3 : 1)];
                int relative_x = x - dialog.left - field.left - 4;
                text_dialog.cursor = text_dialog.anchor =
                    chicago_available
                        ? mac_bitmap_caret(chicago_glyphs, text_dialog.text, relative_x)
                        : std::min(text_dialog.text.size(), size_t(std::max(0, relative_x / 8)));
            }
            return;
        }
        if (alert_id) {
            auto *d = resource_dialog_find(alert_id, true);
            dialog_button.press(*d, x, y);
            return;
        }
        if (animation.busy() || play.pending)
            return;
        if (!play.promotion.empty()) {
            auto *d = resource_dialog_find(408, false);
            int item = resource_dialog_hit(*d, x, y);
            const int piece[] = {4, 5, 2, 3};
            if (item >= 2 && item <= 5)
                for (auto m : play.promotion)
                    if (m.piece == piece[item - 2]) {
                        animation.cancel();
                        send_move(m, play.game.position.side);
                        if (play.apply(m, false))
                            play.computer_requested =
                                player_for_side(settings, play.game.position.side) == 1;
                        return;
                    }
            return;
        }
        if (y < 20) {
            menu = menu_heading(x, y);
            menu_item = -1;
            return;
        }
        if (menu >= 0)
            return;
        int display = original_hit_square(x, y - 20, flat);
        if (editing) {
            if (auto error = bc_setup_ui_click(&setup, x, y - 20,
                                               display < 0 ? -1 : display_to_engine(display)))
                alert(error);
            return;
        }
        // READOPTI 0x5812 permits board input after history browsing, even on a Mac side.
        if (player_for_side(settings, play.game.position.side) == 2 || computer.busy() ||
            (play.computer_requested && player_for_side(settings, play.game.position.side) == 1))
            return;
        hint.reset();
        if (play.mate_animation_done || bc_adjudicate(&play.game) == BC_STALEMATE)
            return;
        unsigned previous_side = play.game.position.side;
        play.click(display);
        if (play.game.position.side != previous_side)
            play.computer_requested = player_for_side(settings, play.game.position.side) == 1;
    };
    /* Native coordinator for DOCHECKM (file offset 0x716a). Wait for
     * presentation, animate the captured king, then show the original ending message. */
    /* WAITTOEN 0x3b50: exchange AA before the terminal reset. */
    auto send_ending = [&]() {
        if (modem.connected() && (settings.white_player == 2 || settings.black_player == 2))
            modem.text(std::string("\xaa\x04\xca\x34", 4));
    };
    auto finish_turn = [&]() {
        if (!animation.busy() && !play.pending && !editing && !alert_id && !text_dialog.open &&
            play.promotion.empty() && !play.ending_announced) {
            if (computer_ending) {
                play.ending_announced = true;
                send_ending();
                alert(bc_ending_message(computer_ending));
                computer_ending = 0;
                confirm_action = 3;
                return;
            }
            BCOutcome outcome = bc_adjudicate(&play.game);
            BCMove mate{};
            if (outcome == BC_CHECKMATE && !play.mate_animation_done &&
                bc_animation_checkmate_move(&play.game, &mate)) {
                play.mate_animation_done = true;
                animation.begin(play.game.position, mate, flat);
            } else if (outcome == BC_CHECKMATE || outcome == BC_STALEMATE) {
                play.ending_announced = true;
                send_ending();
                alert(bc_ending_message(outcome == BC_CHECKMATE ? 0x458 : 0x468));
                confirm_action = 3;
            }
        }
    };
    /* Native asynchronous bridge to computer setup (0x5b7c), FINDHINT (0x6794)
     * and RETURNAN (0x5c38). Consume only complete results on the SDL thread. */
    auto update_computer = [&]() {
        if (modem.busy() || editing || file_waiting || alert_id || text_dialog.open ||
            animation.busy() || play.pending || !play.promotion.empty() || play.ending_announced)
            return;
        if (auto result = computer.take_result()) {
            if (!result->search.cancelled && result->search.has_move) {
                if (result->hint) {
                    hint = result->search.move;
                    hint_started = SDL_GetTicks();
                } else {
                    hint.reset();
                    if (!play.apply(result->search.move, true, true))
                        throw std::runtime_error("Original computer returned an illegal move");
                    play.computer_requested =
                        player_for_side(settings, play.game.position.side) == 1;
                    if (settings.white_player == settings.black_player)
                        computer_ending = bc_computer_resignation(&play.game, int(play.past.size()),
                                                                  result->search.score);
                    if (computer_ending) {
                        play.pending.reset();
                        animation.cancel();
                    }
                }
            }
            return;
        }
        if (!computer.busy() && play.computer_requested) {
            play.computer_requested = false;
            play.selected = -1;
            hint.reset();
            computer.start(play.game, play.past, play.book_eligible, settings.level,
                           bc_time_search_seconds(&thinking_time), false);
        }
    };
    /* CNFGETCO 0x2744 and pending-board 0x2d1e: consume only validated native
     * frames; ACK move/promotion immediately, board at its commit boundary. */
    auto update_modem = [&]() {
        bool allowed =
            !editing && !file_waiting && (!alert_id || play.ending_announced) && !text_dialog.open;
        if (auto result = modem.update(
                allowed && (settings.white_player == 2 || settings.black_player == 2))) {
            if (quitting)
                return;
            for (const auto &line : result->messages) {
                /* READBLOC 0x1122c: original strncmp length20, white-first order. */
                if (line.compare(0, 20, "Your Opponent just quit", 20) == 0) {
                    if (settings.white_player == 2)
                        settings.white_player = 0;
                    else if (settings.black_player == 2)
                        settings.black_player = 0;
                    remote_promotion.clear();
                    remote_promotion_piece = 0;
                }
                alert(mac_bitmap_decode_roman(chicago_available ? chicago_glyphs : nullptr, line));
            }
            if (result->status == -2 || (!result->received && result->status < 0)) {
                alert("Serial communication failed.");
                return;
            }
            if (result->received && result->status > 0) {
                auto &bytes = result->bytes;
                if (bytes[0] == 0xa1 && result->status == 37) {
                    Position position{};
                    expand_save_board(&position, bytes.data() + 2);
                    if (bc_setup_validate(&position)) {
                        alert("Invalid remote board.");
                        return;
                    }
                    calculate_piece_lists(&position);
                    computer.cancel();
                    animation.cancel();
                    hint.reset();
                    play = Play();
                    play.book_eligible = false;
                    bc_setup_commit(&play.game, &position);
                    remote_promotion.clear();
                    remote_promotion_piece = 0;
                    play.computer_requested = player_for_side(settings, position.side) == 1;
                    modem.acknowledge();
                } else if (bytes[0] == 0xa2 && result->status == 6) {
                    modem.acknowledge();
                    remote_promotion.clear();
                    remote_promotion_piece = 0;
                    if (player_for_side(settings, play.game.position.side) != 2)
                        return;
                    BCMove moves[80];
                    size_t count = bc_game_legal_moves(&play.game, moves);
                    remote_promotion.clear();
                    for (size_t i = 0; i < count; ++i)
                        if (moves[i].to == bytes[2] && moves[i].from == bytes[3])
                            remote_promotion.push_back(moves[i]);
                    if (remote_promotion.size() == 1) {
                        play.apply(remote_promotion.front());
                        remote_promotion.clear();
                        play.computer_requested =
                            player_for_side(settings, play.game.position.side) == 1;
                    } else if (remote_promotion.empty()) {
                        modem.text("Boards are not synchronized.\r");
                        send_board();
                    }
                } else if (bytes[0] == 0xa9 && result->status == 5) {
                    modem.acknowledge();
                    if (std::any_of(remote_promotion.begin(), remote_promotion.end(),
                                    [&](BCMove move) { return move.piece == bytes[2]; }))
                        remote_promotion_piece = bytes[2];
                }
            }
        }
        if (waiting_for_remote_end &&
            (modem.ended() || (settings.white_player != 2 && settings.black_player != 2))) {
            waiting_for_remote_end = false;
            alert_id = confirm_action = 0;
            reset();
        }
        if (!remote_promotion.empty() && remote_promotion_piece) {
            for (auto move : remote_promotion)
                if (move.piece == remote_promotion_piece) {
                    play.apply(move);
                    play.computer_requested =
                        player_for_side(settings, play.game.position.side) == 1;
                    break;
                }
            remote_promotion.clear();
            remote_promotion_piece = 0;
        }
    };
    int modem_check_stage = 0;
    Uint64 modem_check_deadline = SDL_GetTicks() + 45000;
    if (modem_check) {
        require(modem.connected(), "--modem-check requires --serial DEVICE");
        settings.white_player = settings.black_player = 0;
        action(2, 7);
    }
    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_EVENT_QUIT)
                request_quit();
            if (e.type == SDL_EVENT_WINDOW_FOCUS_LOST)
                dialog_button.cancel();
            if (e.type == file_event) {
                std::unique_ptr<FileResult> result(static_cast<FileResult *>(e.user.data1));
                receive_file(*result);
            }
            if (e.type == SDL_EVENT_MOUSE_MOTION) {
                require(SDL_RenderCoordinatesFromWindow(renderer_ptr, e.motion.x, e.motion.y,
                                                        &mouse_x, &mouse_y),
                        "Cursor conversion");
                track_menu(int(mouse_x), int(mouse_y));
            }
            if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN && e.button.button == SDL_BUTTON_LEFT) {
                float x, y;
                require(
                    SDL_RenderCoordinatesFromWindow(renderer_ptr, e.button.x, e.button.y, &x, &y),
                    "Mouse conversion");
                click(int(x), int(y));
            }
            if (e.type == SDL_EVENT_MOUSE_BUTTON_UP && e.button.button == SDL_BUTTON_LEFT) {
                float x, y;
                require(
                    SDL_RenderCoordinatesFromWindow(renderer_ptr, e.button.x, e.button.y, &x, &y),
                    "Mouse conversion");
                release_pointer(int(x), int(y));
            }
            if (text_dialog.open && e.type == SDL_EVENT_TEXT_INPUT) {
                text_dialog.replace(e.text.text);
                continue;
            }
            if (text_dialog.open && e.type == SDL_EVENT_KEY_DOWN) {
                text_key(e.key.key, e.key.mod);
                continue;
            }
            if (e.type == SDL_EVENT_KEY_DOWN && !e.key.repeat) {
                if (e.key.key == SDLK_ESCAPE) {
                    menu = menu_item = -1;
                    play.selected = -1;
                    if (alert_id == 402)
                        dismiss(2);
                }
                if ((e.key.key == SDLK_RETURN || e.key.key == SDLK_KP_ENTER) && alert_id)
                    dismiss(1);
                if (e.key.mod & (SDL_KMOD_CTRL | SDL_KMOD_GUI)) {
                    if (e.key.key == SDLK_N)
                        action(0, 0);
                    if (e.key.key == SDLK_O)
                        action(0, 1);
                    if (e.key.key == SDLK_S)
                        action(0, 2);
                    if (e.key.key == SDLK_Q)
                        action(0, 6);
                    if (e.key.key == SDLK_F)
                        action(1, 0);
                    if (e.key.key == SDLK_M)
                        action(1, 3);
                    if (e.key.key == SDLK_T)
                        action(6, 3);
                    if (e.key.key == SDLK_B)
                        action(1, 1);
                    if (e.key.key == SDLK_R)
                        action(1, 2);
                }
            }
        }
        update_modem();
        if (quitting && (modem.quit_complete() || SDL_GetTicks() >= quit_deadline))
            running = false;
        if (modem_check) {
            require(SDL_GetTicks() < modem_check_deadline, "Modem integration timeout");
            if (!modem.busy() && modem_check_stage == 0) {
                play.click(52);
                play.click(36);
                require(play.past.size() == 1, "Modem local move");
                modem_check_stage = 1;
            }
            if (modem_check_stage == 1 && play.past.size() == 2 && !modem.busy()) {
                require(play.game.position.board[0x44].piece == 6 &&
                            play.game.position.board[0x34].piece == 6,
                        "Modem remote move");
                BCSaveFile candidate{};
                Position restored{};
                BCSaveSettings restored_settings{};
                require(bc_save_encode(&candidate, &play.game.position, &settings) &&
                            bc_save_decode(&restored, &restored_settings, candidate.bytes,
                                           sizeof candidate.bytes) &&
                            restored_settings.black_player == 2,
                        "Modem save control");
                modem_check_stage = 2;
            }
            if (modem_check_stage == 2 && play.game.position.board[0].piece == 3 &&
                play.game.position.board[0].side == 1 && play.past.size() == 1) {
                require(remote_promotion.empty() && !remote_promotion_piece,
                        "Remote promotion consumed");
                settings.white_player = 2;
                waiting_for_remote_end = true;
                play.ending_announced = true;
                modem_check_stage = 3;
            }
            if (modem_check_stage == 3 && settings.white_player == 0) {
                require(settings.black_player == 2, "Remote quit clears white first");
                modem_check_stage = 4;
                std::cout << "Remote quit white-first passed\n" << std::flush;
            }
            if (modem_check_stage == 4 && settings.black_player == 0) {
                require(!waiting_for_remote_end && play.past.empty() &&
                            play.game.position.board[0x14].piece == 6,
                        "Quit exits terminal wait");
                settings.black_player = 2;
                alert_id = 0;
                animation.cancel();
                play.pending.reset();
                if (modem_quit_window) {
                    SDL_Event quit_event{};
                    quit_event.type = SDL_EVENT_QUIT;
                    require(SDL_PushEvent(&quit_event), "Push native quit test event");
                } else
                    action(0, 6);
                modem_check_stage = 5;
                std::cout << "Native modem PTY integration passed: moves, save control, "
                             "stale/invalid promotion, remote/local quit\n";
            }
            if (modem_check_stage >= 2)
                alert_id = 0;
        }
        sync_animation();
        animation.tick(uint32_t(SDL_GetTicks() * 60 / 1000));
        finish_turn();
        update_computer();
        if (smoke && rendered == 1) { // Exercise the actual click path in both projections.
            for (bool mode : {false, true}) {
                animation.cancel();
                flat = mode;
                play = Play();
                const int *xs = flat ? flat_hit_x : perspective_hit_x;
                const int *ys = flat ? flat_hit_y : perspective_hit_y;
                for (int d : {52, 36}) {
                    int row = d / 8, col = d % 8;
                    click((xs[row * 9 + col] + xs[row * 9 + col + 1]) / 2,
                          20 + (ys[row] + ys[row + 1]) / 2);
                }
                sync_animation();
                animation.finish_for_check();
                if (play.game.position.board[0x34].piece != 6 || play.game.position.side != 1)
                    throw std::runtime_error("Click-path e4 failed");
                action(1, 1);
                if (play.game.position.board[0x14].piece != 6)
                    throw std::runtime_error("Take Back failed");
                action(1, 2);
                if (play.game.position.board[0x34].piece != 6)
                    throw std::runtime_error("Replay failed");
            }
        }
        if (smoke && rendered == 2) {
            play = Play();
            play.game = {};
            play.game.position.opponent = 1;
            insert_piece(&play.game.position, 1, 0, 4);
            insert_piece(&play.game.position, 1, 1, 0x74);
            insert_piece(&play.game.position, 6, 0, 0x60);
            calculate_piece_lists(&play.game.position);
            play.click(engine_to_display(0x60));
            play.click(engine_to_display(0x70));
            if (play.promotion.size() != 4)
                throw std::runtime_error("Promotion chooser failed");
            sync_animation();
            animation.finish_for_check();
            click(200, 150);
            if (play.game.position.board[0x70].piece != 2)
                throw std::runtime_error("Promotion selection failed");
            sync_animation();
            animation.finish_for_check();
            play = Play();
            // Native MenuSelect adapter regression (original call 0x103f6): use event handlers.
            click(menu_x[0], 6);
            assert(menu == 0 && !alert_id);
            track_menu(menu_x[2], 6);
            assert(menu == 2 && menu_item == -1);
            bool sound_before = animation.sound_enabled;
            track_menu(menu_x[2] + 20, 26);
            assert(menu_item == 0 && animation.sound_enabled == sound_before);
            release_pointer(menu_x[2] + 20, 26);
            assert(menu == -1 && animation.sound_enabled != sound_before);
            animation.sound_enabled = sound_before;
            click(menu_x[1], 6);
            track_menu(menu_x[1] + 20, 26 + 18); // Undo unavailable with empty history.
            assert(menu_item == -1 && play.past.empty());
            release_pointer(menu_x[1] + 20, 26 + 18);
            assert(menu == -1 && play.past.empty() && !alert_id);
            click(menu_x[0], 6);
            track_menu(500, 300);
            release_pointer(500, 300);
            assert(menu == -1 && !alert_id);
            click(menu_x[0], 6);
            release_pointer(menu_x[0], 6); // Releasing the heading cancels selection.
            assert(menu == -1 && !alert_id);
            // Exercise the same menu, file-result, setup and ending paths used by the event loop.
            auto temporary =
                fs::temp_directory_path() / ("battlechess-ui-" + std::to_string(SDL_GetTicksNS()));
            require(fs::create_directory(temporary), "Smoke directory");
            struct RemoveTemporary {
                fs::path path;
                // Native test cleanup; no original address. Remove only this check's temporary
                // directory.
                ~RemoveTemporary() {
                    std::error_code error;
                    fs::remove_all(path, error);
                }
            } cleanup{temporary};
            play.click(52);
            play.click(36);
            sync_animation();
            animation.finish_for_check();
            receive_file({2, (temporary / "game").string(), {}});
            assert(!alert_id && fs::file_size(current_file) == 78);
            auto path = current_file;
            action(0, 0);
            assert(alert_id == 402);
            // Native regression for ModalDialog/TrackControl: Down must not commit.
            click(158, 174);
            assert(alert_id == 402 && play.game.position.board[0x34].piece == 6);
            release_pointer(0, 0); // Releasing outside cancels the press, not the dialog.
            assert(alert_id == 402 && play.game.position.board[0x34].piece == 6);
            click(158, 174);
            release_pointer(311, 174); // Dragging OK onto Cancel cannot activate Cancel.
            assert(alert_id == 402 && play.game.position.board[0x34].piece == 6);
            click(311, 174);
            track_menu(0, 0);
            release_pointer(311, 174); // Leave and re-enter the captured button.
            assert(!alert_id && play.game.position.board[0x34].piece == 6);
            action(0, 0);
            dismiss(2);
            assert(play.game.position.board[0x34].piece == 6);
            action(0, 0);
            dismiss(1);
            assert(play.game.position.board[0x14].piece == 6);
            receive_file({1, path.string(), {}});
            assert(!alert_id && play.game.position.board[0x34].piece == 6 && play.past.empty());
            auto before = play.game.position;
            receive_file({1, (temporary / "missing").string(), {}});
            assert(alert_id == 406 &&
                   play.game.position.board[0x34].piece == before.board[0x34].piece);
            dismiss(1);
            action(0, 4);
            assert(editing && flat);
            action(5, 0);
            assert(alert_id == 402);
            dismiss(1);
            action(5, 2);
            assert(editing && alert_id == 406);
            dismiss(1);
            action(5, 1);
            action(5, 2);
            assert(!editing && !alert_id && play.game.position.board[0x34].piece == 6);
            reset();
            for (auto pair : std::initializer_list<std::pair<int, int>>{
                     {0x15, 0x25}, {0x64, 0x44}, {0x16, 0x36}, {0x73, 0x37}}) {
                play.click(engine_to_display(pair.first));
                play.click(engine_to_display(pair.second));
                sync_animation();
                animation.finish_for_check();
            }
            finish_turn();
            assert(play.mate_animation_done);
            animation.finish_for_check();
            finish_turn();
            assert(alert_id == 406 && message == "Check and mate.");
            dismiss(1);
            assert(!play.mate_animation_done && !play.ending_announced && play.past.empty() &&
                   play.game.position.board[0x14].piece == 6);
            reset();
            play.game = {};
            play.game.position.side = 1;
            play.game.position.opponent = 0;
            insert_piece(&play.game.position, 1, 1, 0x70);
            insert_piece(&play.game.position, 1, 0, 0x52);
            insert_piece(&play.game.position, 2, 0, 0x51);
            calculate_piece_lists(&play.game.position);
            finish_turn();
            assert(alert_id == 406 && message == "Stalemate. How boring!");
            dismiss(1);
            reset();
            action(0, 4);
        }
        if (smoke && rendered == 3) {
            action(5, 0);
            assert(alert_id == 402);
            // Native integration: send real queued SDL events through the main dispatcher.
            SDL_Event down{};
            down.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
            down.button.windowID = SDL_GetWindowID(window.get());
            down.button.button = SDL_BUTTON_LEFT;
            down.button.x = 2 * 158;
            down.button.y = 2 * 174;
            require(SDL_PushEvent(&down), "Push dialog press");
            SDL_Event lost{};
            lost.type = SDL_EVENT_WINDOW_FOCUS_LOST;
            lost.window.windowID = down.button.windowID;
            require(SDL_PushEvent(&lost), "Push focus loss");
            down.type = SDL_EVENT_MOUSE_BUTTON_UP;
            require(SDL_PushEvent(&down), "Push cancelled release");
        }
        if (smoke && rendered == 4) {
            assert(alert_id == 402); // Focus loss must cancel the queued OK press.
            dismiss(2);
            action(5, 2);
            action(4, 0);
            assert(alert_id == 400);
            const auto &about = *resource_dialog_find(400, true);
            const auto &button = original_dialog_items[about.first];
            assert(button.type == 4);
            SDL_Event down{};
            down.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
            down.button.windowID = SDL_GetWindowID(window.get());
            down.button.button = SDL_BUTTON_LEFT;
            down.button.x = 2 * about.left + button.left + button.right;
            down.button.y = 2 * about.top + button.top + button.bottom;
            require(SDL_PushEvent(&down), "Push About press");
            down.type = SDL_EVENT_MOUSE_BUTTON_UP;
            require(SDL_PushEvent(&down), "Push About release");
        }
        if (smoke && rendered == 5) {
            assert(!alert_id); // The queued mouse release must close About.
            reset();
            flat = false;
            play.game = {};
            play.game.position.opponent = 1;
            insert_piece(&play.game.position, 1, 0, 4);
            insert_piece(&play.game.position, 1, 1, 0x74);
            insert_piece(&play.game.position, 6, 0, 0x60);
            calculate_piece_lists(&play.game.position);
            play.click(engine_to_display(0x60));
            play.click(engine_to_display(0x70));
            sync_animation();
            animation.finish_for_check();
            assert(play.promotion_walk && play.game.position.board[0x60].piece == 6 &&
                   animation.scene.board[engine_to_display(0x70)]);
        }
        if (smoke && rendered == 6) {
            animation.cancel();
            play = Play();
            for (int level = 0; level <= 9; ++level) {
                action(3, level);
                assert(settings.level == level);
            }
            action(3, 10);
            assert(text_dialog.open && text_dialog.text == "21");
            text_dialog.replace("10001");
            int32_t seconds_before_click = thinking_time.seconds;
            click(200, 168); // DLOG403 item1, independently read from the original DITL.
            assert(text_dialog.open && thinking_time.seconds == seconds_before_click);
            release_pointer(200, 168);
            assert(!text_dialog.open && thinking_time.seconds == 600000 && settings.level == 10);
            action(3, 10);
            text_key(SDLK_BACKSPACE, SDL_KMOD_NONE);
            text_dialog.replace("0");
            text_key(SDLK_RETURN, SDL_KMOD_NONE);
            assert(thinking_time.seconds == 60);
            auto temporary = fs::temp_directory_path() /
                             ("battlechess-time-" + std::to_string(SDL_GetTicksNS()));
            save(temporary);
            action(3, 2);
            receive_file({1, temporary.string(), {}});
            std::error_code error;
            fs::remove(temporary, error);
            current_file.clear();
            assert(text_dialog.open && settings.level == 10 && text_dialog.text == "1");
            text_dialog.replace("12");
            text_key(SDLK_HOME, SDL_KMOD_NONE);
            text_key(SDLK_DELETE, SDL_KMOD_NONE);
            assert(text_dialog.text == "2");
            text_key(SDLK_RETURN, SDL_KMOD_NONE);
            assert(thinking_time.seconds == 120);
            action(3, 10);
        }
        if (smoke && rendered == 7) {
            accept_text_dialog();
            reset();
            action(3, 0);
            auto human_save = fs::temp_directory_path() /
                              ("battlechess-human-" + std::to_string(SDL_GetTicksNS()));
            save(human_save);
            action(2, 3);
            receive_file({1, human_save.string(), {}});
            std::error_code human_save_error;
            fs::remove(human_save, human_save_error);
            current_file.clear();
            assert(settings.white_player == 0 && play.computer_requested && !play.book_eligible);
            action(2, 3);
            auto computer_save = fs::temp_directory_path() /
                                 ("battlechess-computer-" + std::to_string(SDL_GetTicksNS()));
            save(computer_save);
            action(2, 2);
            receive_file({1, computer_save.string(), {}});
            std::error_code computer_save_error;
            fs::remove(computer_save, computer_save_error);
            current_file.clear();
            assert(settings.white_player == 1 && !play.computer_requested && !play.book_eligible);
            action(1, 0);
            assert(settings.white_player == 1 && play.computer_requested);
            update_computer();
            Uint64 deadline = SDL_GetTicks() + 10000;
            while (computer.busy() && SDL_GetTicks() < deadline) {
                update_computer();
                SDL_Delay(1);
            }
            assert(!computer.busy() && play.past.size() == 1 && play.pending);
            sync_animation();
            animation.finish_for_check();
            action(2, 2);
            assert(settings.white_player == 0);
            auto before_hint = play.game;
            action(1, 3);
            deadline = SDL_GetTicks() + 10000;
            while (computer.busy() && SDL_GetTicks() < deadline) {
                update_computer();
                SDL_Delay(1);
            }
            assert(!computer.busy() && hint &&
                   !std::memcmp(&before_hint, &play.game, sizeof(before_hint)));
            action(1, 1);
            assert(!play.computer_requested && !hint);
            action(1, 2);
            assert(!play.computer_requested);
            action(2, 6);
            update_computer();
            action(1, 0);
            deadline = SDL_GetTicks() + 10000;
            while (computer.busy() && SDL_GetTicks() < deadline) {
                update_computer();
                SDL_Delay(1);
            }
            assert(!computer.busy() && play.past.size() == 2 && play.pending);
            sync_animation();
            animation.finish_for_check();
            action(2, 3);
            update_computer();
            action(0, 0);
            assert(alert_id == 402);
            dismiss(1);
            assert(!computer.busy() && play.past.empty() && play.book_eligible);
            action(2, 2);
            action(2, 5);
        }
        set_grayscale(renderer_ptr, 255);
        require(SDL_RenderClear(renderer_ptr), "Clear");
        SDL_FRect src{0, 0, 512, 330}, dst{0, 20, 512, 330};
        require(SDL_RenderTexture(renderer_ptr, flat ? flat_board.get() : perspective.get(), &src,
                                  &dst),
                "Board");
        if (play.selected >= 0)
            draw_selection(renderer_ptr, play.selected, flat);
        /* Original PRINTHIN (0x66d0): source-first, twenty two-tick phases;
         * destination retains the previous pen until its next scheduled draw. */
        if (hint) {
            Uint64 ticks = (SDL_GetTicks() - hint_started) * 60 / 1000;
            auto pens = original_hint_pens(uint32_t(ticks));
            if (!pens.from)
                hint.reset();
            else {
                draw_selection(renderer_ptr, hint->from, flat, pens.from);
                if (pens.to)
                    draw_selection(renderer_ptr, hint->to, flat, pens.to);
            }
        }
        bool original_scene = animation.busy() || play.promotion_walk || play.mate_animation_done ||
                              animation.displays(play.game.position, flat);
        if (original_scene)
            animation.draw(false);
        const Position &shown = editing ? setup.position : play.game.position;
        uint8_t display[64];
        setup_display_board(&shown, display);
        // PLACE / INSERTFO: back ranks first, equal-y sprites newest first.
        if (!original_scene)
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
                    auto &t = (flat ? flat_pieces : standing)[side * count + shape];
                    draw_texture(renderer_ptr, t.get(), (flat ? flat_x : perspective_x)[i] - s.hx,
                                 20 + (flat ? flat_y : perspective_y)[row] - s.hy, s.width,
                                 s.height);
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
                    draw_texture(renderer_ptr, flat_pieces[side * 6 + shape].get(),
                                 (side == 1 ? 30 : 474) - g.hx, 20 + 30 + (piece - 1) * 40 - g.hy,
                                 g.width, g.height);
                }
        if (editing && setup.held_piece) {
            Position sample{};
            insert_piece(&sample, setup.held_piece, setup.held_side, 0);
            uint8_t codes[64];
            setup_display_board(&sample, codes);
            int shape = flat_shapes[codes[56] & 63] - 17;
            auto g = flat_piece_shapes[shape];
            draw_texture(renderer_ptr, flat_pieces[setup.held_side * 6 + shape].get(),
                         mouse_x - g.hx, mouse_y - g.hy, g.width, g.height);
        }
        set_grayscale(renderer_ptr, 255);
        draw_rectangle(renderer_ptr, 0, 0, logical_width, 20);
        set_grayscale(renderer_ptr, 0);
        draw_rectangle(renderer_ptr, 0, 19, logical_width, 1);
        // Native MenuSelect highlight adapter (original call 0x103f6).
        for (int i = 0; i < 7; ++i)
            if (editing ? i == 5 : i != 5 && (i != 6 || modem.connected())) {
                if (i == menu) {
                    set_grayscale(renderer_ptr, 0);
                    draw_rectangle(renderer_ptr, menu_x[i], 0, menu_width[i], 19);
                }
                set_grayscale(renderer_ptr, i == menu ? 255 : 0);
                draw_system_text(renderer_ptr, menu_x[i], chicago_available ? 1 : 6,
                                 menu_for(i).title);
            }
        if (menu >= 0) {
            auto &m = menu_for(menu);
            set_grayscale(renderer_ptr, 255);
            draw_rectangle(renderer_ptr, menu_x[menu], 20, 250, m.count * 18 + 4);
            set_grayscale(renderer_ptr, 0);
            draw_rectangle(renderer_ptr, menu_x[menu], 20, 250, m.count * 18 + 4, false);
            for (int i = 0; i < m.count; ++i) {
                bool selected = i == menu_item && enabled(menu, i);
                if (selected) {
                    set_grayscale(renderer_ptr, 0);
                    draw_rectangle(renderer_ptr, menu_x[menu] + 1, 22 + i * 18, 248, 18);
                }
                set_grayscale(renderer_ptr, selected ? 255 : enabled(menu, i) ? 0 : 150);
                const char *label = m.items[i].label;
                if (std::string(label) == "-") {
                    draw_rectangle(renderer_ptr, menu_x[menu] + 3, 34 + i * 18, 244, 1);
                    continue;
                }
                if (menu == 2 && i == 0)
                    label = animation.sound_enabled ? "Turn Sound Off" : "Turn Sound On";
                if (menu == 2 && i == 1)
                    label = flat ? "Switch to 3D Board" : "Switch to 2D Board";
                if ((menu == 2 &&
                     (i == 2 + settings.white_player || i == 5 + settings.black_player)) ||
                    (menu == 3 && i == settings.level) || (menu == 6 && i == 2 && auto_answer))
                    draw_system_text(renderer_ptr, menu_x[menu] + 4,
                                     (chicago_available ? 22 : 26) + i * 18, "+");
                draw_system_text(renderer_ptr, menu_x[menu] + 16,
                                 (chicago_available ? 22 : 26) + i * 18, label);
                if (m.items[i].shortcut) {
                    char key[] = {char(m.items[i].shortcut), 0};
                    draw_system_text(renderer_ptr, menu_x[menu] + 234,
                                     (chicago_available ? 22 : 26) + i * 18, key);
                }
            }
        }
        if (!play.promotion.empty() && !animation.busy() && !play.pending) {
            require(resource_dialog_draw(renderer_ptr, *resource_dialog_find(408, false), nullptr,
                                         promotion_picture, &promote),
                    "Promotion dialog");
        }
        if (text_dialog.open) {
            const auto &dialog = *resource_dialog_find(text_dialog.resource_id, false);
            require(resource_dialog_draw(renderer_ptr, dialog), "Time dialog");
            const auto &field =
                original_dialog_items[dialog.first + (text_dialog.resource_id == 404 ? 3 : 1)];
            int x = dialog.left + field.left + 4;
            int y = dialog.top + field.top + (chicago_available ? 1 : 4);
            set_grayscale(renderer_ptr, 0);
            draw_system_text(renderer_ptr, x, y, text_dialog.text.c_str());
            size_t first = std::min(text_dialog.cursor, text_dialog.anchor);
            size_t count = std::max(text_dialog.cursor, text_dialog.anchor) - first;
            const int prefix_width =
                system_text_width(std::string_view(text_dialog.text).substr(0, first));
            const int text_height = chicago_available ? chicago_ascent + chicago_descent : 8;
            if (count) {
                const int selected_width =
                    system_text_width(std::string_view(text_dialog.text).substr(first, count));
                draw_rectangle(renderer_ptr, x + prefix_width, y, selected_width, text_height);
                set_grayscale(renderer_ptr, 255);
                draw_system_text(renderer_ptr, x + prefix_width, y,
                                 text_dialog.text.substr(first, count).c_str());
            } else {
                draw_rectangle(renderer_ptr, x + prefix_width, y, 1, text_height);
            }
        }
        if (alert_id) {
            const char *params[] = {message.c_str(), "", "", ""};
            require(
                resource_dialog_draw(renderer_ptr, *resource_dialog_find(alert_id, true), params),
                "Alert dialog");
        }
        if (!screenshot_path.empty() && (rendered == 0 || (smoke && rendered >= 2))) {
            Surface s(SDL_RenderReadPixels(renderer_ptr, nullptr), SDL_DestroySurface);
            require(bool(s), "Screenshot");
            auto output = rendered == 0 ? screenshot_path
                                        : screenshot_path.parent_path() /
                                              (screenshot_path.stem().string() + "-" +
                                               std::to_string(rendered) + ".png");
            require(SDL_SavePNG(s.get(), output.string().c_str()), "Save screenshot");
        }
        require(SDL_RenderPresent(renderer_ptr), "Present");
        if (smoke && ++rendered >= 8)
            running = false;
        SDL_Delay(16);
    }
    if (smoke)
        std::cout << "Native UI passed: both boards, undo/replay, promotion order, save/open, "
                     "setup, mate/stalemate, level/time controls, computer turn and hint\n";
    return 0;
}
/* Native executable entry point; no original address. Report uncaught host
 * failures and return a nonzero status to launchers and automated checks. */
int main(int argc, char **argv) {
    try {
        return run(argc, argv);
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
