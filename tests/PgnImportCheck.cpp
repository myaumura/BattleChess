#include "PgnExport.h"
#include "PgnImport.h"
#include <cassert>
#include <string>
#include <vector>

namespace {
    BCGame play(const PgnImport &imported) {
        BCGame game = imported.start;
        for (const BCMove &move : imported.moves)
            assert(bcGameApply(&game, move, nullptr));
        return game;
    }

    bool contains(const std::string &text, const std::string &part) {
        return text.find(part) != std::string::npos;
    }

    void checkAnnotatedGame() {
        const std::string pgn = "\xEF\xBB\xBF"
                                "[Event \"Casual\"]\n"
                                "[White \"Anna \\\"A\\\" Smith\"]\n"
                                "[Black \"Mac\"]\n"
                                "[Result \"1-0\"]\n"
                                "% escaped line 1. h4\n"
                                "\n"
                                "1. e4 {King's pawn} e5 2. Bc4 $1 (2. Nf3 Nc6 (2... d6)) Nc6\n"
                                "3. Qh5!? Nf6?? ; missed the threat\n"
                                "4. Qxf7# 1-0\n"
                                "\n"
                                "[Event \"Second game\"]\n"
                                "1. d4 *\n";
        PgnImport imported = importPgn(pgn);
        assert(imported.error.empty());
        assert(imported.white == "Anna \"A\" Smith" && imported.black == "Mac");
        assert(imported.result == "1-0" && imported.moves.size() == 7);
        BCGame game = play(imported);
        assert(game.position.board[0x65].piece == 2 && game.position.side == 1); // Queen f7.
    }

    void checkNotationVariants() {
        // Castling with zeros, long algebraic, missing capture mark, '=' free promotion
        // and over-specified moves all resolve to the unique legal move.
        PgnImport imported = importPgn("1. e2-e4 d7d5 2. ed5 Qxd5 3. Ng1f3 Bg4 4. Bf1e2 Nc6 "
                                       "5. 0-0 0-0-0 *");
        assert(imported.error.empty());
        assert(imported.moves.size() == 10);
        BCGame game = play(imported);
        assert(game.position.board[0x06].piece == 1 && game.position.board[0x72].piece == 1);

        PgnImport promotion =
            importPgn("[SetUp \"1\"]\n[FEN \"7k/1P6/8/8/8/8/8/4K3 w - - 0 1\"]\n1. b8N *");
        assert(promotion.error.empty() && promotion.moves.size() == 1);
        assert(play(promotion).position.board[0x71].piece == 5);
        PgnImport defaultQueen = importPgn("[FEN \"7k/1P6/8/8/8/8/8/4K3 w - - 0 1\"]\n1. b8 *");
        assert(defaultQueen.error.empty() && play(defaultQueen).position.board[0x71].piece == 2);
    }

    void checkFenStartAndEnPassant() {
        // Black just played d7-d5; White captures en passant from the FEN target square.
        PgnImport imported = importPgn("[SetUp \"1\"]\n"
                                       "[FEN \"4k3/8/8/3pP3/8/8/8/4K3 w - d6 0 2\"]\n"
                                       "2. exd6 Kd7 3. Kd2 *");
        assert(imported.error.empty());
        BCGame game = play(imported);
        assert(game.position.board[0x53].piece == 6); // White pawn on d6.
        assert(game.position.board[0x43].piece == 0); // Captured d5 pawn removed.
        assert(game.position.board[0x63].piece == 1); // Black king on d7.

        PgnImport blackFirst =
            importPgn("[FEN \"4k3/8/8/8/8/8/4P3/4K3 b - - 0 1\"]\n1... Kd7 2. e4 *");
        assert(blackFirst.error.empty() && blackFirst.moves.size() == 2);
    }

    void checkRoundTripWithExporter() {
        PgnImport imported =
            importPgn("1. d4 d5 2. Nf3 Nf6 3. Nbd2 e6 4. e4 Be7 5. Bd3 O-O 6. e5 Nfd7 *");
        assert(imported.error.empty());
        std::vector<BCGame> past;
        BCGame game = imported.start;
        for (const BCMove &move : imported.moves) {
            past.push_back(game);
            assert(bcGameApply(&game, move, nullptr));
        }
        std::string exported = exportPgn(past, game, PgnTags{});
        assert(contains(exported, "1. d4 d5 2. Nf3 Nf6 3. Nbd2 e6 4. e4 Be7 5. Bd3 O-O 6. e5 "
                                  "Nfd7 *"));
        PgnImport again = importPgn(exported);
        assert(again.error.empty() && again.moves.size() == imported.moves.size());
    }

    void checkErrors() {
        assert(importPgn("1. e4 e5 2. Ke3 *").error == "Illegal move: 2. Ke3");
        assert(importPgn("1. e4 e5 2. Nf3 Nc6 3. Bb5 a6 4. Ba4 Nf6 5. Nc3 Nd4 6. Nd5 *")
                   .error.empty());
        // Two knights reach d2 from b1 and f3: "Nd2" alone is ambiguous.
        assert(importPgn("1. d4 d5 2. Nf3 Nf6 3. Nd2 *").error == "Ambiguous move: 3. Nd2");
        assert(importPgn("1. e4 {open comment").error == "Unterminated comment");
        assert(importPgn("[Variant \"Chess960\"]\n1. e4 *").error ==
               "Unsupported variant: Chess960");
        assert(!importPgn("[FEN \"8/8/8/8/8/8/8/8 w - - 0 1\"]\n*").error.empty());
    }
}

int main() {
    checkAnnotatedGame();
    checkNotationVariants();
    checkFenStartAndEnPassant();
    checkRoundTripWithExporter();
    checkErrors();
    return 0;
}
