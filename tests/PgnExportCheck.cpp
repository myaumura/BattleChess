#include "PgnExport.h"
#include "setup_board.h"
#include <cassert>
#include <cstring>
#include <string>
#include <vector>

namespace {
    struct Recorder {
        std::vector<BCGame> past;
        BCGame game{};

        Recorder() {
            bcGameInit(&game);
        }

        /* Coordinate move such as "e2e4" or "b7b8n"; returns its SAN before committing. */
        std::string play(const char *text) {
            int from = calc_square(text[0], text[1]), to = calc_square(text[2], text[3]);
            uint8_t promotion = 0;
            if (text[4])
                promotion = uint8_t(std::strchr(" kqrbn", text[4]) - " kqrbn");
            BCMove moves[BC_GAME_MOVE_CAPACITY];
            size_t count = bcGameLegalMoves(&game, moves);
            for (size_t i = 0; i < count; ++i) {
                const BCMove &move = moves[i];
                bool promotes = (move.special & 1) && move.piece >= 2 && move.piece <= 5;
                if (move.from != from || move.to != to || (promotion && move.piece != promotion) ||
                    (!promotion && promotes))
                    continue;
                std::string san = sanMove(game, move);
                BCGame before = game;
                assert(bcGameApply(&game, move, nullptr));
                past.push_back(before);
                return san;
            }
            assert(!"illegal test move");
            return {};
        }

        std::string pgn() const {
            PgnTags tags;
            tags.date = "2026.11.23";
            tags.white = "Human";
            tags.black = "Mac";
            return exportPgn(past, game, tags);
        }
    };

    bool contains(const std::string &text, const std::string &part) {
        return text.find(part) != std::string::npos;
    }

    void checkQuickMate() {
        Recorder r;
        for (const char *move : {"e2e4", "e7e5", "f1c4", "b8c6", "d1h5", "g8f6"})
            r.play(move);
        assert(r.play("h5f7") == "Qxf7#");
        std::string pgn = r.pgn();
        assert(contains(pgn, "[Result \"1-0\"]"));
        assert(contains(pgn, "[White \"Human\"]"));
        assert(!contains(pgn, "[FEN"));
        assert(contains(pgn, "\n\n1. e4 e5 2. Bc4 Nc6 3. Qh5 Nf6 4. Qxf7# 1-0\n"));
    }

    void checkDisambiguationAndCastling() {
        Recorder r;
        for (const char *move : {"d2d4", "d7d5", "g1f3", "g8f6"})
            r.play(move);
        assert(r.play("b1d2") == "Nbd2");
        for (const char *move : {"e7e6", "e2e4", "f8e7", "f1d3"})
            r.play(move);
        assert(r.play("e8g8") == "O-O");
        assert(contains(r.pgn(), "5. Bd3 O-O *\n"));
    }

    void checkEnPassant() {
        Recorder r;
        for (const char *move : {"e2e4", "a7a6", "e4e5", "d7d5"})
            r.play(move);
        assert(r.play("e5d6") == "exd6");
    }

    void checkSetupPromotionAndFen() {
        Position position{};
        insert_piece(&position, 1, 0, 0x04); // White king e1.
        insert_piece(&position, 6, 0, 0x61); // White pawn b7.
        insert_piece(&position, 1, 1, 0x77); // Black king h8.
        calculate_piece_lists(&position);
        position.side = 0;
        position.opponent = 1;
        Recorder r;
        assert(bc_setup_commit(&r.game, &position));
        assert(fenPosition(r.game) == "7k/1P6/8/8/8/8/8/4K3 w - - 0 1");
        Recorder knight = r;
        assert(r.play("b7b8q") == "b8=Q+");
        assert(knight.play("b7b8n") == "b8=N");
        std::string pgn = r.pgn();
        assert(contains(pgn, "[SetUp \"1\"]"));
        assert(contains(pgn, "[FEN \"7k/1P6/8/8/8/8/8/4K3 w - - 0 1\"]"));
        assert(contains(pgn, "1. b8=Q+ *\n"));
    }

    void checkBlackStartsNumbering() {
        Recorder r;
        r.play("e2e4");
        Recorder fromBlack;
        fromBlack.game = r.game;
        fromBlack.play("e7e5");
        fromBlack.play("g1f3");
        assert(contains(fromBlack.pgn(), "1... e5 2. Nf3 *\n"));
    }

    void checkBrokenHistoryIsRejected() {
        Recorder r;
        r.play("e2e4");
        BCGame unrelated;
        bcGameInit(&unrelated);
        assert(exportPgn(r.past, unrelated, PgnTags{}).empty());
    }
}

int main() {
    checkQuickMate();
    checkDisambiguationAndCastling();
    checkEnPassant();
    checkSetupPromotionAndFen();
    checkBlackStartsNumbering();
    checkBrokenHistoryIsRejected();
    return 0;
}
