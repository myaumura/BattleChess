#include "PgnExport.h"
#include "adjudication.h"
#include <cstdlib>

namespace {
    constexpr uint8_t kKing = 1, kPawn = 6;
    constexpr int kLineWidth = 80;

    bool isBoardSquare(int square) {
        return square >= 0 && square <= 0x77 && !(square & 0x88);
    }

    char fileChar(int square) {
        return char('a' + (square & 7));
    }

    char rankChar(int square) {
        return char('1' + (square >> 4));
    }

    std::string squareName(int square) {
        return {fileChar(square), rankChar(square)};
    }

    char pieceLetter(uint8_t piece) {
        static const char letters[] = " KQRBNP";
        return piece <= kPawn ? letters[piece] : '?';
    }

    bool isCastle(const BCMove &move) {
        return (move.special & 1) && move.piece == kKing;
    }

    bool isEnPassant(const BCMove &move) {
        return (move.special & 1) && move.piece == kPawn;
    }

    bool isPromotion(const BCMove &move) {
        return (move.special & 1) && move.piece != kKing && move.piece != kPawn;
    }

    /* Promotion records carry the chosen piece; the piece on the source square is a pawn. */
    uint8_t movingPiece(const BCMove &move) {
        return isPromotion(move) ? kPawn : move.piece;
    }

    bool samePlacement(const Position &a, const Position &b) {
        if (a.side != b.side)
            return false;
        for (int square = 0; square <= 0x77; ++square) {
            if (!isBoardSquare(square))
                continue;
            const Square &x = a.board[square], &y = b.board[square];
            if (x.piece != y.piece || (x.piece && x.side != y.side))
                return false;
        }
        return true;
    }

    /* Recover the committed move from consecutive snapshots instead of the history
     * array, whose 103-record window drops early moves in long games. */
    bool findMove(const BCGame &before, const BCGame &after, BCMove &found) {
        BCMove moves[BC_GAME_MOVE_CAPACITY];
        size_t count = bcGameLegalMoves(&before, moves);
        for (size_t i = 0; i < count; ++i) {
            BCGame trial = before;
            if (bcGameApply(&trial, moves[i], nullptr) &&
                samePlacement(trial.position, after.position)) {
                found = moves[i];
                return true;
            }
        }
        return false;
    }

    bool isStandardStart(const BCGame &game) {
        BCGame initial;
        bcGameInit(&initial);
        return samePlacement(game.position, initial.position) &&
               bcGameCastlingRights(&game, 0) == 3 && bcGameCastlingRights(&game, 1) == 3;
    }

    std::string resultText(const BCGame &game) {
        switch (bc_adjudicate(&game)) {
        case BC_CHECKMATE:
            return game.position.side ? "1-0" : "0-1";
        case BC_STALEMATE:
            return "1/2-1/2";
        default:
            return "*";
        }
    }

    std::string tagLine(const char *name, const std::string &value) {
        std::string escaped;
        for (char c : value) {
            if (c == '"' || c == '\\')
                escaped += '\\';
            escaped += c;
        }
        return std::string("[") + name + " \"" + escaped + "\"]\n";
    }

    void appendToken(std::string &movetext, int &lineLength, const std::string &token) {
        if (lineLength && lineLength + 1 + int(token.size()) > kLineWidth) {
            movetext += '\n';
            lineLength = 0;
        } else if (lineLength) {
            movetext += ' ';
            ++lineLength;
        }
        movetext += token;
        lineLength += int(token.size());
    }
}

std::string sanMove(const BCGame &game, const BCMove &move) {
    std::string san;
    if (isCastle(move)) {
        san = move.to > move.from ? "O-O" : "O-O-O";
    } else {
        uint8_t piece = movingPiece(move);
        bool capture = move.captured || isEnPassant(move);
        if (piece == kPawn) {
            if (capture)
                san += fileChar(move.from);
        } else {
            san += pieceLetter(piece);
            BCMove moves[BC_GAME_MOVE_CAPACITY];
            size_t count = bcGameLegalMoves(&game, moves);
            bool ambiguous = false, sameFile = false, sameRank = false;
            for (size_t i = 0; i < count; ++i) {
                const BCMove &other = moves[i];
                if (other.to != move.to || other.from == move.from || isCastle(other) ||
                    movingPiece(other) != piece)
                    continue;
                ambiguous = true;
                sameFile |= (other.from & 7) == (move.from & 7);
                sameRank |= (other.from >> 4) == (move.from >> 4);
            }
            if (ambiguous && (!sameFile || sameRank))
                san += fileChar(move.from);
            if (ambiguous && sameFile)
                san += rankChar(move.from);
        }
        if (capture)
            san += 'x';
        san += squareName(move.to);
        if (isPromotion(move)) {
            san += '=';
            san += pieceLetter(move.piece);
        }
    }
    BCGame after = game;
    if (bcGameApply(&after, move, nullptr)) {
        BCOutcome outcome = bc_adjudicate(&after);
        if (outcome == BC_CHECKMATE)
            san += '#';
        else if (outcome == BC_CHECK)
            san += '+';
    }
    return san;
}

std::string fenPosition(const BCGame &game) {
    const Position &position = game.position;
    std::string fen;
    for (int rank = 7; rank >= 0; --rank) {
        int empty = 0;
        for (int file = 0; file < 8; ++file) {
            const Square &square = position.board[rank * 16 + file];
            if (!square.piece) {
                ++empty;
                continue;
            }
            if (empty)
                fen += char('0' + empty);
            empty = 0;
            char letter = pieceLetter(square.piece);
            fen += square.side ? char(letter - 'A' + 'a') : letter;
        }
        if (empty)
            fen += char('0' + empty);
        if (rank)
            fen += '/';
    }
    fen += position.side ? " b " : " w ";
    unsigned white = bcGameCastlingRights(&game, 0), black = bcGameCastlingRights(&game, 1);
    std::string castling;
    if (white & 2)
        castling += 'K';
    if (white & 1)
        castling += 'Q';
    if (black & 2)
        castling += 'k';
    if (black & 1)
        castling += 'q';
    fen += castling.empty() ? "-" : castling;
    std::string enPassant = "-";
    if (game.historyCount) {
        const BCMove &last = game.history[game.historyCount - 1];
        if (last.piece == kPawn && !last.special && std::abs(int(last.to) - int(last.from)) == 32)
            enPassant = squareName((last.to + last.from) / 2);
    }
    return fen + " " + enPassant + " 0 1";
}

std::string exportPgn(const std::vector<BCGame> &past, const BCGame &current,
                      const PgnTags &tags) {
    const BCGame &start = past.empty() ? current : past.front();
    std::string result = resultText(current);
    std::string pgn = tagLine("Event", tags.event) + tagLine("Site", tags.site) +
                      tagLine("Date", tags.date) + tagLine("Round", "-") +
                      tagLine("White", tags.white) + tagLine("Black", tags.black) +
                      tagLine("Result", result);
    if (!isStandardStart(start))
        pgn += tagLine("SetUp", "1") + tagLine("FEN", fenPosition(start));
    pgn += '\n';

    std::string movetext;
    int lineLength = 0, moveNumber = 1;
    for (size_t i = 0; i < past.size(); ++i) {
        const BCGame &before = past[i];
        const BCGame &after = i + 1 < past.size() ? past[i + 1] : current;
        BCMove move;
        if (!findMove(before, after, move))
            return {};
        bool black = before.position.side;
        if (!black)
            appendToken(movetext, lineLength, std::to_string(moveNumber) + ".");
        else if (i == 0)
            appendToken(movetext, lineLength, std::to_string(moveNumber) + "...");
        appendToken(movetext, lineLength, sanMove(before, move));
        if (black)
            ++moveNumber;
    }
    appendToken(movetext, lineLength, result);
    return pgn + movetext + "\n";
}
