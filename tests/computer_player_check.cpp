#include "computer_player.h"
#include <cassert>
#include <chrono>
#include <cstring>
#include <thread>

/* Native integration check; no original address. Bound asynchronous waits so
 * failed cancellation or result publication fails visibly instead of hanging. */
static ComputerResult finish(ComputerPlayer &player) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
    while (std::chrono::steady_clock::now() < deadline) {
        if (auto result = player.take_result())
            return *result;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    assert(false && "Computer worker timed out");
    return {};
}

/* Native integration check for computer setup 0x5b7c and FINDHINT 0x6794.
 * Exercise book, original search, history, hints, force and session cancellation. */
int main() {
    BCGame game;
    bc_game_init(&game);
    const BCGame initial = game;
    std::vector<BCGame> past;
    uint32_t random_state = 1;
    ComputerPlayer player(random_state);
    player.start(game, past, true, 1, 3, false);
    auto book = finish(player);
    assert(random_state == 1u * 0x41c64e6du + 0x3039u);
    assert(book.search.has_move && book.search.depth == 0 && !book.hint);
    assert(!std::memcmp(&game, &initial, sizeof game));
    past.push_back(game);
    assert(bc_game_commit_search_move(&game, book.search.move));
    player.start(game, past, true, 1, 3, false);
    auto reply = finish(player);
    assert(reply.search.has_move && reply.search.depth == 0);
    assert(bc_game_commit_search_move(&game, reply.search.move));
    player.start(initial, {}, true, 0, 3, false);
    auto novice = finish(player);
    assert(novice.search.has_move && novice.search.depth == 2);
    player.start(initial, {}, false, 1, 3, true);
    auto hint = finish(player);
    assert(hint.hint && hint.search.has_move && hint.search.depth == 2);
    player.start(initial, {}, false, 9, 1280, false);
    player.force();
    auto forced = finish(player);
    assert(forced.search.has_move && forced.search.interrupted && !forced.search.cancelled);
    player.start(initial, {}, false, 9, 1280, false);
    player.cancel();
    assert(!player.busy() && !player.take_result());
    player.start(initial, {}, true, 1, 3, true);
    assert(finish(player).hint);
    game = initial;
    past.clear();
    for (unsigned ply = 0; ply < 24; ++ply) {
        player.start(game, past, false, 0, 3, false);
        auto turn = finish(player);
        if (!turn.search.has_move)
            break;
        BCMove legal[BC_GAME_MOVE_CAPACITY];
        size_t count = bc_game_legal_moves(&game, legal);
        bool found = false;
        for (size_t i = 0; i < count; ++i)
            found |= legal[i].from == turn.search.move.from && legal[i].to == turn.search.move.to &&
                     legal[i].special == turn.search.move.special &&
                     legal[i].piece == turn.search.move.piece &&
                     legal[i].captured == turn.search.move.captured;
        assert(found);
        past.push_back(game);
        assert(bc_game_commit_search_move(&game, turn.search.move));
    }
    assert(past.size() >= 8);
}
