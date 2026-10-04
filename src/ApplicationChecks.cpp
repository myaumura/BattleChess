#include "ApplicationInternal.h"
#include "presentation.h"
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <stdexcept>

namespace {

constexpr unsigned kMaxAnimationTicks = 20000;
constexpr Uint64 kComputerCheckTimeoutMs = 10000;

/* Native animation probes retain one position and case counter across the
 * original sequence of movement, capture, turn and special-move scenarios. */
class AnimationChecks {
  public:
    AnimationChecks(AnimationHost &animation, SDL_Renderer *rendererPtr,
                    const fs::path &screenshotPath)
        : animation(animation), rendererPtr(rendererPtr), screenshotPath(screenshotPath) {
    }

    unsigned run() {
        checkPawnMovement();
        checkCapturePairs();
        checkPieceDirections();
        checkKnightMoves();
        checkSpecialMoves();
        return cases;
    }

  private:
    AnimationHost &animation;
    SDL_Renderer *rendererPtr;
    const fs::path &screenshotPath;
    unsigned cases = 0;
    Position position{};

    void exerciseMove(const Position &position, BCMove move, bool flat);
    void checkPawnMovement();
    void checkCapturePairs();
    void checkPieceDirections();
    void checkKnightMoves();
    void checkSpecialMoves();
};

/* Native integration check; no original address. Drive a real recovered
 * animation to completion and reject stalled graphs or a lost mover. */
void AnimationChecks::exerciseMove(const Position &position, BCMove move, bool flat) {
    animation.begin(position, move, flat);
    unsigned ticks = 0;
    while (animation.busy() && ticks++ < kMaxAnimationTicks) {
        animation.tick(animation.scene.previous_ticks + 6);
        if (ticks % 7 == 0)
            animation.draw();
        if (!screenshotPath.empty() && move.piece == 6 && move.captured == 6 && ticks == 70) {
            animation.draw();
            Surface image(SDL_RenderReadPixels(rendererPtr, nullptr), SDL_DestroySurface);
            require(bool(image), "Animation screenshot");
            require(SDL_SavePNG(image.get(), screenshotPath.string().c_str()),
                    "Animation screenshot save");
        }
    }
    if (animation.busy())
        throw std::runtime_error(
            "Animation integration timeout case=" + std::to_string(cases) +
            " piece=" + std::to_string(move.piece) + " victim=" + std::to_string(move.captured) +
            " from=" + std::to_string(move.from) + " to=" + std::to_string(move.to) +
            " head=" + std::to_string(animation.scene.head) +
            " type=" + std::to_string(animation.scene.active[animation.scene.head].type));
    int destination = engine_to_display(move.to);
    if (!animation.scene.board[destination])
        throw std::runtime_error("Animation lost moving piece");
    ++cases;
}

void AnimationChecks::checkPawnMovement() {
    reset_board(&position);
    exerciseMove(position, {0x34, 0x14, 0, 6, 0}, false);
    exerciseMove(position, {0x34, 0x14, 0, 6, 0}, true);
}

void AnimationChecks::checkCapturePairs() {
    for (unsigned side = 0; side < 2; ++side)
        for (unsigned piece = 1; piece <= 6; ++piece)
            for (unsigned victim = 1; victim <= 6; ++victim) {
                if (piece == 1 && victim == 1)
                    continue;
                uint16_t to = piece == 4 || piece == 6 ? (side ? 0x22 : 0x44)
                              : piece == 5             ? 0x54
                              : piece == 1             ? 0x34
                                                       : 0x37;
                position = {};
                insert_piece(&position, piece, side, 0x33);
                insert_piece(&position, victim, 1 - side, to);
                exerciseMove(position, {to, 0x33, 0, uint8_t(piece), uint8_t(victim)}, false);
            }
}

void AnimationChecks::checkPieceDirections() {
    for (unsigned piece = 1; piece <= 6; ++piece)
        for (int to : {0x22, 0x23, 0x24, 0x32, 0x34, 0x42, 0x43, 0x44, 0x12, 0x14, 0x21, 0x25, 0x41,
                       0x45, 0x52, 0x54}) {
            int dx = std::abs((to & 7) - 3), dy = std::abs((to >> 4) - 3);
            if ((piece == 1 && (dx > 1 || dy > 1)) || (piece == 3 && dx && dy) ||
                (piece == 4 && dx != dy) || (piece == 5 && dx * dy != 2) ||
                (piece == 6 && to != 0x43) || (piece == 2 && dx && dy && dx != dy))
                continue;
            position = {};
            insert_piece(&position, piece, 0, 0x33);
            exerciseMove(position, {uint16_t(to), 0x33, 0, uint8_t(piece), 0}, false);
        }
}

void AnimationChecks::checkKnightMoves() {
    reset_board(&position);
    for (BCMove move : std::initializer_list<BCMove>{{0x20, 1, 0, 5, 0},
                                                     {0x22, 1, 0, 5, 0},
                                                     {0x25, 6, 0, 5, 0},
                                                     {0x27, 6, 0, 5, 0},
                                                     {0x50, 0x71, 0, 5, 0},
                                                     {0x52, 0x71, 0, 5, 0}})
        exerciseMove(position, move, false);
}

void AnimationChecks::checkSpecialMoves() {
    position = {};
    insert_piece(&position, 1, 0, 4);
    insert_piece(&position, 3, 0, 7);
    exerciseMove(position, {6, 4, 1, 1, 0}, false);
    position = {};
    insert_piece(&position, 6, 0, 0x44);
    insert_piece(&position, 6, 1, 0x43);
    exerciseMove(position, {0x53, 0x44, 1, 6, 0}, false);
}

} // namespace

int checkAnimation(AnimationHost &animation, SDL_Renderer *rendererPtr,
                   const fs::path &screenshotPath) {
    AnimationChecks checks(animation, rendererPtr, screenshotPath);
    const unsigned cases = checks.run();
    std::cout << "Animation integration: " << cases
              << " move/capture/turn/special cases terminated and rendered\n";
    return 0;
}

void Application::checkModem() {
    require(SDL_GetTicks() < modemCheckDeadline, "Modem integration timeout");
    // Preserve consecutive transitions within one event-loop iteration.
    checkModemLocalMove();
    checkModemRemoteMove();
    checkModemPromotion();
    checkModemWhiteQuit();
    checkModemBlackAndLocalQuit();
    if (modemCheckStage >= ModemCheckStage::remotePromotion)
        alertId = 0;
}

void Application::checkModemLocalMove() {
    if (!modem.busy() && modemCheckStage == ModemCheckStage::localMove) {
        session.click(52);
        session.click(36);
        require(session.past.size() == 1, "Modem local move");
        modemCheckStage = ModemCheckStage::remoteMove;
    }
}

void Application::checkModemRemoteMove() {
    if (modemCheckStage == ModemCheckStage::remoteMove && session.past.size() == 2 &&
        !modem.busy()) {
        require(session.game.position.board[0x44].piece == 6 &&
                    session.game.position.board[0x34].piece == 6,
                "Modem remote move");
        BCSaveFile candidate{};
        Position restored{};
        BCSaveSettings restoredSettings{};
        require(bc_save_encode(&candidate, &session.game.position, &settings) &&
                    bc_save_decode(&restored, &restoredSettings, candidate.bytes,
                                   sizeof candidate.bytes) &&
                    restoredSettings.black_player == 2,
                "Modem save control");
        modemCheckStage = ModemCheckStage::remotePromotion;
    }
}

void Application::checkModemPromotion() {
    if (modemCheckStage == ModemCheckStage::remotePromotion &&
        session.game.position.board[0].piece == 3 && session.game.position.board[0].side == 1 &&
        session.past.size() == 1) {
        require(remotePromotion.empty() && !remotePromotionPiece, "Remote promotion consumed");
        settings.white_player = 2;
        waitingForRemoteEnd = true;
        session.endingAnnounced = true;
        modemCheckStage = ModemCheckStage::whiteQuit;
    }
}

void Application::checkModemWhiteQuit() {
    if (modemCheckStage == ModemCheckStage::whiteQuit && settings.white_player == 0) {
        require(settings.black_player == 2, "Remote quit clears white first");
        modemCheckStage = ModemCheckStage::blackQuit;
        std::cout << "Remote quit white-first passed\n" << std::flush;
    }
}

void Application::checkModemBlackAndLocalQuit() {
    if (modemCheckStage == ModemCheckStage::blackQuit && settings.black_player == 0) {
        require(!waitingForRemoteEnd && session.past.empty() &&
                    session.game.position.board[0x14].piece == 6,
                "Quit exits terminal wait");
        settings.black_player = 2;
        alertId = 0;
        animation.cancel();
        session.pending.reset();
        if (options.modemQuitWindow) {
            SDL_Event quitEvent{};
            quitEvent.type = SDL_EVENT_QUIT;
            require(SDL_PushEvent(&quitEvent), "Push native quit test event");
        } else
            performMenuAction(0, 6);
        modemCheckStage = ModemCheckStage::localQuit;
        std::cout << "Native modem PTY integration passed: moves, save control, "
                     "stale/invalid promotion, remote/local quit\n";
    }
}

void Application::checkSmoke() {
    if (!options.smoke)
        return;
    switch (rendered) {
    case 1:
        checkBoardInteraction();
        break;
    case 2:
        checkPromotionSelection();
        checkMenuTracking();
        checkGameDialogs();
        break;
    case 3:
        queueFocusLossEvents();
        break;
    case 4:
        checkFocusLossAndQueueAboutEvents();
        break;
    case 5:
        checkAboutAndPromotionWalk();
        break;
    case 6:
        checkThinkingTime();
        break;
    case 7:
        checkComputerPlay();
        break;
    }
}

void Application::checkBoardInteraction() {
    // Exercise the actual click path in both projections.
    for (bool mode : {false, true}) {
        animation.cancel();
        flat = mode;
        session = GameSession();
        const int *xs = flat ? flat_hit_x : perspective_hit_x;
        const int *ys = flat ? flat_hit_y : perspective_hit_y;
        for (int d : {52, 36}) {
            int row = d / 8, col = d % 8;
            handlePointerPress((xs[row * 9 + col] + xs[row * 9 + col + 1]) / 2,
                               20 + (ys[row] + ys[row + 1]) / 2);
        }
        syncAnimation();
        animation.finish_for_check();
        if (session.game.position.board[0x34].piece != 6 || session.game.position.side != 1)
            throw std::runtime_error("Click-path e4 failed");
        performMenuAction(1, 1);
        if (session.game.position.board[0x14].piece != 6)
            throw std::runtime_error("Take Back failed");
        performMenuAction(1, 2);
        if (session.game.position.board[0x34].piece != 6)
            throw std::runtime_error("Replay failed");
    }
}

void Application::preparePromotionPosition() {
    session.game = {};
    session.game.position.opponent = 1;
    insert_piece(&session.game.position, 1, 0, 4);
    insert_piece(&session.game.position, 1, 1, 0x74);
    insert_piece(&session.game.position, 6, 0, 0x60);
    calculate_piece_lists(&session.game.position);
}

void Application::checkPromotionSelection() {
    session = GameSession();
    preparePromotionPosition();
    session.click(engine_to_display(0x60));
    session.click(engine_to_display(0x70));
    if (session.promotion.size() != 4)
        throw std::runtime_error("Promotion chooser failed");
    syncAnimation();
    animation.finish_for_check();
    handlePointerPress(200, 150);
    if (session.game.position.board[0x70].piece != 2)
        throw std::runtime_error("Promotion selection failed");
    syncAnimation();
    animation.finish_for_check();
    session = GameSession();
}

void Application::checkMenuTracking() {
    // Native MenuSelect adapter regression (original call 0x103f6): use event handlers.
    handlePointerPress(menuX[0], 6);
    assert(menu == 0 && !alertId);
    trackMenu(menuX[2], 6);
    assert(menu == 2 && menuItem == -1);
    bool soundBefore = animation.sound_enabled;
    trackMenu(menuX[2] + 20, 26);
    assert(menuItem == 0 && animation.sound_enabled == soundBefore);
    handlePointerRelease(menuX[2] + 20, 26);
    assert(menu == -1 && animation.sound_enabled != soundBefore);
    animation.sound_enabled = soundBefore;
    handlePointerPress(menuX[1], 6);
    trackMenu(menuX[1] + 20, 26 + 18); // Undo unavailable with empty history.
    assert(menuItem == -1 && session.past.empty());
    handlePointerRelease(menuX[1] + 20, 26 + 18);
    assert(menu == -1 && session.past.empty() && !alertId);
    handlePointerPress(menuX[0], 6);
    trackMenu(500, 300);
    handlePointerRelease(500, 300);
    assert(menu == -1 && !alertId);
    handlePointerPress(menuX[0], 6);
    handlePointerRelease(menuX[0], 6); // Releasing the heading cancels selection.
    assert(menu == -1 && !alertId);
}

void Application::checkGameDialogs() {
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

    checkSavedGameDialogs(temporary);
    checkBoardSetup();
    checkCheckmateReset();
    checkStalemateReset();
    resetGame();
    performMenuAction(0, 4);
}

void Application::checkSavedGameDialogs(const fs::path &temporary) {
    session.click(52);
    session.click(36);
    syncAnimation();
    animation.finish_for_check();
    receiveFileResult({2, (temporary / "game").string(), {}});
    assert(!alertId && fs::file_size(currentFile) == 78);
    auto path = currentFile;
    checkNewGameConfirmation();
    receiveFileResult({1, path.string(), {}});
    assert(!alertId && session.game.position.board[0x34].piece == 6 && session.past.empty());
    auto before = session.game.position;
    receiveFileResult({1, (temporary / "missing").string(), {}});
    assert(alertId == 406 && session.game.position.board[0x34].piece == before.board[0x34].piece);
    dismissAlert(1);
}

void Application::checkNewGameConfirmation() {
    performMenuAction(0, 0);
    assert(alertId == 402);
    // Native regression for ModalDialog/TrackControl: Down must not commit.
    handlePointerPress(158, 174);
    assert(alertId == 402 && session.game.position.board[0x34].piece == 6);
    handlePointerRelease(0, 0); // Releasing outside cancels the press, not the dialog.
    assert(alertId == 402 && session.game.position.board[0x34].piece == 6);
    handlePointerPress(158, 174);
    handlePointerRelease(311, 174); // Dragging OK onto Cancel cannot activate Cancel.
    assert(alertId == 402 && session.game.position.board[0x34].piece == 6);
    handlePointerPress(311, 174);
    trackMenu(0, 0);
    handlePointerRelease(311, 174); // Leave and re-enter the captured button.
    assert(!alertId && session.game.position.board[0x34].piece == 6);
    performMenuAction(0, 0);
    dismissAlert(2);
    assert(session.game.position.board[0x34].piece == 6);
    performMenuAction(0, 0);
    dismissAlert(1);
    assert(session.game.position.board[0x14].piece == 6);
}

void Application::checkBoardSetup() {
    performMenuAction(0, 4);
    assert(editing && flat);
    performMenuAction(5, 0);
    assert(alertId == 402);
    dismissAlert(1);
    performMenuAction(5, 2);
    assert(editing && alertId == 406);
    dismissAlert(1);
    performMenuAction(5, 1);
    performMenuAction(5, 2);
    assert(!editing && !alertId && session.game.position.board[0x34].piece == 6);
}

void Application::checkCheckmateReset() {
    resetGame();
    for (auto pair : std::initializer_list<std::pair<int, int>>{
             {0x15, 0x25}, {0x64, 0x44}, {0x16, 0x36}, {0x73, 0x37}}) {
        session.click(engine_to_display(pair.first));
        session.click(engine_to_display(pair.second));
        syncAnimation();
        animation.finish_for_check();
    }
    finishTurn();
    assert(session.mateAnimationDone);
    animation.finish_for_check();
    finishTurn();
    assert(alertId == 406 && message == "Check and mate.");
    dismissAlert(1);
    assert(!session.mateAnimationDone && !session.endingAnnounced && session.past.empty() &&
           session.game.position.board[0x14].piece == 6);
}

void Application::checkStalemateReset() {
    resetGame();
    session.game = {};
    session.game.position.side = 1;
    session.game.position.opponent = 0;
    insert_piece(&session.game.position, 1, 1, 0x70);
    insert_piece(&session.game.position, 1, 0, 0x52);
    insert_piece(&session.game.position, 2, 0, 0x51);
    calculate_piece_lists(&session.game.position);
    finishTurn();
    assert(alertId == 406 && message == "Stalemate. How boring!");
    dismissAlert(1);
}

void Application::queueFocusLossEvents() {
    performMenuAction(5, 0);
    assert(alertId == 402);
    // Native integration: send real queued SDL events through the main dispatcher.
    SDL_Event down{};
    down.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
    down.button.windowID = SDL_GetWindowID(window);
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

void Application::checkFocusLossAndQueueAboutEvents() {
    assert(alertId == 402); // Focus loss must cancel the queued OK press.
    dismissAlert(2);
    performMenuAction(5, 2);
    performMenuAction(4, 0);
    assert(alertId == 400);
    const auto &about = *resource_dialog_find(400, true);
    const auto &button = original_dialog_items[about.first];
    assert(button.type == 4);
    SDL_Event down{};
    down.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
    down.button.windowID = SDL_GetWindowID(window);
    down.button.button = SDL_BUTTON_LEFT;
    down.button.x = 2 * about.left + button.left + button.right;
    down.button.y = 2 * about.top + button.top + button.bottom;
    require(SDL_PushEvent(&down), "Push About press");
    down.type = SDL_EVENT_MOUSE_BUTTON_UP;
    require(SDL_PushEvent(&down), "Push About release");
}

void Application::checkAboutAndPromotionWalk() {
    assert(!alertId); // The queued mouse release must close About.
    resetGame();
    flat = false;
    preparePromotionPosition();
    session.click(engine_to_display(0x60));
    session.click(engine_to_display(0x70));
    syncAnimation();
    animation.finish_for_check();
    assert(session.promotionWalk && session.game.position.board[0x60].piece == 6 &&
           animation.scene.board[engine_to_display(0x70)]);
}

void Application::checkThinkingTime() {
    animation.cancel();
    session = GameSession();
    for (int level = 0; level <= 9; ++level) {
        performMenuAction(3, level);
        assert(settings.level == level);
    }
    performMenuAction(3, 10);
    assert(textDialog.open && textDialog.text == "21");
    textDialog.replace("10001");
    int32_t secondsBeforeClick = thinkingTime.seconds;
    handlePointerPress(200, 168); // DLOG403 item1, independently read from the original DITL.
    assert(textDialog.open && thinkingTime.seconds == secondsBeforeClick);
    handlePointerRelease(200, 168);
    assert(!textDialog.open && thinkingTime.seconds == 600000 && settings.level == 10);
    performMenuAction(3, 10);
    handleTextKey(SDLK_BACKSPACE, SDL_KMOD_NONE);
    textDialog.replace("0");
    handleTextKey(SDLK_RETURN, SDL_KMOD_NONE);
    assert(thinkingTime.seconds == 60);
    auto temporary =
        fs::temp_directory_path() / ("battlechess-time-" + std::to_string(SDL_GetTicksNS()));
    saveGame(temporary);
    performMenuAction(3, 2);
    receiveFileResult({1, temporary.string(), {}});
    std::error_code error;
    fs::remove(temporary, error);
    currentFile.clear();
    assert(textDialog.open && settings.level == 10 && textDialog.text == "1");
    textDialog.replace("12");
    handleTextKey(SDLK_HOME, SDL_KMOD_NONE);
    handleTextKey(SDLK_DELETE, SDL_KMOD_NONE);
    assert(textDialog.text == "2");
    handleTextKey(SDLK_RETURN, SDL_KMOD_NONE);
    assert(thinkingTime.seconds == 120);
    performMenuAction(3, 10);
}

void Application::checkComputerPlay() {
    acceptTextDialog();
    resetGame();
    performMenuAction(3, 0);

    checkLoadedPlayerModes();
    checkForcedComputerMove();
    checkMoveHintAndHistory();
    checkComputerForceAndReset();
    performMenuAction(2, 2);
    performMenuAction(2, 5);
}

void Application::checkLoadedPlayerModes() {
    auto humanSave =
        fs::temp_directory_path() / ("battlechess-human-" + std::to_string(SDL_GetTicksNS()));
    saveGame(humanSave);
    performMenuAction(2, 3);
    receiveFileResult({1, humanSave.string(), {}});
    std::error_code humanSaveError;
    fs::remove(humanSave, humanSaveError);
    currentFile.clear();
    assert(settings.white_player == 0 && session.computerRequested && !session.bookEligible);
    performMenuAction(2, 3);
    auto computerSave =
        fs::temp_directory_path() / ("battlechess-computer-" + std::to_string(SDL_GetTicksNS()));
    saveGame(computerSave);
    performMenuAction(2, 2);
    receiveFileResult({1, computerSave.string(), {}});
    std::error_code computerSaveError;
    fs::remove(computerSave, computerSaveError);
    currentFile.clear();
    assert(settings.white_player == 1 && !session.computerRequested && !session.bookEligible);
}

void Application::checkForcedComputerMove() {
    performMenuAction(1, 0);
    assert(settings.white_player == 1 && session.computerRequested);
    updateComputer();
    waitForComputerResult();
    assert(!computer.busy() && session.past.size() == 1 && session.pending);
    syncAnimation();
    animation.finish_for_check();
}

void Application::checkMoveHintAndHistory() {
    performMenuAction(2, 2);
    assert(settings.white_player == 0);
    auto beforeHint = session.game;
    performMenuAction(1, 3);
    waitForComputerResult();
    assert(!computer.busy() && hint &&
           !std::memcmp(&beforeHint, &session.game, sizeof(beforeHint)));
    performMenuAction(1, 1);
    assert(!session.computerRequested && !hint);
    performMenuAction(1, 2);
    assert(!session.computerRequested);
}

void Application::checkComputerForceAndReset() {
    performMenuAction(2, 6);
    updateComputer();
    performMenuAction(1, 0);
    waitForComputerResult();
    assert(!computer.busy() && session.past.size() == 2 && session.pending);
    syncAnimation();
    animation.finish_for_check();
    performMenuAction(2, 3);
    updateComputer();
    performMenuAction(0, 0);
    assert(alertId == 402);
    dismissAlert(1);
    assert(!computer.busy() && session.past.empty() && session.bookEligible);
}

void Application::waitForComputerResult() {
    const Uint64 deadline = SDL_GetTicks() + kComputerCheckTimeoutMs;
    while (computer.busy() && SDL_GetTicks() < deadline) {
        updateComputer();
        SDL_Delay(1);
    }
}
