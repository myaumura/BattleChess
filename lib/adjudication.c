#include "adjudication.h"

/* Original: REPEATMO, file offset 0xc60c.
 * Purpose: Identify moves that do not break the original repetition chain. */
int bc_repeat_move(BCMove move) {
    return !move.special && !move.captured && move.piece != 6 && move.piece != 0;
}

/* Original: FIFTYMOV, file offset 0xc670.
 * Purpose: Count consecutive reversible moves in retained history. */
int bc_fifty_moves(const BCGame *game) {
    int count = 0;
    for (size_t i = game->historyCount; i && bc_repeat_move(game->history[i - 1]); --i)
        ++count;
    return count;
}

/* C6BC..C8EA: coordinates are shifted so original current history slot is
 * history[count-1]. The zero-piece sentinel is represented by lower == 0.
 * Follow each piece's source backwards through same-side moves; do not replace
 * this original move-chain test with a modern position/castling-rights hash. */
/* Original: REPETITI, file offset 0xc6bc.
 * Purpose: Count repeated move chains with the original boundary rules. */
int bc_repetitions(const BCGame *game, int search_only) {
    int repeats = 1, end = (int)game->historyCount, boundary = end - 4;
    int lower = end;
    while (lower > 0 && (!search_only || boundary < lower) &&
           bc_repeat_move(game->history[lower - 1]))
        --lower;
    if (boundary < lower)
        return repeats;
    int current = end;
    for (;;) {
        --current;
        unsigned destination = game->history[current].to;
        for (int later = current + 2; later < end; later += 2)
            if (game->history[later].from == destination)
                goto next_piece;
        int earlier = current;
        unsigned source = game->history[current].from;
        do {
            if (earlier - 2 < lower)
                return repeats;
            earlier -= 2;
            if (source == game->history[earlier].to)
                source = game->history[earlier].from;
        } while (source != destination || boundary + 1 < earlier);
        if (earlier < boundary) {
            boundary = earlier;
            if ((end - boundary) & 1) {
                if (boundary == lower)
                    return repeats;
                --boundary;
            }
            current = end;
        }
    next_piece:
        if (current <= boundary) {
            ++repeats;
            if (boundary - 2 < lower)
                return repeats;
            end = boundary;
            boundary -= 2;
            current = end;
        }
    }
}

/* Native helper (no direct original address).
 * Purpose: Classify legal-move availability and check; combines FUN_00007128 (file offset 0x7128)
 * and DOCHECKM (0x716a). */
BCOutcome bc_adjudicate(const BCGame *game) {
    BCMove moves[BC_GAME_MOVE_CAPACITY];
    int check = bcGameInCheck(game);
    if (!bcGameLegalMoves(game, moves))
        return check ? BC_CHECKMATE : BC_STALEMATE;
    return check ? BC_CHECK : BC_ONGOING;
}

/* Original: RETURNAN, file offset 0x5c38.
 * Purpose: Apply its equal-player-mode resignation gate after a computer move. */
unsigned bc_computer_resignation(const BCGame *game, int total_plies, int score) {
    if (total_plies < 120 && bc_fifty_moves(game) < 100 && bc_repetitions(game, 0) < 3 &&
        score > -0x880)
        return 0;
    return game->position.side == 0 ? 0x282 : 0x2a0;
}

/* Native helper (no direct original address).
 * Purpose: Resolve recovered ending-message offsets to text; see ENDINGS_EVIDENCE.md. */
const char *bc_ending_message(unsigned offset) {
    switch (offset) {
    case 0x282:
        return "Black resigns";
    case 0x2a0:
        return "White resigns";
    case 0x458:
        return "Check and mate.";
    case 0x468:
        return "Stalemate. How boring!";
    default:
        return "";
    }
}
