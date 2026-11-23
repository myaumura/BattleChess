#include "PgnImport.h"
#include "setup_board.h"
#include <algorithm>
#include <cctype>
#include <cstring>
#include <sstream>

namespace {
    constexpr uint8_t kKing = 1, kPawn = 6;

    bool isPromotion(const BCMove &move) {
        return (move.special & 1) && move.piece != kKing && move.piece != kPawn;
    }

    bool isCastle(const BCMove &move) {
        return (move.special & 1) && move.piece == kKing;
    }

    uint8_t movingPiece(const BCMove &move) {
        return isPromotion(move) ? kPawn : move.piece;
    }

    /* SAN piece letters; FEN uses the same letters with case for color. */
    uint8_t pieceFromLetter(char letter) {
        switch (std::toupper(static_cast<unsigned char>(letter))) {
        case 'K':
            return 1;
        case 'Q':
            return 2;
        case 'R':
            return 3;
        case 'B':
            return 4;
        case 'N':
            return 5;
        case 'P':
            return 6;
        default:
            return 0;
        }
    }

    bool isResult(const std::string &token) {
        return token == "1-0" || token == "0-1" || token == "1/2-1/2" || token == "*";
    }

    /* Strip move numbers ("12." / "12..."), check marks and annotation suffixes. */
    std::string cleanMoveToken(std::string token) {
        size_t digits = 0;
        while (digits < token.size() && std::isdigit(static_cast<unsigned char>(token[digits])))
            ++digits;
        if (digits && digits < token.size() && token[digits] == '.') {
            size_t dots = digits;
            while (dots < token.size() && token[dots] == '.')
                ++dots;
            token.erase(0, dots);
        }
        while (!token.empty() && std::strchr("+#!?", token.back()))
            token.pop_back();
        return token;
    }

    struct Tokens {
        std::vector<std::pair<std::string, std::string>> tags;
        std::vector<std::string> movetext;
        std::string result;
    };

    /* Tokenize the first game. Movetext ends at a result token or the next tag section. */
    bool tokenize(const std::string &text, Tokens &tokens, std::string &error) {
        size_t i = 0;
        if (text.compare(0, 3, "\xEF\xBB\xBF") == 0)
            i = 3;
        int variationDepth = 0;
        bool inMovetext = false;
        while (i < text.size()) {
            char c = text[i];
            bool lineStart = i == 0 || text[i - 1] == '\n';
            if (lineStart && c == '%') { // Escape line.
                i = text.find('\n', i);
                i = i == std::string::npos ? text.size() : i;
                continue;
            }
            if (std::isspace(static_cast<unsigned char>(c))) {
                ++i;
                continue;
            }
            if (c == '{') {
                size_t end = text.find('}', i);
                if (end == std::string::npos) {
                    error = "Unterminated comment";
                    return false;
                }
                i = end + 1;
                continue;
            }
            if (c == ';') {
                i = text.find('\n', i);
                i = i == std::string::npos ? text.size() : i;
                continue;
            }
            if (c == '[' && variationDepth == 0) {
                if (inMovetext)
                    break; // Next game's tag section.
                size_t end = text.find(']', i);
                if (end == std::string::npos) {
                    error = "Unterminated tag";
                    return false;
                }
                std::string tag = text.substr(i + 1, end - i - 1);
                size_t space = tag.find_first_of(" \t");
                size_t open = tag.find('"'), close = tag.rfind('"');
                if (space != std::string::npos && open != std::string::npos && close > open) {
                    std::string value;
                    for (size_t k = open + 1; k < close; ++k) {
                        if (tag[k] == '\\' && k + 1 < close)
                            ++k;
                        value += tag[k];
                    }
                    tokens.tags.emplace_back(tag.substr(0, space), value);
                }
                i = end + 1;
                continue;
            }
            if (c == '(') {
                ++variationDepth;
                ++i;
                continue;
            }
            if (c == ')') {
                variationDepth = std::max(0, variationDepth - 1);
                ++i;
                continue;
            }
            size_t end = i;
            while (end < text.size() && !std::isspace(static_cast<unsigned char>(text[end])) &&
                   !std::strchr("{}();[]", text[end]))
                ++end;
            std::string token = text.substr(i, end - i);
            i = end;
            if (variationDepth)
                continue;
            inMovetext = true;
            if (token[0] == '$')
                continue; // Numeric annotation glyph.
            if (isResult(token)) {
                tokens.result = token;
                break;
            }
            token = cleanMoveToken(token);
            if (!token.empty())
                tokens.movetext.push_back(token);
        }
        return true;
    }

    std::string tagValue(const Tokens &tokens, const char *name) {
        for (auto &[key, value] : tokens.tags)
            if (key == name)
                return value;
        return {};
    }
}

bool parseFen(const std::string &fen, BCGame &game, std::string &error) {
    std::istringstream fields(fen);
    std::string placement, side = "w", castling = "-", enPassant = "-";
    fields >> placement >> side >> castling >> enPassant;
    Position position{};
    int rank = 7, file = 0;
    for (char c : placement) {
        if (c == '/') {
            if (file != 8 || rank == 0) {
                error = "Invalid FEN board";
                return false;
            }
            --rank;
            file = 0;
        } else if (c >= '1' && c <= '8') {
            file += c - '0';
        } else if (uint8_t piece = pieceFromLetter(c); piece && file < 8) {
            unsigned color = std::islower(static_cast<unsigned char>(c)) ? 1 : 0;
            if (!insert_piece(&position, piece, color, unsigned(rank * 16 + file))) {
                error = "Too many pieces in FEN";
                return false;
            }
            ++file;
        } else {
            error = "Invalid FEN board";
            return false;
        }
        if (file > 8) {
            error = "Invalid FEN board";
            return false;
        }
    }
    if (rank != 0 || file != 8 || (side != "w" && side != "b")) {
        error = "Invalid FEN";
        return false;
    }
    calculate_piece_lists(&position);
    position.side = side == "b";
    position.opponent = !position.side;
    if (const char *problem = bc_setup_validate(&position)) {
        error = problem;
        return false;
    }
    bcGameInit(&game);
    if (!bc_setup_commit(&game, &position)) {
        error = "Invalid FEN position";
        return false;
    }
    // The engine derives en passant from the previous move, so record the double step.
    if (enPassant.size() == 2 && enPassant[0] >= 'a' && enPassant[0] <= 'h' &&
        (enPassant[1] == '3' || enPassant[1] == '6')) {
        int target = calc_square(static_cast<unsigned char>(enPassant[0]),
                                 static_cast<unsigned char>(enPassant[1]));
        int direction = enPassant[1] == '3' ? 16 : -16;
        int to = target + direction, from = target - direction;
        const Square &pawn = game.position.board[to];
        if (pawn.piece == kPawn && pawn.side == game.position.opponent &&
            game.historyCount < BC_GAME_HISTORY_CAPACITY) {
            BCMove doubleStep{uint16_t(to), uint16_t(from), 0, kPawn, 0};
            game.history[game.historyCount++] = doubleStep;
        }
    }
    (void)castling; // Rights follow the engine's piece-history rule (see header).
    return true;
}

bool resolveMove(const BCGame &game, const std::string &rawToken, BCMove &resolved,
                 std::string &error) {
    BCMove moves[BC_GAME_MOVE_CAPACITY];
    size_t count = bcGameLegalMoves(&game, moves);

    std::string castle = rawToken;
    for (char &c : castle)
        if (c == '0')
            c = 'O';
    if (castle == "O-O" || castle == "O-O-O") {
        bool kingside = castle == "O-O";
        for (size_t i = 0; i < count; ++i)
            if (isCastle(moves[i]) && (moves[i].to > moves[i].from) == kingside) {
                resolved = moves[i];
                return true;
            }
        error = "Illegal castling";
        return false;
    }

    // [Piece][from file][from rank][x]<to file><to rank>[=promotion]; '-' separates
    // long algebraic squares ("e2-e4") and is dropped.
    std::string text;
    uint8_t promotion = 0;
    for (size_t i = 0; i < rawToken.size(); ++i) {
        char c = rawToken[i];
        if (c == '-')
            continue;
        if (c == '=') {
            promotion = i + 1 < rawToken.size() ? pieceFromLetter(rawToken[i + 1]) : 0;
            if (promotion < 2 || promotion > 5) {
                error = "Invalid promotion";
                return false;
            }
            break;
        }
        text += c;
    }
    // Promotion without '=': "e8Q", or lowercase UCI style "e7e8q".
    if (!promotion && text.size() >= 3 &&
        std::isdigit(static_cast<unsigned char>(text[text.size() - 2]))) {
        uint8_t piece = pieceFromLetter(text.back());
        if (piece >= 2 && piece <= 5) {
            promotion = piece;
            text.pop_back();
        }
    }
    if (text.size() < 2) {
        error = "Unreadable move";
        return false;
    }
    int toFile = text[text.size() - 2] - 'a', toRank = text[text.size() - 1] - '1';
    if (toFile < 0 || toFile > 7 || toRank < 0 || toRank > 7) {
        error = "Unreadable move";
        return false;
    }
    std::string prefix = text.substr(0, text.size() - 2);
    uint8_t piece = kPawn;
    if (!prefix.empty() && std::isupper(static_cast<unsigned char>(prefix[0]))) {
        piece = pieceFromLetter(prefix[0]);
        if (!piece) {
            error = "Unknown piece";
            return false;
        }
        prefix.erase(0, 1);
    }
    int fromFile = -1, fromRank = -1;
    for (char c : prefix) {
        if (c >= 'a' && c <= 'h')
            fromFile = c - 'a';
        else if (c >= '1' && c <= '8')
            fromRank = c - '1';
        else if (c != 'x' && c != ':') {
            error = "Unreadable move";
            return false;
        }
    }
    // Long algebraic notation names the source square; its piece decides the type.
    if (piece == kPawn && fromFile >= 0 && fromRank >= 0) {
        uint8_t onSquare = game.position.board[fromRank * 16 + fromFile].piece;
        if (onSquare)
            piece = onSquare;
    }
    int to = toRank * 16 + toFile, matches = 0;
    for (size_t i = 0; i < count; ++i) {
        const BCMove &move = moves[i];
        if (isCastle(move) && !(piece == kKing && move.to == to))
            continue;
        if (movingPiece(move) != piece || move.to != to)
            continue;
        if ((fromFile >= 0 && (move.from & 7) != fromFile) ||
            (fromRank >= 0 && (move.from >> 4) != fromRank))
            continue;
        if (isPromotion(move) && move.piece != (promotion ? promotion : 2))
            continue;
        if (!isPromotion(move) && promotion)
            continue;
        resolved = move;
        ++matches;
    }
    if (matches == 1)
        return true;
    error = matches ? "Ambiguous move" : "Illegal move";
    return false;
}

PgnImport importPgn(const std::string &text) {
    PgnImport result;
    Tokens tokens;
    if (!tokenize(text, tokens, result.error))
        return result;
    std::string variant = tagValue(tokens, "Variant");
    if (!variant.empty() && variant != "Standard" && variant != "From Position") {
        result.error = "Unsupported variant: " + variant;
        return result;
    }
    std::string fen = tagValue(tokens, "FEN");
    if (!fen.empty()) {
        if (!parseFen(fen, result.start, result.error))
            return result;
    } else {
        bcGameInit(&result.start);
    }
    result.white = tagValue(tokens, "White");
    result.black = tagValue(tokens, "Black");
    result.result = tokens.result.empty() ? tagValue(tokens, "Result") : tokens.result;
    BCGame game = result.start;
    int ply = 0;
    for (const std::string &token : tokens.movetext) {
        BCMove move;
        std::string problem;
        int number = (ply + (result.start.position.side ? 1 : 0)) / 2 + 1;
        std::string label = std::to_string(number) + ((ply + result.start.position.side) % 2
                                                           ? "... " : ". ") + token;
        if (!resolveMove(game, token, move, problem)) {
            result.error = problem + ": " + label;
            return result;
        }
        if (!bcGameApply(&game, move, nullptr)) {
            result.error = "Illegal move: " + label;
            return result;
        }
        result.moves.push_back(move);
        ++ply;
    }
    return result;
}
