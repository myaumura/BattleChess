#include "Application.h"
#include "ApplicationInternal.h"
#include "assets.hpp"
#include <iostream>
#include <stdexcept>

/* Native application host; no single original entry point. Replace Toolbox
 * startup and event dispatch while calling the recovered core and resource UI. */
int runApplication(int argc, char **argv) {
    ApplicationOptions options;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if ((a == "--data" || a == "--screenshot") && i + 1 < argc) {
            if (a == "--data")
                options.data = argv[++i];
            else
                options.screenshotPath = argv[++i];
        } else if (a == "--serial" && i + 1 < argc)
            options.serialPath = argv[++i];
        else if (a == "--animation-check")
            options.animationCheck = true;
        else if (a == "--modem-quit-window")
            options.modemQuitWindow = true;
        else if (a == "--modem-check")
            options.modemCheck = true;
        else if (a == "--smoke")
            options.smoke = true;
        else if (a == "--check-assets")
            options.check = true;
        else if (a == "--flat")
            options.flat = true;
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
    if (options.data.empty())
        throw std::runtime_error("Use run.sh with BC_DATA_DIR or --data PATH");
    const fs::path assets = options.data / "assets";
    require(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO), "SDL initialization");
    struct Quit {
        // Native lifetime guard; no original address. Release SDL even after an exception.
        ~Quit() {
            SDL_Quit();
        }
    } quit;
    // WIND400 (GetNewWindow at file0xffa8) defines the visible board port.
    // The native menu bar remains a separate twenty-pixel SDL adapter above it.
    const int logicalWidth = original_content_width;
    const int logicalHeight = 20 + original_content_height;
    Handle<SDL_Window, SDL_DestroyWindow> window(
        SDL_CreateWindow("Battle Chess", 2 * logicalWidth, 2 * logicalHeight,
                         SDL_WINDOW_RESIZABLE |
                             ((options.smoke || options.animationCheck || options.modemCheck)
                                  ? SDL_WINDOW_HIDDEN
                                  : 0)),
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
    auto *rendererPtr = renderer.get();
    require(SDL_SetRenderLogicalPresentation(rendererPtr, logicalWidth, logicalHeight,
                                             SDL_LOGICAL_PRESENTATION_LETTERBOX),
            "Logical coordinates");
    if (!options.smoke && !options.animationCheck && !options.modemCheck) {
        auto title = loadTexture(rendererPtr, assets / "external_graphics/screens/title.png");
        setGrayscale(rendererPtr, 255);
        require(SDL_RenderClear(rendererPtr), "Title clear");
        drawTexture(rendererPtr, title.get(), 0, 20, 512, 330);
        require(SDL_RenderPresent(rendererPtr), "Title");
    }
    GameTextures textures(rendererPtr, assets, options.check);
    // Original initialized DATA 0xffd14; book, search and fades share one stream.
    uint32_t randomState = 1;
    AnimationHost animation(rendererPtr, assets, options.data / "animation_pixels.bin",
                            randomState);
    animation.fast = options.smoke || options.animationCheck;
    if (options.animationCheck)
        return checkAnimation(animation, rendererPtr, options.screenshotPath);
    Application application(options, window.get(), rendererPtr, textures, animation, randomState);
    return application.run();
}

GameTextures::GameTextures(SDL_Renderer *rendererPtr, const fs::path &assets, bool check) {
    perspective =
        loadTexture(rendererPtr, assets / "external_graphics/screens/perspective_board.png");
    flatBoard = loadTexture(rendererPtr, assets / "external_graphics/screens/flat_board.png");
    standing = loadPieceTextures(rendererPtr, assets / "external_graphics/standing/decoded.2bpp",
                                 standing_shapes, int(std::size(standing_shapes)));
    flatPieces = loadPieceTextures(
        rendererPtr, assets / "external_graphics/flat_pieces/decoded.2bpp", flat_piece_shapes, 6);
    for (int i = 400; i <= 403; ++i)
        promote.push_back(
            loadTexture(rendererPtr, assets / "pictures" / ("PICT_" + std::to_string(i) + ".png")));
    if (check)
        for (auto &f : frames) {
            auto t = loadTexture(rendererPtr, assets / f.path);
            float x, y;
            require(SDL_GetTextureSize(t.get(), &x, &y), "Asset size");
            if (x != f.width || y != f.height)
                throw std::runtime_error("Asset dimensions differ");
        }
}

Application::Application(const ApplicationOptions &options, SDL_Window *window,
                         SDL_Renderer *renderer, GameTextures &textures, AnimationHost &animation,
                         uint32_t &randomState)
    : options(options), window(window), rendererPtr(renderer), textures(textures),
      animation(animation), computer(randomState), modem(options.serialPath),
      blockedCursor(loadCursor(options.data / "assets/raw/System_CURS_4.bin")),
      thinkingCursor(loadCursor(options.data / "assets/raw/CURS_400.bin")),
      checkedCursor(loadCursor(options.data / "assets/raw/CURS_401.bin")), flat(options.flat),
      settings{0, 0, uint8_t(options.smoke ? 0 : 1), 0, 1, 0} {
    require(options.serialPath.empty() || modem.connected(), "Open serial device");
    fileEvent = registerFileDialogEvent();
}

/* Quit 0x2cd4/0x3cbe: both native quit paths send the original modem line. */
void Application::requestQuit() {
    if (!quitting && modem.connected() &&
        (settings.white_player == 2 || settings.black_player == 2)) {
        quitting = true;
        quitDeadline = SDL_GetTicks() + 2000;
        modem.quit();
    } else if (!quitting)
        running = false;
}

/* Native SDL dispatcher; consume events on the host thread in their original order. */
void Application::handleEvent(const SDL_Event &e) {
    if (e.type == SDL_EVENT_QUIT)
        requestQuit();
    if (e.type == SDL_EVENT_WINDOW_FOCUS_LOST)
        dialogButton.cancel();
    if (e.type == SDL_EVENT_WINDOW_MOUSE_LEAVE)
        mouseX = mouseY = -1;
    if (e.type == SDL_EVENT_WINDOW_MOUSE_ENTER) {
        float x, y;
        SDL_GetMouseState(&x, &y);
        require(SDL_RenderCoordinatesFromWindow(rendererPtr, x, y, &mouseX, &mouseY),
                "Cursor conversion");
    }
    if (e.type == fileEvent) {
        std::unique_ptr<FileResult> result(static_cast<FileResult *>(e.user.data1));
        receiveFileResult(*result);
    }
    if (e.type == SDL_EVENT_MOUSE_MOTION) {
        require(
            SDL_RenderCoordinatesFromWindow(rendererPtr, e.motion.x, e.motion.y, &mouseX, &mouseY),
            "Cursor conversion");
        trackMenu(int(mouseX), int(mouseY));
    }
    if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN && e.button.button == SDL_BUTTON_LEFT) {
        float x, y;
        require(SDL_RenderCoordinatesFromWindow(rendererPtr, e.button.x, e.button.y, &x, &y),
                "Mouse conversion");
        mouseX = x;
        mouseY = y;
        handlePointerPress(int(x), int(y));
    }
    if (e.type == SDL_EVENT_MOUSE_BUTTON_UP && e.button.button == SDL_BUTTON_LEFT) {
        float x, y;
        require(SDL_RenderCoordinatesFromWindow(rendererPtr, e.button.x, e.button.y, &x, &y),
                "Mouse conversion");
        mouseX = x;
        mouseY = y;
        handlePointerRelease(int(x), int(y));
    }
    if (textDialog.open && e.type == SDL_EVENT_TEXT_INPUT) {
        textDialog.replace(e.text.text);
        return;
    }
    if (textDialog.open && e.type == SDL_EVENT_KEY_DOWN) {
        handleTextKey(e.key.key, e.key.mod);
        return;
    }
    if (e.type == SDL_EVENT_KEY_DOWN && !e.key.repeat) {
        if (e.key.key == SDLK_ESCAPE) {
            menu = menuItem = speedMenuItem = -1;
            session.selected = -1;
            if (alertId == 402)
                dismissAlert(2);
        }
        if ((e.key.key == SDLK_RETURN || e.key.key == SDLK_KP_ENTER) && alertId)
            dismissAlert(1);
        if (e.key.mod & (SDL_KMOD_CTRL | SDL_KMOD_GUI)) {
            if (e.key.key == SDLK_N)
                performMenuAction(0, 0);
            if (e.key.key == SDLK_O)
                performMenuAction(0, 1);
            if (e.key.key == SDLK_S)
                performMenuAction(0, 2);
            if (e.key.key == SDLK_Q)
                performMenuAction(0, 6);
            if (e.key.key == SDLK_F)
                performMenuAction(1, 0);
            if (e.key.key == SDLK_M)
                performMenuAction(1, 3);
            if (e.key.key == SDLK_T)
                performMenuAction(6, 3);
            if (e.key.key == SDLK_B)
                performMenuAction(1, 1);
            if (e.key.key == SDLK_R)
                performMenuAction(1, 2);
        }
    }
}

int Application::run() {
    modemCheckDeadline = SDL_GetTicks() + 45000;
    if (options.modemCheck) {
        require(modem.connected(), "--modem-check requires --serial DEVICE");
        settings.white_player = settings.black_player = 0;
        performMenuAction(2, 7);
    }
    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event))
            handleEvent(event);
        updateModem();
        if (quitting && (modem.quitComplete() || SDL_GetTicks() >= quitDeadline))
            running = false;
        if (options.modemCheck)
            checkModem();
        syncAnimation();
        animation.tick(uint32_t(SDL_GetTicks() * 60 / 1000));
        finishTurn();
        updateComputer();
        if (options.smoke)
            checkSmoke();
        render();
        if (options.smoke && ++rendered >= 8)
            running = false;
        SDL_Delay(16);
    }
    if (options.smoke)
        std::cout << "Native UI passed: both boards, undo/replay, promotion order, save/open, "
                     "setup, mate/stalemate, level/time controls, computer turn and hint\n";
    return 0;
}
