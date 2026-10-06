#include "ApplicationInternal.h"
#include "adjudication.h"
#include "animation_plan.h"
#include "mac_bitmap_font.h"
#include <algorithm>
#include <stdexcept>

/* Native player-settings lookup for GETSETTI 0x74b4. Retain original control
 * codes (Human 0, Mac 1, Modem 2) when deciding whose turn to schedule. */
uint8_t playerForSide(const BCSaveSettings &settings, unsigned side) {
    return side ? settings.black_player : settings.white_player;
}

/* SENDBOAR 0x112c2: only the original 33-byte FILLSAVE body. */
void Application::sendBoard() {
    if (modem.connected() && (settings.white_player == 2 || settings.black_player == 2)) {
        uint8_t board[33];
        fill_save(&session.game.position, board);
        modem.packet(0xa1, board, sizeof board);
    }
}

/* Native new-game coordinator; RESETGAM is at file offset 0x5724.
 * Drop session history, pending presentation and the previous save association. */
void Application::resetGame() {
    computer.cancel();
    hint.reset();
    computerEnding = 0;
    animation.cancel();
    session = GameSession();
    session.computerRequested = settings.white_player == 1;
    saved = {};
    currentFile.clear();
    remotePromotion.clear();
    remotePromotionPiece = 0;
    modem.clearEnded();
}

/* Native scheduling bridge to DOWALK (file offset 0xd3bc). Start each
 * committed move once, using its saved pre-move position. */
/* DOWALK 0xd3bc / FUN0x7630 send to a modem opponent, never echo remote turns. */
void Application::sendMove(BCMove move, unsigned movingSide) {
    if (modem.connected() && (movingSide ? settings.white_player : settings.black_player) == 2 &&
        (movingSide ? settings.black_player : settings.white_player) != 2) {
        uint8_t squares[] = {uint8_t(move.to), uint8_t(move.from)};
        modem.packet(0xa2, squares, 2);
        if (move.special && move.piece != 1 && move.piece != 6)
            modem.packet(0xa9, &move.piece, 1);
    }
}
void Application::syncAnimation() {
    if (session.pending) {
        if (!session.pendingSent) {
            if (!session.promotionWalk)
                sendMove(*session.pending, session.animationBefore.side);
            session.pendingSent = true;
        }
        if (modem.busy() || animation.busy())
            return;
        animation.begin(session.animationBefore, *session.pending, flat);
        session.pending.reset();
        session.pendingSent = false;
    }
}

/* Native coordinator for DOCHECKM (file offset 0x716a). Wait for
 * presentation, animate the captured king, then show the original ending message. */
/* WAITTOEN 0x3b50: exchange AA before the terminal reset. */
void Application::sendEnding() {
    if (modem.connected() && (settings.white_player == 2 || settings.black_player == 2))
        modem.text(std::string("\xaa\x04\xca\x34", 4));
}

void Application::finishTurn() {
    if (!animation.busy() && !session.pending && !editing && !alertId && !textDialog.open &&
        session.promotion.empty() && !session.endingAnnounced) {
        if (computerEnding) {
            session.endingAnnounced = true;
            sendEnding();
            showAlert(bc_ending_message(computerEnding));
            computerEnding = 0;
            confirmAction = 3;
            return;
        }
        BCOutcome outcome = bc_adjudicate(&session.game);
        BCMove mate{};
        if (outcome == BC_CHECKMATE && !session.mateAnimationDone &&
            bc_animation_checkmate_move(&session.game, &mate)) {
            session.mateAnimationDone = true;
            animation.begin(session.game.position, mate, flat);
        } else if (outcome == BC_CHECKMATE || outcome == BC_STALEMATE) {
            session.endingAnnounced = true;
            sendEnding();
            showAlert(bc_ending_message(outcome == BC_CHECKMATE ? 0x458 : 0x468));
            confirmAction = 3;
        }
    }
}

/* Native asynchronous bridge to computer setup (0x5b7c), FINDHINT (0x6794)
 * and RETURNAN (0x5c38). Consume only complete results on the SDL thread. */
void Application::updateComputer() {
    if (modem.busy() || editing || fileWaiting || alertId || textDialog.open || animation.busy() ||
        session.pending || !session.promotion.empty() || session.endingAnnounced)
        return;
    if (auto result = computer.takeResult()) {
        if (!result->search.cancelled && result->search.has_move) {
            if (result->hint) {
                hint = result->search.move;
                hintStarted = SDL_GetTicks();
            } else {
                hint.reset();
                if (!session.apply(result->search.move, true, true))
                    throw std::runtime_error("Original computer returned an illegal move");
                session.computerRequested =
                    playerForSide(settings, session.game.position.side) == 1;
                if (settings.white_player == settings.black_player)
                    computerEnding = bc_computer_resignation(
                        &session.game, int(session.past.size()), result->search.score);
                if (computerEnding) {
                    session.pending.reset();
                    animation.cancel();
                }
            }
        }
        return;
    }
    if (!computer.busy() && session.computerRequested) {
        session.computerRequested = false;
        session.selected = -1;
        hint.reset();
        computer.start(session.game, session.past, session.bookEligible, settings.level,
                       bc_time_search_seconds(&thinkingTime), false);
    }
}

/* CNFGETCO 0x2744 and pending-board 0x2d1e: consume only validated native
 * frames; ACK move/promotion immediately, board at its commit boundary. */
void Application::updateModem() {
    bool allowed =
        !editing && !fileWaiting && (!alertId || session.endingAnnounced) && !textDialog.open;
    if (auto result =
            modem.update(allowed && (settings.white_player == 2 || settings.black_player == 2))) {
        if (quitting)
            return;
        for (const auto &line : result->messages) {
            /* READBLOC 0x1122c: original strncmp length20, white-first order. */
            if (line.compare(0, 20, "Your Opponent just quit", 20) == 0) {
                if (settings.white_player == 2)
                    settings.white_player = 0;
                else if (settings.black_player == 2)
                    settings.black_player = 0;
                remotePromotion.clear();
                remotePromotionPiece = 0;
            }
            showAlert(mac_bitmap_decode_roman(chicago_available ? chicago_glyphs : nullptr, line));
        }
        if (result->status == -2 || (!result->received && result->status < 0)) {
            showAlert("Serial communication failed.");
            return;
        }
        if (result->received && result->status > 0) {
            auto &bytes = result->bytes;
            if (bytes[0] == 0xa1 && result->status == 37) {
                Position position{};
                expand_save_board(&position, bytes.data() + 2);
                if (bc_setup_validate(&position)) {
                    showAlert("Invalid remote board.");
                    return;
                }
                calculate_piece_lists(&position);
                computer.cancel();
                animation.cancel();
                hint.reset();
                session = GameSession();
                session.bookEligible = false;
                bc_setup_commit(&session.game, &position);
                remotePromotion.clear();
                remotePromotionPiece = 0;
                session.computerRequested = playerForSide(settings, position.side) == 1;
                modem.acknowledge();
            } else if (bytes[0] == 0xa2 && result->status == 6) {
                modem.acknowledge();
                remotePromotion.clear();
                remotePromotionPiece = 0;
                if (playerForSide(settings, session.game.position.side) != 2)
                    return;
                BCMove moves[80];
                size_t count = bcGameLegalMoves(&session.game, moves);
                remotePromotion.clear();
                for (size_t i = 0; i < count; ++i)
                    if (moves[i].to == bytes[2] && moves[i].from == bytes[3])
                        remotePromotion.push_back(moves[i]);
                if (remotePromotion.size() == 1) {
                    session.apply(remotePromotion.front());
                    remotePromotion.clear();
                    session.computerRequested =
                        playerForSide(settings, session.game.position.side) == 1;
                } else if (remotePromotion.empty()) {
                    modem.text("Boards are not synchronized.\r");
                    sendBoard();
                }
            } else if (bytes[0] == 0xa9 && result->status == 5) {
                modem.acknowledge();
                if (std::any_of(remotePromotion.begin(), remotePromotion.end(),
                                [&](BCMove move) { return move.piece == bytes[2]; }))
                    remotePromotionPiece = bytes[2];
            }
        }
    }
    if (waitingForRemoteEnd &&
        (modem.ended() || (settings.white_player != 2 && settings.black_player != 2))) {
        waitingForRemoteEnd = false;
        alertId = confirmAction = 0;
        resetGame();
    }
    if (!remotePromotion.empty() && remotePromotionPiece) {
        for (auto move : remotePromotion)
            if (move.piece == remotePromotionPiece) {
                session.apply(move);
                session.computerRequested =
                    playerForSide(settings, session.game.position.side) == 1;
                break;
            }
        remotePromotion.clear();
        remotePromotionPiece = 0;
    }
}
