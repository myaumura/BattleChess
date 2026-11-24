#ifndef BC_LICHESS_PROTOCOL_H
#define BC_LICHESS_PROTOCOL_H

#include "Game.h"
#include "Json.h"
#include <string>
#include <vector>

/* Native Lichess Board API protocol helpers; no original entry point. Pure functions
 * over the engine's game state so they can be checked without a network. Field names
 * follow the published OpenAPI specification (lichess-org/api, doc/specs). */

// UCI for a legal move in game: "e2e4", "e7e8q"; castling as the king's two-square move.
std::string uciMove(const BCGame &game, const BCMove &move);

/* Match a UCI move against the legal moves of game. Accepts both standard castling
 * ("e1g1") and Lichess's king-to-rook form ("e1h1"). */
bool resolveUci(const BCGame &game, const std::string &uci, BCMove &move);

/* A game rebuilt from gameFull's initialFen ("startpos" or FEN) and a space-separated
 * UCI move list. error is empty on success. */
struct LichessLine {
    BCGame start{};
    std::vector<BCMove> moves;
    std::string error;
};
LichessLine buildLichessLine(const std::string &initialFen, const std::string &moves);
std::vector<std::string> splitUciMoves(const std::string &moves);

struct LichessGameState {
    std::string moves, status, winner;
    long long wtime = 0, btime = 0, winc = 0, binc = 0; // Milliseconds.
    bool wdraw = false, bdraw = false;
};
LichessGameState parseGameState(const Json &event);

struct LichessPlayer {
    std::string name;
    int aiLevel = 0; // 1..8 for the Lichess AI, else 0.
    int rating = 0;
};

struct LichessGameFull {
    std::string id, initialFen = "startpos", speed;
    LichessPlayer white, black;
    long long clockInitial = 0, clockIncrement = 0; // Milliseconds; 0 without a clock.
    int daysPerTurn = 0;
    LichessGameState state;
};
LichessGameFull parseGameFull(const Json &event);

struct LichessChallenge {
    std::string id, challenger, speed, color, timeControl;
    bool rated = false, incoming = false;
};
LichessChallenge parseChallenge(const Json &challenge);

/* Online animation policy. Auto follows the time control and the player's own clock;
 * Fast plays quiet moves at 2x and shows captures at once. */
enum class LichessAnimationPolicy { automatic, full, fast, boardOnly };
enum class LichessMoveAnimation { full, fast, none };
LichessMoveAnimation chooseLichessAnimation(LichessAnimationPolicy policy,
                                            const std::string &speed, bool hasClock,
                                            long long ownMillis, bool capture);
const char *lichessPolicyName(LichessAnimationPolicy policy);

// Clock text: "9:58", "1:02:03"; tenths below ten seconds ("0:07.4").
std::string formatLichessClock(long long millis);

/* Final message for a finished game from the player's side ("white"/"black"), or empty
 * while the game is still running. */
std::string lichessEndMessage(const LichessGameState &state, const std::string &ownColor,
                              const std::string &opponent);
bool lichessGameOver(const std::string &status);

// Time controls offered by the Lichess menu.
struct LichessTimeControl {
    const char *label;
    int minutes, increment, days; // days > 0: correspondence.
    bool seekable;                // Board API seeks allow rapid, classical, correspondence.
};
const std::vector<LichessTimeControl> &lichessTimeControls();

// application/x-www-form-urlencoded escaping.
std::string formEscape(const std::string &value);

#endif
