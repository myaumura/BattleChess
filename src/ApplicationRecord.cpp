#include "ApplicationInternal.h"
#include "GifWriter.h"
#include "PgnExport.h"
#include "adjudication.h"
#include "animation_plan.h"
#include "MacBitmapFont.h"
#include "presentation.h"
#include <algorithm>

namespace
{
    constexpr int kGifScale = 2;
    // Playback runs at twice the original speed; every recorded frame is kept.
    constexpr double kGifSpeed = 2;
    // Original timings in hundredths of a second, before kGifSpeed is applied.
    // One recovered animation step is six 60 Hz ticks (HANDLEAL's gate at 1x).
    constexpr double kStepDelay = 10;
    // Original fade phases last ten ticks each (presentFadeFrame waits 167 ms).
    constexpr double kFadeDelay = 50.0 / 3;
    // Pauses are already in output time and are not scaled.
    constexpr int kOpeningHold = 80, kMoveHold = 30, kFinalHold = 300;
    // Browsers replace GIF delays below two hundredths with a slow default.
    constexpr int kMinDelay = 2;
    constexpr unsigned kMaxStepsPerMove = 20000;
    // Refresh the progress dialog at most this often, in milliseconds.
    constexpr Uint64 kProgressInterval = 50;
    constexpr int kProgressWidth = 260, kProgressHeight = 76, kBarWidth = 228, kBarHeight = 12;
}

/* Native GIF picker; no original entry point. Suggest the saved game's name. */
void Application::openGifDialog()
{
    fileWaiting = true;
    fs::path suggestion = currentFile.empty()
                              ? fs::path("Battle Chess.gif")
                              : fs::path(currentFile).replace_extension(".gif");
    showGifExportDialog(window, suggestion.string());
}

/* Native GIF export; no original entry point. Replay this session's moves offscreen
 * through the recovered movement, combat, fade and checkmate animations at twice the
 * original speed, always on the 3D board whatever the current view. The live board, RNG
 * stream, fade masks and session are left as they were; the export blocks input while
 * it runs and shows a progress dialog over a snapshot of the window. */
bool Application::exportGifFile(fs::path path)
{
    if (path.extension().empty())
        path += ".gif";
    std::vector<BCGame> games = session.past;
    games.push_back(session.game);
    std::vector<BCMove> moves;
    for (size_t i = 0; i + 1 < games.size(); ++i)
    {
        BCMove move;
        if (!recoverMove(games[i], games[i + 1], move))
        {
            showAlert("Unable to export GIF.");
            return false;
        }
        moves.push_back(move);
    }
    if (moves.empty())
    {
        showAlert("Play at least one move before exporting a GIF.");
        return false;
    }

    const int width = logicalWidth, height = original_content_height;
    Texture target(SDL_CreateTexture(rendererPtr, SDL_PIXELFORMAT_RGBA32,
                                     SDL_TEXTUREACCESS_TARGET, width, 20 + height),
                   SDL_DestroyTexture);
    GifWriter gif;
    if (!target || !gif.open(path, width * kGifScale, height * kGifScale))
    {
        showAlert("Unable to export GIF.");
        return false;
    }

    // Restore everything the recording borrows, even if a renderer call throws.
    uint32_t& randomState = animation.sharedRandomState();
    struct Restore
    {
        Application& app;
        SDL_Texture* previousTarget;
        uint32_t& randomState;
        uint32_t savedRandomState;
        std::array<uint16_t, 16> savedFadeMasks;
        bool savedFast;

        ~Restore()
        {
            app.animation.cancel();
            app.animation.fadeFrame = nullptr;
            app.animation.fast = savedFast;
            randomState = savedRandomState;
            app.animation.restoreFadeMaskOrder(savedFadeMasks);
            SDL_SetRenderTarget(app.rendererPtr, previousTarget);
        }
    } restore{
        *this, SDL_GetRenderTarget(rendererPtr), randomState, randomState,
        animation.fadeMaskOrder(), animation.fast
    };

    animation.cancel();

    // Snapshot the window once; progress updates redraw it with a dialog on top, since the
    // animation host is busy with the recording and cannot draw the live board meanwhile.
    applyLayout(false);
    const int windowWidth = viewWidth(), windowHeight = viewHeight();
    Texture backdrop(SDL_CreateTexture(rendererPtr, SDL_PIXELFORMAT_RGBA32,
                                       SDL_TEXTUREACCESS_TARGET, windowWidth, windowHeight),
                     SDL_DestroyTexture);
    require(bool(backdrop) && SDL_SetRenderTarget(rendererPtr, backdrop.get()), "GIF backdrop");
    renderBoard();
    renderBelowBoard();
    renderMenus();
    BCMove mate{};
    const bool mated = bc_adjudicate(&session.game) == BC_CHECKMATE &&
        bc_animation_checkmate_move(&session.game, &mate);
    const size_t totalMoves = moves.size() + (mated ? 1 : 0);
    Uint64 lastProgress = 0;
    gifProgressFrames = 0;
    // Native progress dialog; no original entry point. Drawn in the window's Mac style.
    auto showProgress = [&](size_t movesDone, bool force)
    {
        Uint64 now = SDL_GetTicks();
        if (!force && gifProgressFrames && now - lastProgress < kProgressInterval)
            return;
        lastProgress = now;
        require(SDL_SetRenderTarget(rendererPtr, restore.previousTarget), "Progress target");
        drawTexture(rendererPtr, backdrop.get(), 0, 0, float(windowWidth), float(windowHeight));
        const int left = (windowWidth - kProgressWidth) / 2;
        const int top = 20 + (height - kProgressHeight) / 2;
        setGrayscale(rendererPtr, 0);
        drawRectangle(rendererPtr, left, top, kProgressWidth, kProgressHeight);
        setGrayscale(rendererPtr, 255);
        drawRectangle(rendererPtr, left + 1, top + 1, kProgressWidth - 2, kProgressHeight - 2);
        setGrayscale(rendererPtr, 0);
        drawRectangle(rendererPtr, left + 3, top + 3, kProgressWidth - 6, kProgressHeight - 6,
                      false);
        drawRectangle(rendererPtr, left + 4, top + 4, kProgressWidth - 8, kProgressHeight - 8,
                      false);
        const int textOffset = chicago_available ? 0 : 4;
        const int barLeft = left + (kProgressWidth - kBarWidth) / 2;
        drawSystemText(rendererPtr, barLeft, top + 10 + textOffset, "Exporting GIF...");
        const int barTop = top + 32;
        drawRectangle(rendererPtr, barLeft, barTop, kBarWidth, kBarHeight, false);
        const int filled = int((kBarWidth - 2) * movesDone / totalMoves);
        drawRectangle(rendererPtr, barLeft + 1, barTop + 1, filled, kBarHeight - 2);
        std::string status = movesDone < totalMoves
                                 ? "Move " + std::to_string(movesDone + 1) + " of " +
                                 std::to_string(totalMoves)
                                 : std::string("Finishing...");
        drawSystemText(rendererPtr, barLeft, barTop + 16 + textOffset, status.c_str());
        // Kept beside the smoke screenshot for manual inspection of the dialog.
        if (options.smoke && !options.screenshotPath.empty() && movesDone * 2 >= totalMoves &&
            !gifProgressCaptured)
        {
            Surface image(SDL_RenderReadPixels(rendererPtr, nullptr), SDL_DestroySurface);
            require(bool(image), "Progress screenshot");
            auto shot = options.screenshotPath.parent_path() / "gif-export-progress.png";
            require(SDL_SavePNG(image.get(), shot.string().c_str()), "Progress screenshot save");
            gifProgressCaptured = true;
        }
        require(SDL_RenderPresent(rendererPtr), "Progress present");
        ++gifProgressFrames;
        require(SDL_SetRenderTarget(rendererPtr, target.get()), "GIF target");
    };

    animation.fast = true; // One runtime step per tick call, and no sound.
    require(SDL_SetRenderTarget(rendererPtr, target.get()), "GIF target");
    size_t movesDone = 0;
    showProgress(movesDone, true);
    std::vector<uint8_t> gray(size_t(width * kGifScale * height * kGifScale));
    // Carry fractional hundredths so the scaled delays keep the overall timing exact.
    double owed = 0;
    auto scaled = [&](double originalDelay)
    {
        owed += originalDelay / kGifSpeed;
        int delay = std::max(kMinDelay, int(owed));
        owed -= delay;
        return delay;
    };
    auto capture = [&](int delay)
    {
        setGrayscale(rendererPtr, 255);
        require(SDL_RenderClear(rendererPtr), "GIF clear");
        animation.draw(true);
        SDL_Rect content{0, 20, width, height};
        Surface pixels(SDL_RenderReadPixels(rendererPtr, &content), SDL_DestroySurface);
        require(bool(pixels), "GIF read pixels");
        Surface rgba(SDL_ConvertSurface(pixels.get(), SDL_PIXELFORMAT_RGBA32), SDL_DestroySurface);
        require(bool(rgba), "GIF pixel format");
        for (int y = 0; y < height * kGifScale; ++y)
        {
            const auto* row = static_cast<const uint8_t*>(rgba->pixels) +
                (y / kGifScale) * rgba->pitch;
            for (int x = 0; x < width * kGifScale; ++x)
            {
                const uint8_t* p = row + 4 * (x / kGifScale);
                gray[size_t(y * width * kGifScale + x)] =
                    uint8_t((p[0] * 299 + p[1] * 587 + p[2] * 114) / 1000);
            }
        }
        gif.addFrame(gray, delay);
        SDL_PumpEvents(); // Keep the window responsive to the system during long exports.
        showProgress(movesDone, false);
    };
    animation.fadeFrame = [&] { capture(scaled(kFadeDelay)); };
    auto play = [&](const Position& before, BCMove move, int hold)
    {
        animation.begin(before, move, false); // Always the 3D board.
        capture(hold);
        for (unsigned step = 0; animation.busy(); ++step)
        {
            if (step == kMaxStepsPerMove)
                throw std::runtime_error("GIF animation did not terminate");
            animation.tick(animation.scene.previous_ticks + 6);
            capture(scaled(kStepDelay));
        }
    };

    for (size_t i = 0; i < moves.size(); ++i, ++movesDone)
        play(games[i].position, moves[i], i ? kMoveHold : kOpeningHold);
    if (mated)
    {
        play(session.game.position, mate, kMoveHold);
        ++movesDone;
    }
    capture(kFinalHold);
    showProgress(movesDone, true);
    bool closed = gif.close();
    lastGifFrames = gif.framesWritten();
    if (!closed)
    {
        std::error_code error;
        fs::remove(path, error);
        showAlert("Unable to export GIF.");
        return false;
    }
    return true;
}
