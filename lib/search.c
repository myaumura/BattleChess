#include "search.h"
#include "search_moves.h"
#include "evaluation.h"
#include "adjudication.h"
#include <stdlib.h>
#include <string.h>

/* Original SEARCH frame, file offset 0x163f2: named fields follow offsets
 * 00..1e; child_line is the 0xc0-byte array at 20. Native pointers need not
 * reproduce the 68k frame's byte layout. */
typedef struct {
    int16_t alpha, beta, depth, follows_pv, previous_static, previous_result;
    BCMove *variation;
    int16_t quiescent, best, next_depth, next_follows_pv;
    int16_t move_score, result, research;
    uint8_t phase;
    BCMove child_line[24];
} SearchFrame;
typedef struct {
    BCGame game;
    BCEvaluation evaluation;
    BCPawnState pawns[25];
    SearchFrame frames[24];
    BCMove killers[24][2], line[24], variation[24];
    BCMove history[BC_GAME_HISTORY_CAPACITY + 24];
    size_t root_history_count;
    int16_t check[25], advanced_pawn[26];
    unsigned ply, depth, root_moves;
    int16_t root_score, baseline, lower_bound;
    int interrupted, cancelled;
    uint32_t random_state;
    uint64_t candidates, started;
    const BCSearchLimits *limits;
} Search;

/* Original EQMOVE, file offset 0x4c4c: compare the complete eight-byte move. */
static int equal_move(BCMove a, BCMove b) {
    return a.to == b.to && a.from == b.from && a.special == b.special && a.piece == b.piece &&
           a.captured == b.captured;
}
/* Original FIFTYMOV, file offset 0xc670: include hypothetical search moves
 * without losing the oldest entries from the original 103-move history. */
static int fifty_moves(const Search *search) {
    size_t count = search->root_history_count + search->ply + 1;
    int result = 0;
    while (count && bc_repeat_move(search->history[--count]))
        ++result;
    return result;
}
/* Original REPETITI, file offset 0xc6bc: the same move-chain traversal as
 * adjudication.c, over original history plus search slots 104..127. */
static int repetitions(const Search *search, int search_only) {
    const BCMove *history = search->history;
    int repeats = 1, end = (int)search->root_history_count + (int)search->ply + 1;
    int boundary = end - 4, lower = end;
    while (lower > 0 && (!search_only || boundary < lower) && bc_repeat_move(history[lower - 1]))
        --lower;
    if (boundary < lower)
        return repeats;
    int current = end;
    for (;;) {
        --current;
        unsigned destination = history[current].to;
        for (int later = current + 2; later < end; later += 2)
            if (history[later].from == destination)
                goto next_piece;
        int earlier = current;
        unsigned source = history[current].from;
        do {
            if (earlier - 2 < lower)
                return repeats;
            earlier -= 2;
            if (source == history[earlier].to)
                source = history[earlier].from;
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
/* Original stop gate, file offset 0x16246, with native monotonic clock.
 * Explicit host cancellation replaces processing Macintosh menu commands. */
static int budget_expired(Search *search) {
    const BCSearchLimits *limits = search->limits;
    return limits->milliseconds && limits->seconds > 0 &&
           limits->milliseconds(limits->context) - search->started >=
               (uint64_t)limits->seconds * 1000;
}
/* Original SEARCHLO event checks, file offsets 0x16b28..0x16bd6: preserve
 * the first iteration's timeout exemption; explicit native cancel is immediate. */
static void poll_host(Search *search) {
    int command = search->limits->poll ? search->limits->poll(search->limits->context) : 0;
    if (command < 0) {
        search->cancelled = search->interrupted = 1;
        return;
    }
    if (command > 0 || (search->depth >= 2 && budget_expired(search)))
        search->interrupted = 1;
}
/* Original SLBUC, file offset 0x16e4c: retain a rejected static score as best. */
static int static_cutoff(SearchFrame *frame, int16_t score) {
    if (score > frame->alpha)
        return 0;
    if (frame->best < score)
        frame->best = score;
    return 1;
}
/* Original SEARCHUP, file offset 0x16c86: keep the two original killer slots,
 * excluding a move that recaptures on the previous move's destination. */
static void update_killers(Search *search, BCMove move) {
    BCMove previous =
        search->ply ? search->line[search->ply - 1]
                    : (search->root_history_count ? search->history[search->root_history_count - 1]
                                                  : (BCMove){0});
    BCMove *killers = search->killers[search->ply];
    if (!move.piece || (previous.piece && move.to == previous.to))
        return;
    if (killers[0].piece && !equal_move(move, killers[1])) {
        if (!equal_move(move, killers[0]))
            killers[1] = move;
        return;
    }
    killers[1] = killers[0];
    killers[0] = move;
}
/* Original unnamed PV update, file offset 0x17482: copy the child line and
 * current move, retaining the mate-search root bound replacement. */
static void update_variation(Search *search, SearchFrame *frame, BCMove move) {
    memcpy(frame->variation, frame->child_line, sizeof(frame->child_line));
    frame->variation[search->ply] = move;
    if (!search->ply) {
        search->root_score = frame->best;
        if (search->limits->mate_search)
            frame->best = search->lower_bound;
    }
}
/* Original SLBDG, file offset 0x1738e: repetition cutoffs and the root-score
 * bias for reversible-move and repeated-position sequences. */
static int draw_gate(Search *search, SearchFrame *frame) {
    if (search->ply == 1) {
        int fifty = fifty_moves(search), repeated = repetitions(search, 0);
        if (repeated > 2) {
            frame->result = 0;
            return 1;
        }
        int weight = fifty >= 96 ? 3 : repeated >= 2 ? 2 : fifty > 19 ? 1 : 0;
        int adjustment = (search->baseline / 4) * weight;
        frame->move_score = (int16_t)(frame->move_score + adjustment);
        frame->result = (int16_t)(frame->result + adjustment);
    }
    if (search->ply > 2 && repetitions(search, 1) > 1) {
        frame->result = 0;
        return 1;
    }
    return 0;
}

static int16_t search_node(Search *, int16_t, int16_t, int16_t, int16_t, int16_t, int16_t,
                           BCMove *);
/* Original SLBU, file offset 0x16e8e: selective static pruning, extensions,
 * legality and root-only random perturbation, before recursive SEARCH. */
static int prepare_move(Search *search, SearchFrame *frame, BCMove move) {
    unsigned ply = search->ply;
    Position *position = &search->game.position;
    unsigned side = position->side, opponent = position->opponent;
    frame->next_depth = (int16_t)(frame->depth - 1);
    if (!search->limits->mate_search) {
        if (search->depth < 2 && frame->quiescent && ply >= 2 &&
            move.from != search->line[ply - 2].to && frame->phase != 1 &&
            move.piece <= move.captured)
            return 0;
        frame->move_score =
            (int16_t)(bc_evaluation_delta(&search->evaluation, &search->pawns[ply + 1],
                                          &search->pawns[ply], side, move, search->root_score) -
                      frame->previous_static);
        search->check[ply + 1] = (int16_t)bcGamePieceAttacks(
            position, move.piece, side, move.to, position->pieces[opponent][0].square);
        if (search->check[ply + 1])
            frame->next_depth = frame->depth;
        search->advanced_pawn[ply + 2] = search->advanced_pawn[ply];
        if (move.piece == 6 && (move.to > 95 || move.to < 24)) {
            search->advanced_pawn[ply + 2] = (int16_t)move.to;
            frame->next_depth = frame->depth;
        }
        if (ply && !search->check[ply + 1] && frame->next_depth < 1 &&
            static_cutoff(frame, frame->move_score))
            return 0;
    }
    if (!bcGameSearchApply(&search->game, move))
        return 0;
    search->history[search->root_history_count + ply] = move;
    if (search->limits->mate_search) {
        if (!ply)
            ++search->root_moves;
        search->check[ply + 1] = 0;
        search->advanced_pawn[ply + 2] = -1;
        frame->move_score = frame->result = 0;
        if (frame->next_depth < 1) {
            if (!frame->next_depth)
                search->check[ply + 1] = (int16_t)bcGameInCheck(&search->game);
            if (!search->check[ply + 1] && static_cutoff(frame, frame->move_score))
                return 0;
        }
    } else {
        int pawn = search->advanced_pawn[ply + 2];
        if (pawn >= 0 && (search->game.position.board[pawn].side != side ||
                          search->game.position.board[pawn].piece != 6))
            search->advanced_pawn[ply + 2] = -1;
        if (!ply) {
            ++search->root_moves;
            search->random_state = search->random_state * 0x41c64e6du + 0x3039u;
            frame->move_score = (int16_t)(frame->move_score + ((search->random_state >> 16) & 7));
        }
        frame->result = frame->move_score;
    }
    return 1;
}
/* Original SEARCHLO, file offset 0x168d0, including SLBGB at 0x16d82:
 * skip duplicate phases, search the selected window and re-search improvements. */
static int visit_move(void *context, BCMove move) {
    Search *search = context;
    unsigned ply = search->ply;
    SearchFrame *frame = &search->frames[ply];
    ++search->candidates;
    if (frame->phase) {
        if (equal_move(move, frame->variation[ply]))
            return 0;
        if (!frame->quiescent && frame->phase != 2)
            for (unsigned i = 0; i < 2; ++i)
                if (equal_move(move, search->killers[ply][i]))
                    return 0;
    }
    search->line[ply] = move;
    if (ply < 23) {
        frame->child_line[ply + 1] = (BCMove){0};
        if (!frame->phase)
            memcpy(frame->child_line, frame->variation, sizeof(frame->child_line));
    }
    frame->next_follows_pv = frame->research = 0;
    if (frame->follows_pv) {
        if (!frame->phase)
            frame->next_follows_pv = ply < 23 && frame->variation[ply + 1].piece != 0;
        else
            frame->research = frame->alpha <= frame->best;
    }
    BCGame snapshot = search->game;
    for (;;) {
        if (!prepare_move(search, frame, move)) {
            search->game = snapshot;
            return 0;
        }
        if ((!search->limits->mate_search || search->check[ply + 1] || frame->next_depth > 0) &&
            !draw_gate(search, frame) && ply < 23) {
            ++search->ply;
            int16_t lower = (int16_t)(frame->research ? -1 - frame->alpha : -frame->beta);
            frame->result = (int16_t)-search_node(
                search, lower, (int16_t)-frame->alpha, frame->next_depth, frame->next_follows_pv,
                frame->move_score, frame->result, frame->child_line);
            --search->ply;
        }
        search->game = snapshot;
        if (search->interrupted)
            return 1;
        poll_host(search);
        if (frame->best < frame->result)
            frame->best = frame->result;
        if (equal_move(frame->variation[ply], move))
            update_variation(search, frame, move);
        if (frame->best <= frame->alpha)
            return search->interrupted;
        update_variation(search, frame, move);
        if (frame->best >= frame->beta)
            return 1;
        if (frame->depth > 1 && frame->follows_pv && !frame->research) {
            int score = frame->best + 4;
            frame->best = (int16_t)(score < frame->beta - 1 ? score : frame->beta - 1);
        }
        frame->alpha = frame->best;
        if (!frame->research || search->interrupted)
            return search->interrupted;
        frame->research = 0;
    }
}
/* Original SEARCHSE, file offset 0x16592: retain all seven generation phases
 * and their original list directions instead of sorting generic legal moves. */
static void search_sequence(Search *search) {
    unsigned ply = search->ply;
    SearchFrame *frame = &search->frames[ply];
    Position *position = &search->game.position;
    BCMove previous =
        ply ? search->line[ply - 1]
            : (search->root_history_count ? search->history[search->root_history_count - 1]
                                          : (BCMove){0});
    if (frame->variation[ply].piece) {
        frame->phase = 0;
        if (visit_move(search, frame->variation[ply]))
            return;
    }
    if (previous.piece && previous.piece != 1) {
        frame->phase = 1;
        if (bc_search_captures_to(&search->game, previous.to, visit_move, search))
            return;
    }
    frame->phase = 2;
    if (!frame->quiescent)
        for (unsigned i = 0; i < 2; ++i) {
            BCMove move = search->killers[ply][i];
            if (move.piece && bcGameKillerMoveValid(&search->game, move) &&
                visit_move(search, move))
                return;
        }
    frame->phase = 3;
    for (int i = 1; i <= position->last_piece[position->opponent]; ++i) {
        PieceEntry piece = position->pieces[position->opponent][i];
        if (piece.piece && (piece.square != previous.to || !previous.piece) &&
            bc_search_captures_to(&search->game, piece.square, visit_move, search))
            return;
    }
    int pawn = search->advanced_pawn[ply];
    if (frame->quiescent && pawn >= 0 && position->board[pawn].side == position->side &&
        position->board[pawn].piece == 6 &&
        bc_search_quiet_from(&search->game, (unsigned)pawn, visit_move, search))
        return;
    if (!frame->quiescent) {
        if (bc_search_castles(&search->game, visit_move, search))
            return;
        for (int i = position->last_piece[position->side]; i >= 0; --i) {
            PieceEntry piece = position->pieces[position->side][i];
            if (piece.piece &&
                bc_search_quiet_from(&search->game, piece.square, visit_move, search))
                return;
        }
    }
    bc_search_en_passant(&search->game, visit_move, search);
}
/* Original SEARCH, file offset 0x163f2: stand-pat, mate/stalemate scores,
 * selective generation and killer update; original depth ceiling is 24 plies. */
static int16_t search_node(Search *search, int16_t alpha, int16_t beta, int16_t depth,
                           int16_t follows_pv, int16_t previous_static, int16_t previous_result,
                           BCMove *variation) {
    unsigned ply = search->ply;
    SearchFrame *frame = &search->frames[ply];
    if (ply)
        *frame = search->frames[ply - 1];
    frame->alpha = alpha;
    frame->beta = beta;
    frame->depth = depth;
    frame->follows_pv = follows_pv;
    frame->previous_static = previous_static;
    frame->previous_result = previous_result;
    frame->variation = variation;
    frame->quiescent = depth <= 0 && !search->check[ply];
    int16_t mate = (int16_t)(-32000 + (int)ply * 128);
    frame->best = frame->quiescent ? (int16_t)-previous_result : mate;
    if (frame->quiescent && frame->alpha < frame->best) {
        frame->alpha = frame->best;
        if (frame->best >= frame->beta)
            return frame->best;
    }
    search_sequence(search);
    if (!search->interrupted) {
        if (frame->best == mate && !bcGameInCheck(&search->game))
            frame->best = 0;
        else
            update_killers(search, variation[ply]);
    }
    return frame->best;
}
/* Original CALLSEAR, file offset 0x16376: search the current root depth and
 * keep a terminal score when the root produced no legal candidates. */
static int16_t call_search(Search *search, int16_t alpha, int16_t beta) {
    search->root_moves = 0;
    int16_t initial = (int16_t)-search->evaluation.root_score;
    int16_t score =
        search_node(search, alpha, beta, (int16_t)search->depth, search->variation[0].piece != 0,
                    initial, initial, search->variation);
    if (!search->root_moves)
        search->root_score = score;
    return score;
}
/* Original FINDMOVE/CLEARKIL, file offsets 0x160b4/0x16280: fixed-root
 * evaluation, iterative aspiration windows, mate-depth steps and stop tests.
 * Native adaptation returns the result instead of entering the Macintosh UI wait loop. */
int bc_search_find(const BCGame *game, const BCSearchLimits *limits, uint32_t *random_state,
                   BCSearchResult *result) {
    if (!game || !limits || !random_state || !result || !limits->max_depth ||
        limits->max_depth > 23 || game->historyCount > BC_GAME_HISTORY_CAPACITY)
        return 0;
    Search search = {0};
    search.game = *game;
    search.limits = limits;
    search.random_state = *random_state;
    search.root_history_count = game->historyCount;
    memcpy(search.history, game->history, game->historyCount * sizeof(BCMove));
    search.started = limits->milliseconds ? limits->milliseconds(limits->context) : 0;
    bc_evaluation_init(&search.evaluation, game, game->position.side);
    if (limits->session)
        memcpy(search.pawns, limits->session->pawns, sizeof(search.pawns));
    search.pawns[0] = search.evaluation.root_pawns;
    search.root_score = search.evaluation.root_score;
    search.lower_bound = 32767;
    search.advanced_pawn[0] = search.advanced_pawn[1] = -1;
    for (unsigned side = 0; side < 2; ++side)
        for (unsigned square = side ? 16 : 96; square < (side ? 24u : 104u); ++square)
            if (game->position.board[square].piece == 6 &&
                game->position.board[square].side == side)
                search.advanced_pawn[side == game->position.side ? 0 : 1] = (int16_t)square;
    do {
        if (search.depth < 2)
            search.baseline = search.root_score;
        int16_t next_bound = (int16_t)(search.root_score - 128);
        if (search.lower_bound > next_bound)
            search.lower_bound = next_bound;
        if (limits->mate_search) {
            search.lower_bound = 0x6000;
            if (search.depth)
                ++search.depth;
        }
        ++search.depth;
        int16_t score = call_search(&search, search.lower_bound, 0x7f00);
        if (score <= search.lower_bound && !search.interrupted && !limits->mate_search &&
            search.root_moves) {
            search.root_score = search.lower_bound;
            call_search(&search, (int16_t)0x8100, (int16_t)(search.lower_bound - 8));
            search.root_moves = 2;
        }
    } while (!search.interrupted && search.depth < limits->max_depth && search.root_moves > 1 &&
             abs(search.root_score) < 0x7080 && !budget_expired(&search));
    if (limits->session)
        memcpy(limits->session->pawns, search.pawns, sizeof(search.pawns));
    *random_state = search.random_state;
    *result = (BCSearchResult){0};
    result->move = search.variation[0];
    memcpy(result->variation, search.variation, sizeof(result->variation));
    result->score = search.root_score;
    result->depth = search.depth;
    result->candidates = search.candidates;
    result->has_move = result->move.piece != 0;
    result->cancelled = search.cancelled;
    result->interrupted = search.interrupted;
    return 1;
}
