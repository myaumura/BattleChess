#ifndef BC_GAME_SESSION_H
#define BC_GAME_SESSION_H

#include "game.h"
#include <optional>
#include <vector>

/* Native session: committed positions, undo/replay history and pending presentation. */
struct GameSession {
    BCGame game {};
    std::vector<BCGame> past, future;
    std::vector<BCMove> promotion;
    int selected = -1;
    bool mateAnimationDone = false;
    bool endingAnnounced = false;
    bool promotionWalk = false;
    bool computerRequested = false;
    bool bookEligible = true;
    bool pendingSent = false;
    std::optional<BCMove> pending;
    Position animationBefore {};
    GameSession();
    bool apply(BCMove move, bool animate = true, bool computerMove = false);
    void click(int display);
    void undo();
    void replay();
};

#endif
