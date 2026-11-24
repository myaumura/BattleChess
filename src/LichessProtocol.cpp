#include "LichessProtocol.h"
#include "PgnImport.h"
#include <cstdio>
#include <sstream>

namespace
{
    constexpr uint8_t kKing = 1, kRook = 3, kPawn = 6;

    std::string squareName(int square)
    {
        return {char('a' + (square & 7)), char('1' + (square >> 4))};
    }

    int parseSquare(const std::string& text, size_t at)
    {
        if (at + 2 > text.size())
            return -1;
        int file = text[at] - 'a', rank = text[at + 1] - '1';
        if (file < 0 || file > 7 || rank < 0 || rank > 7)
            return -1;
        return (rank << 4) | file;
    }

    uint8_t promotionPiece(char letter)
    {
        switch (letter)
        {
        case 'q': return 2;
        case 'r': return 3;
        case 'b': return 4;
        case 'n': return 5;
        default: return 0;
        }
    }

    char promotionLetter(uint8_t piece)
    {
        static const char letters[] = "  qrbn";
        return piece >= 2 && piece <= 5 ? letters[piece] : 0;
    }

    bool isCastle(const BCGame& game, const BCMove& move)
    {
        return game.position.board[move.from].piece == kKing &&
            ((move.to & 7) - (move.from & 7) == 2 || (move.from & 7) - (move.to & 7) == 2);
    }
}

std::string uciMove(const BCGame& game, const BCMove& move)
{
    std::string uci = squareName(move.from) + squareName(move.to);
    if (game.position.board[move.from].piece == kPawn && move.piece != kPawn)
        if (char letter = promotionLetter(move.piece))
            uci += letter;
    return uci;
}

bool resolveUci(const BCGame& game, const std::string& uci, BCMove& resolved)
{
    int from = parseSquare(uci, 0), to = parseSquare(uci, 2);
    if (from < 0 || to < 0 || uci.size() > 5)
        return false;
    uint8_t promotion = uci.size() == 5 ? promotionPiece(uci[4]) : 0;
    if (uci.size() == 5 && !promotion)
        return false;
    BCMove moves[BC_GAME_MOVE_CAPACITY];
    size_t count = bcGameLegalMoves(&game, moves);
    const auto& mover = game.position.board[from];
    const auto& target = game.position.board[to];
    // King-to-rook castling: the king "captures" its own rook.
    bool kingToRook = mover.piece == kKing && target.piece == kRook && target.side == mover.side;
    for (size_t i = 0; i < count; ++i)
    {
        const BCMove& move = moves[i];
        if (move.from != from)
            continue;
        bool matches = kingToRook
                           ? isCastle(game, move) && ((move.to > from) == (to > from))
                           : move.to == to;
        if (!matches)
            continue;
        bool promoting = mover.piece == kPawn && move.piece != kPawn;
        if (promoting != (promotion != 0) || (promoting && move.piece != promotion))
            continue;
        resolved = move;
        return true;
    }
    return false;
}

std::vector<std::string> splitUciMoves(const std::string& moves)
{
    std::vector<std::string> out;
    std::istringstream stream(moves);
    std::string token;
    while (stream >> token)
        out.push_back(token);
    return out;
}

LichessLine buildLichessLine(const std::string& initialFen, const std::string& moves)
{
    LichessLine line;
    if (initialFen.empty() || initialFen == "startpos")
    {
        bcGameInit(&line.start);
    }
    else if (!parseFen(initialFen, line.start, line.error))
    {
        line.error = "Unreadable starting position: " + line.error;
        return line;
    }
    BCGame game = line.start;
    for (const auto& uci : splitUciMoves(moves))
    {
        BCMove move;
        if (!resolveUci(game, uci, move) || !bcGameApply(&game, move, nullptr))
        {
            line.error = "Illegal move from Lichess: " + uci;
            return line;
        }
        line.moves.push_back(move);
    }
    return line;
}

LichessGameState parseGameState(const Json& event)
{
    LichessGameState state;
    state.moves = event["moves"].str();
    state.status = event["status"].str();
    state.winner = event["winner"].str();
    state.wtime = event["wtime"].integer();
    state.btime = event["btime"].integer();
    state.winc = event["winc"].integer();
    state.binc = event["binc"].integer();
    state.wdraw = event["wdraw"].boolean();
    state.bdraw = event["bdraw"].boolean();
    return state;
}

static LichessPlayer parsePlayer(const Json& player)
{
    LichessPlayer out;
    out.aiLevel = int(player["aiLevel"].integer());
    out.name = player["name"].str(out.aiLevel
                                      ? "Lichess AI level " + std::to_string(out.aiLevel)
                                      : "Anonymous");
    out.rating = int(player["rating"].integer());
    return out;
}

LichessGameFull parseGameFull(const Json& event)
{
    LichessGameFull game;
    game.id = event["id"].str();
    game.initialFen = event["initialFen"].str("startpos");
    game.speed = event["speed"].str();
    game.white = parsePlayer(event["white"]);
    game.black = parsePlayer(event["black"]);
    game.clockInitial = event["clock"]["initial"].integer();
    game.clockIncrement = event["clock"]["increment"].integer();
    game.daysPerTurn = int(event["daysPerTurn"].integer());
    game.state = parseGameState(event["state"]);
    return game;
}

LichessChallenge parseChallenge(const Json& challenge)
{
    LichessChallenge out;
    out.id = challenge["id"].str();
    out.challenger = challenge["challenger"]["name"].str("Someone");
    out.speed = challenge["speed"].str();
    out.color = challenge["color"].str("random");
    out.rated = challenge["rated"].boolean();
    out.incoming = challenge["direction"].str("in") == "in";
    const Json& control = challenge["timeControl"];
    out.timeControl = control["show"].str();
    if (out.timeControl.empty() && control["type"].str() == "correspondence")
        out.timeControl = std::to_string(control["daysPerTurn"].integer()) + " days";
    if (out.timeControl.empty())
        out.timeControl = "unlimited";
    return out;
}

LichessMoveAnimation chooseLichessAnimation(LichessAnimationPolicy policy,
                                            const std::string& speed, bool hasClock,
                                            long long ownMillis, bool capture)
{
    switch (policy)
    {
    case LichessAnimationPolicy::full:
        return LichessMoveAnimation::full;
    case LichessAnimationPolicy::boardOnly:
        return LichessMoveAnimation::none;
    case LichessAnimationPolicy::fast:
        return capture ? LichessMoveAnimation::none : LichessMoveAnimation::fast;
    case LichessAnimationPolicy::automatic:
        break;
    }
    // Under a minute on the own clock nothing may cost time.
    if (hasClock && ownMillis < 60000)
        return LichessMoveAnimation::none;
    if (speed == "correspondence" || speed == "classical" || !hasClock)
        return LichessMoveAnimation::full;
    if (speed == "rapid")
        return capture ? LichessMoveAnimation::none : LichessMoveAnimation::fast;
    return LichessMoveAnimation::none; // Blitz and faster.
}

const char* lichessPolicyName(LichessAnimationPolicy policy)
{
    switch (policy)
    {
    case LichessAnimationPolicy::automatic: return "Auto";
    case LichessAnimationPolicy::full: return "Full";
    case LichessAnimationPolicy::fast: return "Fast";
    case LichessAnimationPolicy::boardOnly: return "Board Only";
    }
    return "Auto";
}

std::string formatLichessClock(long long millis)
{
    if (millis < 0)
        millis = 0;
    char text[32];
    long long seconds = millis / 1000;
    if (seconds >= 3600)
        std::snprintf(text, sizeof text, "%lld:%02lld:%02lld", seconds / 3600, seconds / 60 % 60,
                      seconds % 60);
    else if (millis < 10000)
        std::snprintf(text, sizeof text, "0:%02lld.%lld", seconds, millis / 100 % 10);
    else
        std::snprintf(text, sizeof text, "%lld:%02lld", seconds / 60, seconds % 60);
    return text;
}

bool lichessGameOver(const std::string& status)
{
    return !status.empty() && status != "created" && status != "started";
}

std::string lichessEndMessage(const LichessGameState& state, const std::string& ownColor,
                              const std::string& opponent)
{
    if (!lichessGameOver(state.status))
        return "";
    const std::string& s = state.status;
    if (s == "aborted")
        return "Game aborted.";
    if (s == "noStart")
        return "Game did not start.";
    std::string reason = s == "mate"
                             ? "by checkmate"
                             : s == "resign"
                             ? "by resignation"
                             : s == "outoftime"
                             ? "on time"
                             : s == "timeout"
                             ? "because the opponent left"
                             : s == "cheat"
                             ? "by fair-play decision"
                             : s == "variantEnd"
                             ? "by variant rules"
                             : "";
    if (state.winner.empty())
    {
        if (s == "stalemate")
            return "Stalemate. Draw.";
        if (s == "insufficientMaterialClaim")
            return "Draw: insufficient material.";
        if (s == "outoftime")
            return "Draw: time ran out against insufficient material.";
        return "Game drawn.";
    }
    std::string who = state.winner == ownColor ? "You win" : opponent + " wins";
    return reason.empty() ? who + "." : who + " " + reason + ".";
}

const std::vector<LichessTimeControl>& lichessTimeControls()
{
    static const std::vector<LichessTimeControl> controls = {
        {"10+0 Rapid", 10, 0, 0, true}, {"15+10 Rapid", 15, 10, 0, true},
        {"30+0 Classical", 30, 0, 0, true}, {"30+20 Classical", 30, 20, 0, true},
        {"5+3 Blitz", 5, 3, 0, false}, {"Correspondence 3 days", 0, 0, 3, true},
    };
    return controls;
}

std::string formEscape(const std::string& value)
{
    static const char hex[] = "0123456789ABCDEF";
    std::string out;
    for (unsigned char c : value)
    {
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
            c == '-' || c == '_' || c == '.' || c == '~')
        {
            out += char(c);
        }
        else if (c == ' ')
        {
            out += '+';
        }
        else
        {
            out += '%';
            out += hex[c >> 4];
            out += hex[c & 15];
        }
    }
    return out;
}
