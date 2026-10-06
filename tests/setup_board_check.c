#include "setup_board.h"
#include <assert.h>
#include <string.h>
// Native validation (no original entrypoint): Run the setup board check regression assertions.
int main(void) {
    Position p = {0};
    reset_board(&p);
    assert(!bc_setup_validate(&p));
    BCGame g = {0};
    g.historyCount = 4;
    assert(bc_setup_commit(&g, &p) && g.historyCount == 0);
    p.board[4].piece = 0;
    assert(strcmp(bc_setup_validate(&p), "White needs a King.") == 0);
    memset(&p, 0, sizeof p);
    insert_piece(&p, 1, 0, 0);
    insert_piece(&p, 1, 1, 0x11);
    assert(strcmp(bc_setup_validate(&p), "Kings are adjacent.") == 0);
    p.board[0x11].piece = 0;
    insert_piece(&p, 1, 1, 0x77);
    insert_piece(&p, 6, 0, 3);
    assert(strcmp(bc_setup_validate(&p), "Pawns in final rank.") == 0);
    p.board[3].piece = 0;
    insert_piece(&p, 3, 0, 7);
    assert(strcmp(bc_setup_validate(&p), "Black is in Check.") == 0);
    p.side = 1;
    assert(!bc_setup_validate(&p)); /* side to move may itself be checked */
    insert_piece(&p, 2, 0, 0x30);
    for (unsigned i = 0x10; i < 0x30; ++i)
        if (!(i & 0x88))
            insert_piece(&p, 6, 0, i);
    assert(strcmp(bc_setup_validate(&p), "White has too many pieces.") == 0);
    return 0;
}
