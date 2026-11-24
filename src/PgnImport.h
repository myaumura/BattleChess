#ifndef BC_PGN_IMPORT_H
#define BC_PGN_IMPORT_H

#include "Game.h"
#include <string>
#include <vector>

/* Native PGN import; no original entry point. Reads the first game of a PGN text:
 * the optional SetUp/FEN start position and the main line. Comments, variations,
 * annotation glyphs and move numbers are skipped. */
struct PgnImport
{
    BCGame start{};
    std::vector<BCMove> moves; // Legal moves, in order, starting from start.
    std::string white, black, result;
    std::string error; // Empty on success.
};

PgnImport importPgn(const std::string& text);

/* Parse a FEN position into a committed game. Castling rights follow the engine's
 * own piece-history rule; an en passant target becomes the matching last pawn move. */
bool parseFen(const std::string& fen, BCGame& game, std::string& error);

/* Resolve one SAN (or long algebraic) token against the legal moves of game. */
bool resolveMove(const BCGame& game, const std::string& token, BCMove& move, std::string& error);

#endif
