#include "recovered_core.h"
#include <cassert>
#include <cstring>
#include <cstdio>
// Native validation (no original entrypoint): Run the board check regression assertions.
int main() {
    Position p{};
    reset_board(&p);
    const unsigned back[] = {3, 5, 4, 2, 1, 4, 5, 3};
    assert(p.side == 0 && p.opponent == 1);
    for (unsigned f = 0; f < 8; ++f) {
        assert(p.board[f].piece == back[f] && p.board[f].side == 0);
        assert(p.board[0x70 + f].piece == back[f] && p.board[0x70 + f].side == 1);
        assert(p.board[0x10 + f].piece == 6 && p.board[0x10 + f].side == 0);
        assert(p.board[0x60 + f].piece == 6 && p.board[0x60 + f].side == 1);
    }
    for (unsigned r = 2; r < 6; ++r)
        for (unsigned f = 0; f < 8; ++f)
            assert(p.board[r * 16 + f].piece == 0);
    for (unsigned side = 0; side < 2; ++side) {
        assert(p.last_piece[side] == 15 && p.last_nonpawn[side] == 7);
        for (int i = 0; i < 16; ++i) {
            const auto e = p.pieces[side][i];
            assert(p.board[e.square].list_index == i && p.board[e.square].side == side);
            assert(e.piece == p.board[e.square].piece);
        }
    }
    const auto before = p;
    assert(!insert_piece(&p, 7, 0, 0) && !insert_piece(&p, 1, 2, 0));
    assert(!insert_piece(&p, 1, 0, 0x08) && !insert_piece(&p, 1, 0, 0x80));
    assert(std::memcmp(&p, &before, sizeof p) == 0);
    p.board[0x22].list_index = 11;
    assert(insert_piece(&p, 5, 1, 0x22));
    assert(p.board[0x22].piece == 5 && p.board[0x22].side == 1 && p.board[0x22].list_index == 11);
    reset_board(&p);
    assert(std::memcmp(&p, &before, sizeof p) == 0);
    uint8_t snapshot[33], again[33], display[64], rows[8];
    fill_save(&p, snapshot);
    Position q{};
    expand_save_board(&q, snapshot);
    calculate_piece_lists(&q);
    fill_save(&q, again);
    assert(std::memcmp(snapshot, again, 33) == 0);
    setup_display_board(&q, display);
    available_to_move(display, 0, rows);
    const unsigned display_back[] = {6, 3, 1, 5, 2, 1, 3, 6};
    for (unsigned i = 0; i < 8; ++i)
        assert((display[56 + i] & 7) == display_back[i]);
    assert(rows[6] == 255 && rows[7] == 255 && rows[0] == 0);
    puts("board recovery: reset, insertion, lists and snapshot checks passed");
}
