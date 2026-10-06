#include "GameSession.h"
#include "presentation.h"

/* Native session initializer; no direct original entry point. Start from
 * RESETGAM (file offset 0x5724) with empty undo/presentation state. */
GameSession::GameSession() {
    bcGameInit(&game);
}

/* Native move coordinator; related to CHECKOPT at file offset 0x58bc.
 * Keep an undo snapshot and stage presentation only after a legal move commits. */
bool GameSession::apply(BCMove move, bool animate, bool computerMove) {
    BCGame before = game;
    bool committed = computerMove ? bcGameCommitSearchMove(&game, move)
                                  : bcGameApply(&game, move, nullptr);
    if (committed) {
        computerRequested = false;
        endingAnnounced = false;
        past.push_back(before);
        future.clear();
        selected = -1;
        animationBefore = before.position;
        if (animate)
            pending = move;
    }
    promotion.clear();
    promotionWalk = false;
    return committed;
}

/* Native input adapter for CHECKOPT (file offset 0x58bc). Resolve source
 * and destination; WALKIFPR (0x75c4) travels as a pawn before PAWNSELE. */
void GameSession::click(int display) {
    if (display < 0)
        return;
    int square = display_to_engine(display);
    if (selected >= 0) {
        BCMove moves[80];
        auto moveCount = bcGameLegalMoves(&game, moves);
        std::vector<BCMove> matching;
        for (size_t i = 0; i < moveCount; ++i)
            if (moves[i].from == selected && moves[i].to == square)
                matching.push_back(moves[i]);
        if (matching.size() == 1) {
            apply(matching[0]);
            return;
        }
        if (matching.size() > 1) {
            selected = -1;
            promotion = matching;
            animationBefore = game.position;
            BCMove pawn = matching[0];
            pawn.piece = 6;
            pawn.special = 0;
            pending = pawn;
            promotionWalk = true;
            return;
        }
    }
    auto piece = game.position.board[square];
    selected = piece.piece && piece.side == game.position.side ? square : -1;
}

/* Native snapshot adapter; no one-to-one original address. Restore the
 * previous position through the original quick-placement behavior, without animation. */
void GameSession::undo() {
    if (past.empty())
        return;
    computerRequested = false;
    future.push_back(game);
    game = past.back();
    past.pop_back();
    selected = -1;
    promotion.clear();
    endingAnnounced = false;
}

/* Native snapshot adapter; no one-to-one original address. Reapply a
 * position undone in this session without generating a new move or animation. */
void GameSession::replay() {
    if (future.empty())
        return;
    computerRequested = false;
    past.push_back(game);
    game = future.back();
    future.pop_back();
    selected = -1;
    endingAnnounced = false;
}
