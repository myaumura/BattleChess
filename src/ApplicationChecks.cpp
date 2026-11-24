#include "ApplicationInternal.h"
#include "LichessClient.h"
#include "PgnImport.h"
#include "presentation.h"
#include <algorithm>
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
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
                " piece=" + std::to_string(move.piece) +
                " victim=" + std::to_string(move.captured) + " from=" + std::to_string(move.from) +
                " to=" + std::to_string(move.to) + " head=" + std::to_string(animation.scene.head) +
                " type=" + std::to_string(animation.scene.active[animation.scene.head].type));
        int destination = engine_to_display(move.to);
        if (!animation.scene.board[destination])
            throw std::runtime_error("Animation lost moving piece");
        ++cases;
    }

    void AnimationChecks::checkPawnMovement() {
        // Removing speed dispatch must break the elapsed-time comparison, not just a label check.
        bool savedFast = animation.fast, savedSound = animation.soundEnabled;
        animation.fast = false;
        animation.soundEnabled = false;
        constexpr double speeds[] = {1.0, 1.25, 1.5, 1.75, 2.0};
        unsigned durations[5]{};
        for (int choice = 0; choice < 5; ++choice) {
            animation.cancel();
            animation.speed = speeds[choice];
            reset_board(&position);
            animation.begin(position, {0x34, 0x14, 0, 6, 0}, false);
            uint32_t started = animation.scene.previous_ticks;
            while (animation.busy() && durations[choice] < kMaxAnimationTicks) {
                ++durations[choice];
                animation.tick(started + durations[choice]);
            }
            assert(!animation.busy());
            assert(animation.scene.board[engine_to_display(0x34)]);
            const auto &sprite =
                animation.scene.sprites[animation.scene.sprite_at[engine_to_display(0x34)]];
            assert(sprite.visible && sprite.square == engine_to_display(0x34));
        }
        for (int choice = 1; choice < 5; ++choice) {
            assert(durations[choice] < durations[choice - 1]);
            assert(durations[choice] * speeds[choice] >= durations[0]);
            assert(durations[choice] * speeds[choice] < durations[0] + speeds[choice]);
        }
        // A delayed host update must carry its accelerated passes through both castling submoves.
        animation.cancel();
        position = {};
        insert_piece(&position, 1, 0, 4);
        insert_piece(&position, 3, 0, 7);
        animation.begin(position, {6, 4, 1, 1, 0}, false);
        animation.tick(animation.scene.previous_ticks + 12000);
        assert(!animation.busy());
        assert(animation.scene.board[engine_to_display(6)] &&
               animation.scene.board[engine_to_display(5)]);
        assert(!animation.scene.board[engine_to_display(4)] &&
               !animation.scene.board[engine_to_display(7)]);
        animation.speed = 1;
        animation.fast = savedFast;
        animation.soundEnabled = savedSound;
        animation.cancel();
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
            for (int to : {0x22, 0x23, 0x24, 0x32, 0x34, 0x42, 0x43, 0x44, 0x12, 0x14, 0x21, 0x25,
                           0x41, 0x45, 0x52, 0x54}) {
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
        checkCustomCursors();
        checkBoardInteraction();
        checkBothBoards();
        checkMovePanel();
        checkBoardAids();
        checkIdleAnimations();
        checkLichess();
        checkPgnImportAndGifExport();
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

void Application::checkCustomCursors() {
    auto savedSession = session;
    float savedX = mouseX, savedY = mouseY;
    session = GameSession();
    mouseX = mouseY = 100;
    render();
    assert(SDL_GetCursor() == SDL_GetDefaultCursor());

    session.pending = BCMove{};
    render();
    SDL_Cursor *movingCursor = SDL_GetCursor();
    assert(movingCursor == blockedCursor.get());
    assert(movingCursor != SDL_GetDefaultCursor());

    session.pending.reset();
    session.game.position = {};
    session.game.position.opponent = 1;
    insert_piece(&session.game.position, 1, 0, 4);
    insert_piece(&session.game.position, 1, 1, 0x77);
    insert_piece(&session.game.position, 3, 1, 0x64);
    calculate_piece_lists(&session.game.position);
    assert(bcGameInCheck(&session.game));
    render();
    SDL_Cursor *checkedCursor = SDL_GetCursor();
    assert(checkedCursor == this->checkedCursor.get());
    assert(checkedCursor != SDL_GetDefaultCursor());
    assert(checkedCursor != movingCursor);
    session.pending = BCMove{};
    render();
    assert(SDL_GetCursor() == movingCursor);

    session.pending.reset();
    assert(bcGameInCheck(&session.game));
    computer.start(session.game, session.past, false, 0, 1, true);
    render();
    assert(SDL_GetCursor() == thinkingCursor.get());
    assert(SDL_GetCursor() != SDL_GetDefaultCursor());
    assert(SDL_GetCursor() != movingCursor);
    assert(SDL_GetCursor() != checkedCursor);

    menu = 0;
    render();
    assert(SDL_GetCursor() == SDL_GetDefaultCursor());
    menu = -1;
    alertId = 402;
    render();
    assert(SDL_GetCursor() == SDL_GetDefaultCursor());
    alertId = 0;
    mouseY = 0;
    render();
    assert(SDL_GetCursor() == SDL_GetDefaultCursor());
    mouseY = 100;
    SDL_Event leave{};
    leave.type = SDL_EVENT_WINDOW_MOUSE_LEAVE;
    handleEvent(leave);
    render();
    assert(SDL_GetCursor() == SDL_GetDefaultCursor());

    computer.cancel();
    session = savedSession;
    mouseX = savedX;
    mouseY = savedY;
    render();
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
        animation.finishForCheck();
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

void Application::checkBothBoards() {
    animation.cancel();
    flat = true;
    session = GameSession();
    performMenuAction(2, kBothBoardsItem);
    const int dualWidth = logicalWidth + kFlatViewWidth;
    const int dualHeight = 20 + original_content_height + panelHeight();
    if (!bothBoards || flat || !dualView() || viewWidth() != dualWidth ||
        presentedWidth != dualWidth || dualWidth >= 2 * logicalWidth ||
        viewHeight() != dualHeight || presentedHeight != dualHeight)
        throw std::runtime_error("Both-boards layout failed");
    // Select e2 on the perspective board (left) and choose e4 on the flat board (right).
    auto centre = [](bool viewFlat, int display, int offset) {
        const int *xs = viewFlat ? flat_hit_x : perspective_hit_x;
        const int *ys = viewFlat ? flat_hit_y : perspective_hit_y;
        int row = display / 8, col = display % 8;
        return std::pair<int, int>{offset + (xs[row * 9 + col] + xs[row * 9 + col + 1]) / 2,
                                   20 + (ys[row] + ys[row + 1]) / 2};
    };
    auto [fromX, fromY] = centre(false, 52, 0);
    auto [toX, toY] = centre(true, 36, logicalWidth - kFlatViewLeft);
    if (boardSquareAt(fromX, fromY) != 52 || boardSquareAt(toX, toY) != 36)
        throw std::runtime_error("Both-boards hit test failed");
    handlePointerPress(fromX, fromY);
    handlePointerPress(toX, toY);
    if (session.game.position.board[0x34].piece != 6)
        throw std::runtime_error("Both-boards move failed");
    syncAnimation();
    const Position &during = flatViewPosition();
    if (!animation.busy() || during.board[0x14].piece != 6 || during.board[0x34].piece)
        throw std::runtime_error("Flat view advanced before the animation finished");
    renderBoard();
    animation.finishForCheck();
    const Position &after = flatViewPosition();
    if (after.board[0x14].piece || after.board[0x34].piece != 6)
        throw std::runtime_error("Flat view did not follow the finished move");
    renderBoard();
    performMenuAction(0, 4);
    if (!editing || dualView() || !flat || viewWidth() != logicalWidth)
        throw std::runtime_error("Board setup did not use the single flat view");
    performMenuAction(5, 2);
    if (editing || !dualView() || flat)
        throw std::runtime_error("Both-boards view not restored after setup");
    performMenuAction(2, kBothBoardsItem);
    applyLayout(false);
    if (bothBoards || !flat || viewWidth() != logicalWidth || presentedWidth != logicalWidth ||
        presentedHeight != 20 + original_content_height + (showCaptured ? kCapturedHeight : 0))
        throw std::runtime_error("Both-boards view did not restore the single board");
    // Leave the flat single view and a fresh game, as the following checks expect.
    animation.cancel();
    session = GameSession();
}

void Application::checkMovePanel() {
    auto load = [&](const std::string &text) {
        PgnImport game = importPgn(text);
        if (!game.error.empty())
            throw std::runtime_error("Panel check PGN: " + game.error);
        resetGame();
        session.game = game.start;
        for (const BCMove &move : game.moves)
            if (!session.apply(move, false))
                throw std::runtime_error("Panel check move rejected");
    };
    // A game too long for the panel scrolls to keep the latest move in view.
    std::string shuffle;
    for (int i = 0; i < 30; ++i)
        shuffle += "Nf3 Nf6 Ng1 Ng8 ";
    load(shuffle + "*");
    setBothBoards(true);
    applyLayout(false);
    auto tokens = panelTokens();
    auto shows = [&](int ply) {
        return std::any_of(tokens.begin(), tokens.end(),
                           [&](const PanelToken &token) { return token.ply == ply; });
    };
    if (panelMoves().size() != 120 || !shows(119) || shows(0))
        throw std::runtime_error("Panel did not scroll to the latest move");

    // A realistic game that fits: numbering, castling and captures in SAN.
    load(
        "1. e4 e5 2. Nf3 Nc6 3. Bb5 a6 4. Ba4 Nf6 5. O-O Be7 6. Re1 b5 7. Bb3 d6 8. c3 O-O "
        "9. h3 Nb8 10. d4 Nbd7 11. c4 c6 12. cxb5 axb5 13. Nc3 Bb7 14. Bg5 b4 15. Nb1 h6 "
        "16. Bh4 c5 17. dxe5 Nxe4 18. Bxe7 Qxe7 19. exd6 Qf6 20. Nbd2 Nxd6 *");
    const auto &moves = panelMoves();
    tokens = panelTokens();
    if (moves.size() != 40 || moves.front() != "e4" || moves[8] != "O-O" ||
        moves.back() != "Nxd6" || !shows(0) || !shows(39) || tokens.front().text != "1.")
        throw std::runtime_error("Panel move list is wrong");
    render();
    if (!options.screenshotPath.empty()) {
        Surface image(SDL_RenderReadPixels(rendererPtr, nullptr), SDL_DestroySurface);
        require(bool(image), "Panel screenshot");
        auto path = options.screenshotPath.parent_path() / "both-boards-panel.png";
        require(SDL_SavePNG(image.get(), path.string().c_str()), "Panel screenshot save");
    }

    // Clicking a move jumps there through the ordinary history, and back again.
    auto clickPly = [&](int ply) {
        for (const auto &token : panelTokens())
            if (token.ply == ply) {
                int y = 20 + original_content_height + 8 + token.row * 18 + 4;
                handlePointerPress(token.x + 1, y);
                handlePointerRelease(token.x + 1, y);
                return;
            }
        throw std::runtime_error("Panel move not shown");
    };
    clickPly(9); // 5... Be7
    if (session.past.size() != 10 || session.future.size() != 30 || panelMoves().size() != 40)
        throw std::runtime_error("Panel click did not take back");
    clickPly(39);
    if (session.past.size() != 40 || !session.future.empty())
        throw std::runtime_error("Panel click did not replay");
    // Number labels are not moves.
    auto number = panelTokens().front();
    handlePointerPress(number.x + 1, 20 + original_content_height + 8 + 4);
    if (session.past.size() != 40)
        throw std::runtime_error("Panel number label changed the game");

    // Full screen toggles from the Settings menu and keeps the layout consistent.
    bool wasFullscreen = fullscreen();
    performMenuAction(2, kFullScreenItem);
    performMenuAction(2, kFullScreenItem);
    applyLayout(false);
    if (fullscreen() != wasFullscreen || alertId || presentedHeight != viewHeight())
        throw std::runtime_error("Full screen toggle failed");

    setBothBoards(false);
    applyLayout(false);
    animation.cancel();
    session = GameSession();
}

/* Captured pieces and coordinates: history-based capture list, layout and drawing. */
void Application::checkBoardAids() {
    auto fail = [](const std::string &what) { throw std::runtime_error("Board aids: " + what); };
    auto load = [&](const std::string &text) {
        PgnImport game = importPgn(text);
        if (!game.error.empty())
            fail("PGN " + game.error);
        resetGame();
        session.game = game.start;
        for (const BCMove &move : game.moves)
            if (!session.apply(move, false))
                fail("move rejected");
    };
    auto shot = [&](const char *name) {
        applyLayout(false);
        render();
        if (options.screenshotPath.empty())
            return;
        Surface image(SDL_RenderReadPixels(rendererPtr, nullptr), SDL_DestroySurface);
        require(bool(image), "Board aids screenshot");
        auto path = options.screenshotPath.parent_path() / name;
        require(SDL_SavePNG(image.get(), path.string().c_str()), "Board aids screenshot save");
    };
    const bool savedFlat = flat;
    if (!showCoordinates || !showCaptured)
        fail("aids should be on by default");

    // Pawn exchange, then a queen trade: each side has lost a pawn and a queen.
    load("1. e4 d5 2. exd5 Qxd5 3. Nc3 Qxd2+ 4. Qxd2 Nf6 5. Qxd8+ Kxd8 *");
    auto captured = capturedPieces();
    if (captured[0] != std::vector<uint8_t>{2, 6, 6} || captured[1] != std::vector<uint8_t>{2, 6})
        fail("captured lists");
    flat = false;
    shot("board-aids-3d.png");
    flat = true;
    shot("board-aids-2d.png");

    // En passant counts; promotion does not.
    load("1. e4 a6 2. e5 d5 3. exd6 *");
    captured = capturedPieces();
    if (!captured[0].empty() || captured[1] != std::vector<uint8_t>{6})
        fail("en passant capture");
    load("[FEN \"k7/7P/8/8/8/8/8/K7 w - - 0 1\"]\n\n1. h8=Q *");
    captured = capturedPieces();
    if (!captured[0].empty() || !captured[1].empty())
        fail("promotion counted as a capture");

    // Toggles: the strip changes the single view's height; coordinates only drawing.
    const int withStrip = viewHeight();
    performMenuAction(2, kCapturedItem);
    applyLayout(false);
    if (showCaptured || viewHeight() != withStrip - kCapturedHeight ||
        presentedHeight != viewHeight())
        fail("captured strip toggle");
    performMenuAction(2, kCapturedItem);
    performMenuAction(2, kCoordinatesItem);
    if (showCoordinates)
        fail("coordinates toggle");
    render();
    performMenuAction(2, kCoordinatesItem);
    if (!showCaptured || !showCoordinates || viewHeight() != withStrip)
        fail("aids restored");

    // README screenshots: opening position in both views, and a pawn combat mid-fight.
    if (!options.screenshotPath.empty()) {
        resetGame();
        flat = false;
        shot("readme-perspective.png");
        flat = true;
        shot("readme-flat.png");
        flat = false;
        load("1. e4 d5 *");
        BCMove capture{};
        std::string problem;
        if (!resolveMove(session.game, "exd5", capture, problem))
            fail("combat move");
        Position before = session.game.position;
        session.apply(capture, false);
        unsigned steps = 0;
        animation.begin(before, capture, false);
        while (animation.busy() && steps < 20000) {
            animation.tick(animation.scene.previous_ticks + 6);
            ++steps;
        }
        for (int percent : {45, 60, 75}) {
            animation.cancel();
            animation.begin(before, capture, false);
            for (unsigned step = 0; step < steps * unsigned(percent) / 100; ++step)
                animation.tick(animation.scene.previous_ticks + 6);
            std::string name = "readme-combat-" + std::to_string(percent) + ".png";
            shot(name.c_str());
        }
        animation.cancel();
    }

    // Both boards: the captured row closes the panel.
    load("1. e4 d5 2. exd5 Qxd5 3. Nc3 Qxd2+ 4. Qxd2 Nf6 5. Qxd8+ Kxd8 *");
    setBothBoards(true);
    if (viewHeight() != 20 + original_content_height + kPanelHeight + kCapturedHeight)
        fail("panel height with captures");
    shot("board-aids-both.png");
    setBothBoards(false);
    applyLayout(false);
    flat = savedFlat;
    animation.cancel();
    session = GameSession();
}

void Application::checkIdleAnimations() {
    animation.cancel();
    session = GameSession();
    bool savedFlat = flat;
    flat = false;
    const uint8_t savedWhite = settings.white_player;
    settings.white_player = 0;
    uint8_t display[64];
    setup_display_board(&session.game.position, display);
    const uint32_t randomBefore = animation.sharedRandomState();
    const unsigned startedBefore = idle.started();
    if (!idleAllowed())
        throw std::runtime_error("Idle gate closed on a quiet human turn");

    // Off by default: nothing plays however long the board stays quiet.
    uint64_t now = SDL_GetTicks();
    idle.update(now, true, display, 1);
    idle.update(now + 10 * IdleAnimator::kMaxDelay, true, display, 1);
    if (idle.enabled() || idle.current() || idle.started() != startedBefore)
        throw std::runtime_error("Idle animation played while disabled");

    performMenuAction(2, kIdleAnimationsItem);
    if (!idle.enabled())
        throw std::runtime_error("Idle Animations menu item did not enable playback");
    now = SDL_GetTicks();
    idle.update(now, true, display, 1);
    idle.update(now + IdleAnimator::kMinDelay - 1, true, display, 1);
    if (idle.current())
        throw std::runtime_error("Idle clip started before the quiet period");
    now += IdleAnimator::kMaxDelay;
    idle.update(now, true, display, 1);
    auto first = idle.current();
    if (!first || (display[first->square] & 7) != 1 ||
        first->white != bool(display[first->square] & 64))
        throw std::runtime_error("Idle clip did not start on a bishop");
    // Half a second in, the clip has advanced five frames.
    idle.update(now + 5 * IdleAnimator::kFrameMilliseconds, true, display, 1);
    auto later = idle.current();
    if (!later || later->square != first->square || later->frame != first->frame + 5)
        throw std::runtime_error("Idle clip did not advance");
    render();
    if (!options.screenshotPath.empty()) {
        Surface image(SDL_RenderReadPixels(rendererPtr, nullptr), SDL_DestroySurface);
        require(bool(image), "Idle screenshot");
        auto path = options.screenshotPath.parent_path() / "idle-animation.png";
        require(SDL_SavePNG(image.get(), path.string().c_str()), "Idle screenshot save");
    }
    // A clip ends on its own; the longest has 29 frames.
    idle.update(now + 30 * IdleAnimator::kFrameMilliseconds, true, display, 1);
    if (idle.current())
        throw std::runtime_error("Idle clip did not end");

    // Input stops a running clip.
    now += IdleAnimator::kMaxDelay * 2;
    idle.update(now, true, display, 1);
    if (!idle.current())
        throw std::runtime_error("Idle clip did not restart after the quiet period");
    SDL_Event key{};
    key.type = SDL_EVENT_KEY_DOWN;
    key.key.key = SDLK_A;
    handleEvent(key);
    if (idle.current())
        throw std::runtime_error("Key press did not stop the idle clip");

    // The gate: flat board, the Mac's turn, an open menu or dialog.
    flat = true;
    bool flatBlocked = !idleAllowed();
    flat = false;
    settings.white_player = 1;
    bool macBlocked = !idleAllowed();
    settings.white_player = 0;
    menu = 2;
    bool menuBlocked = !idleAllowed();
    menu = -1;
    showAlert("Idle check");
    bool alertBlocked = !idleAllowed();
    dismissAlert(1);
    if (!flatBlocked || !macBlocked || !menuBlocked || !alertBlocked || !idleAllowed())
        throw std::runtime_error("Idle gate did not block");
    // Leaving the gate open again waits a full quiet period before the next clip.
    idle.update(now, false, display, 1);
    idle.update(now + 1, true, display, 1);
    idle.update(now + 2, true, display, 1);
    if (idle.current())
        throw std::runtime_error("Idle clip started without a quiet period");

    if (animation.sharedRandomState() != randomBefore)
        throw std::runtime_error("Idle animation consumed the shared RNG");
    performMenuAction(2, kIdleAnimationsItem);
    if (idle.enabled() || idle.current())
        throw std::runtime_error("Idle Animations did not turn off");
    settings.white_player = savedWhite;
    flat = savedFlat;
    session = GameSession();
}

/* Lichess flow with injected server replies; requests are recorded, never sent. */
void Application::checkLichess() {
    auto fail = [](const std::string &what) { throw std::runtime_error("Lichess: " + what); };
    auto sent = [&](const std::string &request) {
        return std::find(lichess.outbox.begin(), lichess.outbox.end(), request) !=
               lichess.outbox.end();
    };
    auto sentPrefix = [&](const std::string &prefix) {
        return std::any_of(lichess.outbox.begin(), lichess.outbox.end(),
                           [&](const std::string &r) { return r.rfind(prefix, 0) == 0; });
    };
    auto reply = [&](const std::string &tag, int status, const std::string &body) {
        LichessMessage message;
        message.tag = tag;
        message.status = status;
        message.body = body;
        handleLichessMessage(message);
    };
    auto line = [&](const std::string &stream, const std::string &body) {
        LichessMessage message;
        message.kind = LichessMessage::Kind::line;
        message.tag = stream;
        message.status = 200;
        message.body = body;
        handleLichessMessage(message);
    };
    auto state = [&](const std::string &moves, const std::string &extra = "") {
        line("game", R"({"type":"gameState","moves":")" + moves +
                         R"(","wtime":590000,"btime":600000,"winc":0,"binc":0,"status":"started")" +
                         extra + "}");
    };
    auto shot = [&](const char *name) {
        render();
        if (options.screenshotPath.empty())
            return;
        Surface image(SDL_RenderReadPixels(rendererPtr, nullptr), SDL_DestroySurface);
        require(bool(image), "Lichess screenshot");
        auto path = options.screenshotPath.parent_path() / name;
        require(SDL_SavePNG(image.get(), path.string().c_str()), "Lichess screenshot save");
    };
    auto click = [&](int engineSquare) {
        int display = engine_to_display(engineSquare), row = display / 8, col = display % 8;
        int x = (perspective_hit_x[row * 9 + col] + perspective_hit_x[row * 9 + col + 1]) / 2;
        int y = 20 + (perspective_hit_y[row] + perspective_hit_y[row + 1]) / 2;
        handlePointerPress(x, y);
        handlePointerRelease(x, y);
    };

    animation.cancel();
    session = GameSession();
    const BCSaveSettings savedSettings = settings;
    const bool savedFlat = flat;
    flat = false;
    settings.white_player = 0;
    settings.black_player = 1;
    settings.level = 2;
    lichess.outbox.clear();

    if (menuHeadingAt(menuX[kLichessMenu] + 5, 6) != kLichessMenu)
        fail("menu missing from the Modem slot");
    if (menuItemEnabled(kLichessMenu, kLichessPlayAi) || menuItemEnabled(kLichessMenu, kLichessAnalyse))
        fail("play and analyse enabled before connecting or moving");

    // Connect: without a token the prompt opens together with the token page.
    performMenuAction(kLichessMenu, kLichessConnect);
    if (textDialog.open) {
        if (textDialog.resourceId != kTokenPrompt || !sentPrefix("OPEN https://lichess.org/account/oauth/token/create"))
            fail("token prompt");
        textDialog.replace("lip_Test-Token");     // '-' is not a token character...
        textDialog.replace("lip_TestToken_123");  // ...so only this lands.
        if (textDialog.text != "lip_TestToken_123")
            fail("token characters");
        shot("lichess-token-prompt.png");
        acceptTextDialog();
    }
    if (!sent("GET /api/account") || !lichess.connecting)
        fail("account request");
    reply("account", 200, R"({"id":"tester","username":"Tester"})");
    if (!lichess.connected || lichess.username != "Tester" || !sent("STREAM events GET /api/stream/event"))
        fail("connection");

    // Connecting lists running games in the lobby; nothing takes over the board.
    if (!sent("GET /api/account/playing?nb=50"))
        fail("running games requested");
    reply("playing", 200,
          R"({"nbMyTurn":1,"nowPlaying":[{"gameId":"old1","fullId":"old1xxxx","color":"black",)"
          R"("fen":"","hasMoved":true,"isMyTurn":true,"lastMove":"e2e4","opponent":{"id":"f",)"
          R"("username":"Friend","rating":1500},"perf":"correspondence","rated":false,"rating":1500,)"
          R"("secondsLeft":86000,"source":"friend","speed":"correspondence","variant":{"key":"standard"}},)"
          R"({"gameId":"web1","fullId":"web1xxxx","color":"white","fen":"","hasMoved":true,)"
          R"("isMyTurn":false,"lastMove":"","opponent":{"id":"g","username":"Speedy"},"perf":"bullet",)"
          R"("rated":true,"rating":1500,"secondsLeft":50,"source":"pool","speed":"bullet",)"
          R"("variant":{"key":"standard"}}]})");
    if (!lichess.lobbyOpen || lichess.inGame || lobbyRows().size() != 2 ||
        lichess.games[1].boardPlayable)
        fail("lobby on connect");
    if (lobbyRowText(lobbyRows()[0]) != "Friend (1500) - correspondence - your move")
        fail("lobby row text");
    shot("lichess-lobby.png");
    if (menuItemEnabled(0, 0))
        fail("menus behind the lobby");
    auto press = [&](int button) {
        SDL_Rect r = lobbyButtonRect(button);
        handlePointerPress(r.x + r.w / 2, r.y + r.h / 2);
        handlePointerRelease(r.x + r.w / 2, r.y + r.h / 2);
    };
    // Keys move the selection; a bullet game can only be opened in the browser.
    lichessLobbyKey(SDLK_DOWN);
    if (std::string(lobbyButtonLabel(kLobbyOpen)) != "In Browser")
        fail("bullet game marked for the browser");
    lichessLobbyKey(SDLK_RETURN);
    const std::string &opened = lichess.outbox.back();
    if (opened.rfind("OPEN ", 0) != 0 || opened.compare(opened.size() - 5, 5, "/web1") != 0)
        fail("bullet game opened in the browser");
    lichessLobbyKey(SDLK_ESCAPE);
    if (lichess.lobbyOpen)
        fail("Escape closes the lobby");

    // Games... reopens it; Open joins the selected game.
    performMenuAction(kLichessMenu, kLichessGames);
    if (!lichess.lobbyOpen)
        fail("Games... opens the lobby");
    lichess.lobbySelection = 0;
    press(kLobbyOpen);
    if (!lichess.inGame || lichess.color != "black" || lichess.lobbyOpen ||
        !sent("STREAM game GET /api/board/game/stream/old1"))
        fail("open a game from the lobby");

    // Leave Game returns to a local board without resigning.
    performMenuAction(kLichessMenu, kLichessLeave);
    if (lichess.inGame || alertId != 406 || sentPrefix("POST /api/board/game/old1/resign") ||
        !sent("CLOSE game"))
        fail("leave game");
    dismissAlert(1);
    if (!menuItemEnabled(0, 0))
        fail("New Game after leaving");

    // A game Lichess refuses to stream is left and marked for the browser.
    lichessJoinGame("old1", "black", "Friend");
    {
        LichessMessage refused;
        refused.kind = LichessMessage::Kind::closed;
        refused.tag = "game";
        refused.status = 400;
        refused.body = R"({"error":"This game cannot be played with the Board API."})";
        handleLichessMessage(refused);
    }
    if (lichess.inGame || alertId != 406 || lichess.games[0].boardPlayable)
        fail("refused game stream is left");
    dismissAlert(1);
    click(0x14); // e2: the board works again.
    if (session.selected != 0x14)
        fail("board locked after leaving an online game");
    session = GameSession();

    // Challenge a friend from the lobby, then withdraw it.
    openLichessLobby();
    press(kLobbyChallenge);
    if (!textDialog.open || textDialog.resourceId != kUsernamePrompt)
        fail("username prompt");
    textDialog.replace("friend_1");
    acceptTextDialog();
    if (!sent("STREAM challenge POST /api/challenge/friend_1 rated=false&clock.limit=600&"
              "clock.increment=0&color=white&keepAliveStream=true") ||
        !lichess.challenging || alertId != 406)
        fail("challenge sent");
    dismissAlert(1);
    if (std::string(lobbyButtonLabel(kLobbyChallenge)) != "Cancel Ch." ||
        lobbyButtonEnabled(kLobbyPlayAi))
        fail("pending challenge in the lobby");
    press(kLobbyChallenge);
    if (lichess.challenging || lichess.expectingGame || !sent("CLOSE challenge"))
        fail("challenge withdrawn");
    press(kLobbyClose);
    if (lichess.lobbyOpen)
        fail("Close button");

    // Analysis upload for a local game.
    BCMove moves[BC_GAME_MOVE_CAPACITY];
    bcGameLegalMoves(&session.game, moves);
    session.apply(moves[0], false);
    performMenuAction(kLichessMenu, kLichessAnalyse);
    if (!sentPrefix("POST /api/import pgn="))
        fail("analysis upload");
    reply("import", 200, R"({"id":"imp1","url":"https://lichess.org/imp1"})");
    if (!sent("OPEN https://lichess.org/imp1"))
        fail("analysis page");
    session = GameSession();

    // Play the AI as White (Settings: Human White, Mac Black), level from the Level menu.
    lichess.timeControl = 0;
    performMenuAction(kLichessMenu, kLichessPlayAi);
    if (!sent("POST /api/challenge/ai level=2&clock.limit=600&clock.increment=0&color=white"))
        fail("AI challenge request");
    line("events", R"({"type":"gameStart","game":{"gameId":"g1","fullId":"g1xxxx","color":"white",)"
                   R"("opponent":{"id":null,"username":"AI","ai":2}}})");
    if (!lichess.inGame || !sent("STREAM game GET /api/board/game/stream/g1"))
        fail("game start");
    line("game", R"({"type":"gameFull","id":"g1","initialFen":"startpos","speed":"rapid",)"
                 R"("clock":{"initial":600000,"increment":0},"white":{"id":"tester","name":"Tester",)"
                 R"("rating":1500},"black":{"aiLevel":2},"state":{"type":"gameState",)"
                 R"("moves":"e2e4 e7e5","wtime":600000,"btime":600000,"winc":0,"binc":0,"status":"started"}})");
    if (session.past.size() != 2 || animation.busy() || session.pending)
        fail("gameFull sync without animation");

    // Fair play: no engine moves, hints or local history commands.
    if (menuItemEnabled(1, 0) || menuItemEnabled(1, 1) || menuItemEnabled(1, 3) ||
        menuItemEnabled(0, 0) || menuItemEnabled(2, 6) || !menuItemEnabled(0, kExportPgnItem))
        fail("menus during an online game");
    session.computerRequested = true;
    updateComputer();
    if (computer.busy() || session.computerRequested)
        fail("engine ran during an online game");

    // Own move: sent at commit; Auto + rapid + quiet = fast animation.
    click(0x06);
    click(0x25);
    if (session.past.size() != 3)
        fail("own move");
    syncAnimation();
    if (!sent("POST /api/board/game/g1/move/g1f3") || !animation.busy() ||
        animation.minimumSpeed != 2)
        fail("own move sent and animated fast");

    // The reply arrives mid-animation; the flat board already shows it.
    state("e2e4 e7e5 g1f3 b8c6");
    if (session.past.size() != 4 || !session.pending ||
        std::memcmp(&flatViewPosition(), &session.game.position, sizeof(Position)) != 0)
        fail("opponent reply queued with the flat board up to date");
    // A click finishes the animation and selects at once.
    click(0x05);
    if (animation.busy() || session.pending || session.selected != 0x05)
        fail("click during animation");
    session.selected = -1;

    // Server echo of an own move does not animate; an opponent capture under Auto/rapid
    // appears at once.
    state("e2e4 e7e5 g1f3 b8c6 f1b5");
    if (session.past.size() != 5 || session.pending)
        fail("own move echo");
    state("e2e4 e7e5 g1f3 b8c6 f1b5 c6d4");
    finishLichessAnimation();
    state("e2e4 e7e5 g1f3 b8c6 f1b5 c6d4 f3d4");
    state("e2e4 e7e5 g1f3 b8c6 f1b5 c6d4 f3d4 e5d4");
    if (session.past.size() != 8 || session.pending || animation.busy())
        fail("opponent capture shown at once");
    long long clock = lichessClock(true);
    if (clock > 590000 || clock < 580000)
        fail("white clock");
    if (formatLichessClock(lichessClock(false)) != "10:00")
        fail("black clock");
    menu = kLichessMenu;
    shot("lichess-game.png");
    menu = -1;

    // A jump of several moves (or a mismatch) rebuilds without animation.
    state("e2e4 e7e5 g1f3 b8c6 f1b5 c6d4 f3d4 e5d4 e1g1 g8f6 d2d3");
    if (session.past.size() != 11 || session.pending)
        fail("multi-move resync");

    // Draw offer from the opponent, accepted.
    state("e2e4 e7e5 g1f3 b8c6 f1b5 c6d4 f3d4 e5d4 e1g1 g8f6 d2d3", R"(,"bdraw":true)");
    if (alertId != 402 || confirmAction != kConfirmAcceptDraw)
        fail("draw offer prompt");
    dismissAlert(1);
    if (!sent("POST /api/board/game/g1/draw/yes"))
        fail("draw accepted");

    // Resign through the menu.
    performMenuAction(kLichessMenu, kLichessResign);
    if (alertId != 402 || confirmAction != kConfirmResign)
        fail("resign confirmation");
    dismissAlert(1);
    if (!sent("POST /api/board/game/g1/resign"))
        fail("resign request");
    line("game", R"({"type":"gameState","moves":"e2e4 e7e5 g1f3 b8c6 f1b5 c6d4 f3d4 e5d4 e1g1 g8f6 d2d3",)"
                 R"("wtime":500000,"btime":600000,"winc":0,"binc":0,"status":"resign","winner":"black"})");
    if (lichess.inGame || alertId != 406 || message != "Lichess AI level 2 wins by resignation." ||
        !sent("CLOSE game"))
        fail("game end");
    dismissAlert(1);
    if (session.past.size() != 11 || !menuItemEnabled(0, 0))
        fail("finished game stays on the board");
    performMenuAction(kLichessMenu, kLichessAnalyse);
    if (lichess.outbox.back().size() < 3 || lichess.outbox.back().compare(lichess.outbox.back().size() - 3, 3, "/g1") != 0)
        fail("finished game opens on Lichess");

    // An incoming challenge opens the lobby with it selected; Decline answers it.
    line("events", R"({"type":"challenge","challenge":{"id":"ch1","direction":"in","rated":false,)"
                   R"("speed":"rapid","color":"random","challenger":{"id":"x","name":"Opponent"},)"
                   R"("timeControl":{"type":"clock","limit":600,"increment":0,"show":"10+0"}}})");
    if (!lichess.lobbyOpen || !lobbyRows()[size_t(lichess.lobbySelection)].challenge ||
        lobbyRowText(lobbyRows()[size_t(lichess.lobbySelection)]) !=
            "Challenge from Opponent: 10+0 casual" ||
        std::string(lobbyButtonLabel(kLobbyOpen)) != "Accept")
        fail("incoming challenge in the lobby");
    press(kLobbyDecline);
    if (!sent("POST /api/challenge/ch1/decline reason=generic") || !lichess.challenges.empty())
        fail("challenge declined");
    press(kLobbyClose);

    performMenuAction(kLichessMenu, kLichessConnect);
    if (lichess.connected || !sent("CLOSE events"))
        fail("disconnect");

    animation.cancel();
    animation.minimumSpeed = 1;
    lichess = LichessState();
    lichess.offline = true;
    session = GameSession();
    settings = savedSettings;
    flat = savedFlat;
}

void Application::checkPgnImportAndGifExport() {
    fs::path directory = fs::temp_directory_path() / "battlechess-smoke-export";
    fs::create_directories(directory);
    auto writeText = [](const fs::path &path, const char *text) {
        std::ofstream out(path, std::ios::binary);
        out << text;
    };
    // A finished game loads at its final position without the resetting ending alert.
    writeText(directory / "mate.pgn",
              "[White \"Smoke\"]\n1. e4 e5 2. Bc4 Nc6 3. Qh5 Nf6 4. Qxf7# 1-0\n");
    importPgnFile(directory / "mate.pgn");
    if (alertId || session.past.size() != 7 || !session.endingAnnounced ||
        session.game.position.board[0x65].piece != 2 || session.computerRequested)
        throw std::runtime_error("PGN import did not load the game");
    finishTurn();
    if (alertId)
        throw std::runtime_error("Imported finished game raised an ending alert");

    // Record through the original animations, including capture and mate, from the flat
    // view: the export must still show the 3D board. Kept beside the smoke screenshot,
    // when one is requested, for manual inspection.
    fs::path gifPath = options.screenshotPath.empty()
                           ? directory / "mate"
                           : options.screenshotPath.parent_path() / "smoke-export";
    bool savedFlat = flat, savedFast = animation.fast;
    flat = true;
    uint32_t randomBefore = animation.sharedRandomState();
    auto masksBefore = animation.fadeMaskOrder();
    if (!exportGifFile(gifPath) || alertId)
        throw std::runtime_error("GIF export failed");
    if (animation.fadeMaskOrder() != masksBefore)
        throw std::runtime_error("GIF export changed the fade masks");
    // The progress dialog appeared at the start, during and at the end of the export.
    if (gifProgressFrames < 3)
        throw std::runtime_error("GIF export showed no progress");
    auto readBytes = [](const fs::path &path) {
        std::ifstream in(path, std::ios::binary);
        return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    };
    std::string single = readBytes(gifPath.replace_extension(".gif"));
    if (single.compare(0, 6, "GIF89a") != 0 || lastGifFrames < 40)
        throw std::runtime_error("GIF export wrote too little");
    // With both boards shown (3D on the left) the export is byte-identical: it ignores
    // the current view and the wider window layout.
    setBothBoards(true);
    fs::path dualPath = directory / "mate-both.gif";
    if (!exportGifFile(dualPath) || alertId || readBytes(dualPath) != single)
        throw std::runtime_error("GIF export differs between views");
    setBothBoards(false);
    flat = savedFlat;
    if (animation.sharedRandomState() != randomBefore || animation.busy() ||
        animation.fast != savedFast || animation.fadeFrame || session.past.size() != 7)
        throw std::runtime_error("GIF export changed live state");

    // The imported moves are ordinary history: Take Back reopens the game.
    performMenuAction(1, 1);
    if (session.past.size() != 6 || session.endingAnnounced)
        throw std::runtime_error("Take Back after PGN import failed");

    // A bad file reports the move and leaves the current game alone.
    writeText(directory / "bad.pgn", "1. e4 e5 2. Ke3 *\n");
    importPgnFile(directory / "bad.pgn");
    if (!alertId || message != "Unable to import PGN. Illegal move: 2. Ke3" ||
        session.past.size() != 6)
        throw std::runtime_error("Invalid PGN was not rejected");
    dismissAlert(1);
    fs::remove_all(directory);
    animation.cancel();
    session = GameSession();
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
    animation.finishForCheck();
    handlePointerPress(200, 150);
    if (session.game.position.board[0x70].piece != 2)
        throw std::runtime_error("Promotion selection failed");
    syncAnimation();
    animation.finishForCheck();
    session = GameSession();
}

void Application::checkMenuTracking() {
    // Native MenuSelect adapter regression (original call 0x103f6): use event handlers.
    handlePointerPress(menuX[0], 6);
    assert(menu == 0 && !alertId);
    trackMenu(menuX[2], 6);
    assert(menu == 2 && menuItem == -1);
    bool soundBefore = animation.soundEnabled;
    trackMenu(menuX[2] + 20, 26);
    assert(menuItem == 0 && animation.soundEnabled == soundBefore);
    handlePointerRelease(menuX[2] + 20, 26);
    assert(menu == -1 && animation.soundEnabled != soundBefore);
    animation.soundEnabled = soundBefore;
    double speedBefore = animation.speed;
    constexpr double speeds[] = {1.0, 1.25, 1.5, 1.75, 2.0};
    for (int choice = 0; choice < 5; ++choice) {
        handlePointerPress(menuX[2], 6);
        double before = animation.speed;
        trackMenu(menuX[2] + 20, 26 + 9 * 18);
        assert(menuItem == 9 && animation.speed == before);
        // Keep the parent open on release, then click in the popup to its right.
        handlePointerRelease(menuX[2] + 20, 26 + 9 * 18);
        assert(menu == 2 && menuItem == 9 && animation.speed == before);
        int x = menuX[2] + 270, y = 26 + (9 + choice) * 18;
        trackMenu(x, y);
        assert(menu == 2 && menuItem == 9 && animation.speed == before);
        render();
        if (choice == 2 && !options.screenshotPath.empty()) {
            Surface image(SDL_RenderReadPixels(rendererPtr, nullptr), SDL_DestroySurface);
            require(bool(image), "Speed submenu screenshot");
            auto path = options.screenshotPath.parent_path() / "animation-speed-menu.png";
            require(SDL_SavePNG(image.get(), path.string().c_str()),
                    "Speed submenu screenshot save");
        }
        handlePointerPress(x, y);
        handlePointerRelease(x, y);
        assert(menu == -1 && animation.speed == speeds[choice]);
    }
    // Hover across both popups while held; leaving the popup must cancel selection.
    handlePointerPress(menuX[2], 6);
    trackMenu(menuX[2] + 20, 26 + 9 * 18);
    trackMenu(menuX[2] + 270, 26 + 11 * 18);
    handlePointerRelease(menuX[2] + 270, 26 + 11 * 18);
    assert(menu == -1 && animation.speed == 1.5);
    handlePointerPress(menuX[2], 6);
    trackMenu(menuX[2] + 20, 26 + 9 * 18);
    trackMenu(menuX[2] + 270, 26 + 13 * 18);
    handlePointerRelease(500, 300);
    assert(menu == -1 && animation.speed == 1.5);
    handlePointerPress(menuX[2], 6);
    trackMenu(menuX[2] + 20, 26 + 9 * 18);
    trackMenu(menuX[0], 6);
    handlePointerRelease(menuX[0], 6); // Dragging back onto a title leaves that menu open.
    assert(menu == 0 && menuItem == -1 && animation.speed == 1.5);
    handlePointerPress(menuX[0], 6);
    handlePointerRelease(menuX[0], 6);
    assert(menu == -1 && animation.speed == 1.5);
    handlePointerPress(menuX[2], 6);
    trackMenu(menuX[2] + 20, 26 + 9 * 18);
    SDL_Event escape{};
    escape.type = SDL_EVENT_KEY_DOWN;
    escape.key.key = SDLK_ESCAPE;
    handleEvent(escape);
    assert(menu == -1 && menuItem == -1 && speedMenuItem == -1 && animation.speed == 1.5);
    animation.speed = speedBefore;
    handlePointerPress(menuX[1], 6);
    trackMenu(menuX[1] + 20, 26 + 18); // Undo unavailable with empty history.
    assert(menuItem == -1 && session.past.empty());
    handlePointerRelease(menuX[1] + 20, 26 + 18);
    assert(menu == -1 && session.past.empty() && !alertId);
    handlePointerPress(menuX[0], 6);
    trackMenu(500, 300);
    handlePointerRelease(500, 300);
    assert(menu == -1 && !alertId);
    // Click-to-open: releasing the heading keeps the menu open for a second click.
    handlePointerPress(menuX[0], 6);
    handlePointerRelease(menuX[0], 6);
    assert(menu == 0 && menuItem == -1 && !alertId);
    trackMenu(menuX[0] + 20, 26); // Hover without the button highlights rows.
    assert(menu == 0 && menuItem == 0);
    trackMenu(menuX[2], 6); // Moving over another title switches menus.
    assert(menu == 2 && menuItem == -1);
    soundBefore = animation.soundEnabled;
    handlePointerPress(menuX[2] + 20, 26);
    assert(menu == 2 && animation.soundEnabled == soundBefore);
    handlePointerRelease(menuX[2] + 20, 26);
    assert(menu == -1 && animation.soundEnabled != soundBefore);
    animation.soundEnabled = soundBefore;
    // Clicking the open title again closes it.
    handlePointerPress(menuX[0], 6);
    handlePointerRelease(menuX[0], 6);
    handlePointerPress(menuX[0], 6);
    handlePointerRelease(menuX[0], 6);
    assert(menu == -1 && !alertId);
    // Clicking outside closes it without touching the board.
    int selectedBefore = session.selected;
    handlePointerPress(menuX[0], 6);
    handlePointerRelease(menuX[0], 6);
    handlePointerPress(500, 300);
    handlePointerRelease(500, 300);
    assert(menu == -1 && session.selected == selectedBefore && !alertId);
    // Click Settings, click Animation Speed, click a speed.
    speedBefore = animation.speed;
    handlePointerPress(menuX[2], 6);
    handlePointerRelease(menuX[2], 6);
    handlePointerPress(menuX[2] + 20, 26 + 9 * 18);
    handlePointerRelease(menuX[2] + 20, 26 + 9 * 18);
    assert(menu == 2 && menuItem == 9);
    trackMenu(menuX[2] + 270, 26 + 13 * 18);
    handlePointerPress(menuX[2] + 270, 26 + 13 * 18);
    handlePointerRelease(menuX[2] + 270, 26 + 13 * 18);
    assert(menu == -1 && animation.speed == 2.0);
    animation.speed = speedBefore;
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
    animation.finishForCheck();
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
        animation.finishForCheck();
    }
    finishTurn();
    assert(session.mateAnimationDone);
    animation.finishForCheck();
    finishTurn();
    assert(alertId == 406 && message == "Check and mate.");
    dismissAlert(1);
    // The finished game stays on the board: exportable, frozen, until New Game.
    assert(!alertId && session.mateAnimationDone && session.endingAnnounced &&
           session.past.size() == 4 && session.game.position.board[0x37].piece == 2);
    assert(menuItemEnabled(0, kExportPgnItem) && menuItemEnabled(0, kExportGifItem) &&
           menuItemEnabled(1, 1));
    session.click(engine_to_display(0x14));
    handlePointerPress(250, 200);
    assert(session.past.size() == 4);
    performMenuAction(0, 0);
    assert(alertId == 402);
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
    assert(!alertId && session.endingAnnounced && session.game.position.board[0x51].piece == 2);
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
    const auto &about = *findResourceDialog(400, ResourceDialogKind::alert);
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
    animation.finishForCheck();
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
    animation.finishForCheck();
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
    animation.finishForCheck();
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
