#ifndef BC_PGN_EXPORT_H
#define BC_PGN_EXPORT_H

#include "Game.h"
#include <string>
#include <vector>

/* Native PGN export; no original entry point. The original save format stores
 * only the current position, so the movetext comes from the session snapshots. */
struct PgnTags {
    std::string event = "Battle Chess game";
    std::string site = "BattleChess";
    std::string date = "????.??.??";
    std::string white = "?";
    std::string black = "?";
};

/* past holds the game before each committed move, oldest first; current is the
 * game after the last move. Returns an empty string if a snapshot does not follow
 * from its predecessor by one legal move. */
std::string exportPgn(const std::vector<BCGame> &past, const BCGame &current,
                      const PgnTags &tags);

/* Find the single legal move that turns before into after (placement and side to move). */
bool recoverMove(const BCGame &before, const BCGame &after, BCMove &move);

/* Standard Algebraic Notation for a legal move in game, including check suffixes. */
std::string sanMove(const BCGame &game, const BCMove &move);

/* Forsyth-Edwards Notation for the position; clocks are not tracked by the engine. */
std::string fenPosition(const BCGame &game);

#endif
