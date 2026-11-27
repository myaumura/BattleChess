#include "ApplicationInternal.h"
#include "MacBitmapFont.h"
#include "PgnExport.h"
#include "adjudication.h"
#include "presentation.h"
#include "recovered_core.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <string>

/* Native side-by-side information panel; no original entry point. With the 3D board at
 * its original size it sits under both boards: game status on the left, the session's
 * move list on the right. With a zoomed 3D board it is stacked in the right column under
 * the 2D board: status above, moves below. */

namespace
{
    constexpr int kPanelPadding = 8, kRowHeight = 18, kStatusWidth = 200, kTokenGap = 6;

    const char* playerName(uint8_t player)
    {
        return player == 0 ? "Human" : player == 1 ? "Mac" : "Modem";
    }
}

int Application::viewHeight() const
{
    if (dualView())
        return dualCanvasHeight(boardZoom) + fillHeight;
    return 20 + original_content_height + (showCaptured ? kCapturedHeight : 0);
}

int Application::panelHeight() const
{
    return panelTextHeight() + (showCaptured ? kCapturedHeight : 0);
}

int Application::panelTextHeight() const
{
    return panelGeometry().height;
}

Application::PanelGeometry Application::panelGeometry() const
{
    PanelGeometry g{};
    const int boardsBottom = 20 + original_content_height;
    g.capturedLeft = 0;
    g.capturedWidth = viewWidth();
    if (!largeLayout())
    {
        g.left = 0;
        g.top = boardsBottom;
        g.width = viewWidth();
        g.height = kPanelHeight + fillHeight;
        g.movesLeft = kStatusWidth + kPanelPadding;
        g.movesTop = g.top + kPanelPadding;
        g.capturedTop = g.top + g.height;
    }
    else
    {
        g.stacked = true;
        g.left = columnLeft();
        g.top = boardsBottom;
        g.width = viewWidth() - g.left;
        g.capturedTop = 20 + perspectiveRect().h + fillHeight;
        g.height = g.capturedTop - g.top;
        g.movesLeft = g.left + kPanelPadding;
        // Five status rows, then a divider, then the moves.
        g.movesTop = g.top + 2 * kPanelPadding + 5 * kRowHeight + kPanelPadding;
    }
    g.movesRight = g.left + g.width - kPanelPadding;
    g.movesRows = std::max(0, (g.top + g.height - kPanelPadding - g.movesTop) / kRowHeight);
    return g;
}

/* Native captured-pieces list; no original entry point. Each committed move removes at
 * most one piece of the side that did not move (en passant included); a promoting pawn
 * belongs to the mover and is not a capture. */
std::array<std::vector<uint8_t>, 2> Application::capturedPieces() const
{
    auto count = [](const Position& position, unsigned side, uint8_t piece)
    {
        int total = 0;
        for (int rank = 0; rank < 8; ++rank)
            for (int file = 0; file < 8; ++file)
            {
                const auto& square = position.board[(rank << 4) | file];
                total += square.piece == piece && square.side == side;
            }
        return total;
    };
    std::array<std::vector<uint8_t>, 2> captured;
    for (size_t i = 0; i < session.past.size(); ++i)
    {
        const Position& before = session.past[i].position;
        const Position& after =
            i + 1 < session.past.size() ? session.past[i + 1].position : session.game.position;
        unsigned victim = before.side ? 0 : 1;
        for (uint8_t piece = 2; piece <= 6; ++piece)
            for (int lost = count(before, victim, piece) - count(after, victim, piece); lost > 0;
                 --lost)
                captured[victim].push_back(piece);
    }
    // Queen, rook, bishop, knight, pawn: the engine's piece codes 2..6 in that order.
    for (auto& list : captured)
        std::sort(list.begin(), list.end());
    return captured;
}

void Application::renderCaptured(int left, int top, int width)
{
    setGrayscale(rendererPtr, 255);
    drawRectangle(rendererPtr, left, top, width, kCapturedHeight);
    setGrayscale(rendererPtr, 0);
    drawRectangle(rendererPtr, left, top, width, 1);
    static const int values[7] = {0, 0, 9, 5, 3, 3, 1};
    auto captured = capturedPieces();
    int material[2] = {0, 0}; // Material won by White (index 0) and Black (1).
    for (uint8_t piece : captured[1])
        material[0] += values[piece];
    for (uint8_t piece : captured[0])
        material[1] += values[piece];
    const int textOffset = chicago_available ? 0 : 4;
    for (unsigned taker = 0; taker < 2; ++taker)
    {
        int x = left + (taker == 0 ? 0 : width / 2) + kPanelPadding;
        if (taker == 1)
            drawRectangle(rendererPtr, left + width / 2, top + 4, 1, kCapturedHeight - 8);
        const char* label = taker == 0 ? "White" : "Black";
        drawSystemText(rendererPtr, x, top + 5 + textOffset, label);
        x += systemTextWidth(label) + 8;
        // The captured side's own flat sprites at half size: one screen pixel per sprite
        // pixel at the default 2x window.
        unsigned victim = 1 - taker;
        for (uint8_t piece : captured[victim])
        {
            Position sample{};
            insert_piece(&sample, piece, victim, 0);
            uint8_t codes[64];
            setup_display_board(&sample, codes);
            int shape = flat_shapes[codes[56] & 63] - 17;
            const auto& g = flat_piece_shapes[shape];
            drawTexture(rendererPtr, textures.flatPieces[victim * 6 + shape].get(), float(x),
                        float(top + kCapturedHeight - 3) - g.height / 2.0f, g.width / 2.0f,
                        g.height / 2.0f);
            x += g.width / 2 - 2;
        }
        int lead = material[taker] - material[1 - taker];
        if (lead > 0)
            drawSystemText(rendererPtr, x + 8, top + 5 + textOffset,
                           ("+" + std::to_string(lead)).c_str());
    }
}

/* The panel under both boards, or the captured strip under the single board. */
void Application::renderBelowBoard()
{
    if (dualView())
        renderPanel();
    else if (showCaptured)
        renderCaptured(0, 20 + original_content_height, viewWidth());
}

/* The whole line of play: committed history, the shown game, then the moves Replay
 * would restore (future is a stack, nearest move last). */
const std::vector<std::string>& Application::panelMoves()
{
    std::vector<BCGame> line = session.past;
    line.push_back(session.game);
    line.insert(line.end(), session.future.rbegin(), session.future.rend());
    bool same = line.size() == panelLine.size();
    for (size_t i = 0; same && i < line.size(); ++i)
        same = std::memcmp(&line[i], &panelLine[i], sizeof(BCGame)) == 0;
    if (same)
        return panelSan;
    panelLine = line;
    panelSan.clear();
    for (size_t i = 0; i + 1 < line.size(); ++i)
    {
        BCMove move;
        if (!recoverMove(line[i], line[i + 1], move))
            break; // A position set up mid-session ends the readable line.
        panelSan.push_back(sanMove(line[i], move));
    }
    return panelSan;
}

/* Lay out move numbers and moves in rows, keeping each number with its moves, and
 * scroll so the most recent committed move stays visible. */
std::vector<Application::PanelToken> Application::panelTokens()
{
    const auto& moves = panelMoves();
    std::vector<PanelToken> tokens;
    const PanelGeometry geometry = panelGeometry();
    const int left = geometry.movesLeft, right = geometry.movesRight;
    int x = left, row = 0;
    bool blackFirst = !panelLine.empty() && panelLine.front().position.side;
    size_t i = 0;
    for (int number = 1; i < moves.size(); ++number)
    {
        // A group is "n. white black", or "n... black" when Black moved first.
        std::vector<PanelToken> group;
        std::string label = std::to_string(number) + (blackFirst && i == 0 ? "..." : ".");
        group.push_back({label, -1, 0, 0, systemTextWidth(label)});
        size_t count = blackFirst && i == 0 ? 1 : 2;
        for (size_t k = 0; k < count && i < moves.size(); ++k, ++i)
            group.push_back({moves[i], int(i), 0, 0, systemTextWidth(moves[i])});
        int width = -kTokenGap;
        for (auto& token : group)
            width += token.width + kTokenGap;
        if (x > left && x + width > right)
        {
            x = left;
            ++row;
        }
        for (auto& token : group)
        {
            token.x = x;
            token.row = row;
            x += token.width + kTokenGap;
            tokens.push_back(token);
        }
        x += kTokenGap;
    }
    const int visible = geometry.movesRows;
    int currentRow = 0;
    for (auto& token : tokens)
        if (token.ply == int(session.past.size()) - 1)
            currentRow = token.row;
    int firstRow = std::max(0, currentRow - visible + 1);
    std::vector<PanelToken> shown;
    for (auto& token : tokens)
        if (token.row >= firstRow && token.row < firstRow + visible)
        {
            token.row -= firstRow;
            shown.push_back(token);
        }
    return shown;
}

void Application::renderPanel()
{
    if (!dualView())
        return;
    // (Below the single board, renderBelowBoard draws only the captured strip.)
    const PanelGeometry g = panelGeometry();
    const int top = g.top;
    setGrayscale(rendererPtr, 255);
    drawRectangle(rendererPtr, g.left, top, g.width, g.height);
    setGrayscale(rendererPtr, 0);
    drawRectangle(rendererPtr, g.left, top, g.width, 1);
    if (g.stacked)
    {
        // The column's left edge continues the seam beside the zoomed 3D board.
        drawRectangle(rendererPtr, g.left, top, 1, g.height);
        drawRectangle(rendererPtr, g.left + kPanelPadding, g.movesTop - kPanelPadding,
                      g.width - 2 * kPanelPadding, 1);
    }
    else
        drawRectangle(rendererPtr, kStatusWidth, top + kPanelPadding, 1,
                      g.height - 2 * kPanelPadding);
    const int textOffset = chicago_available ? 0 : 4;
    auto rowY = [&](int row) { return top + kPanelPadding + row * kRowHeight; };
    auto moveY = [&](int row) { return g.movesTop + row * kRowHeight; };

    // Status: outcome or side to move, computer activity, players and level.
    const char* side = session.game.position.side ? "Black" : "White";
    std::string status;
    BCOutcome outcome = bc_adjudicate(&session.game);
    if (outcome == BC_CHECKMATE)
        status = std::string("Checkmate: ") + (session.game.position.side ? "White" : "Black") +
            " wins";
    else if (outcome == BC_STALEMATE)
        status = "Stalemate";
    else
        status = std::string(side) + " to move" + (bcGameInCheck(&session.game) ? ", check" : "");
    std::string lines[] = {
        status,
        computer.busy() ? "Mac is thinking..." : session.future.empty() ? "" : "Viewing history",
        std::string("White: ") + playerName(settings.white_player),
        std::string("Black: ") + playerName(settings.black_player),
        std::string("Computer: ") +
        (settings.level < 10 ? menuResource(3).items[settings.level].label : "custom time"),
    };
    if (lichess.onlineSession)
    {
        auto player = [](const LichessPlayer& p)
        {
            return p.rating ? p.name + " (" + std::to_string(p.rating) + ")" : p.name;
        };
        lines[1] = !lichess.inGame
                       ? "Lichess game over"
                       : lichess.opponentGone
                       ? "Opponent left the game"
                       : lichessOwnTurn()
                       ? "Your move"
                       : "Waiting for " + lichess.opponent;
        lines[2] = "White: " + player(lichess.meta.white);
        lines[3] = "Black: " + player(lichess.meta.black);
        lines[4] = "Lichess " + lichess.speed + ", animations " +
            lichessPolicyName(lichess.policy);
    }
    for (int row = 0; row < 5; ++row)
        drawSystemText(rendererPtr, g.left + kPanelPadding, rowY(row) + textOffset,
                       lines[row].c_str());

    // Moves: the last committed move inverted, moves Replay would restore in gray.
    const int current = int(session.past.size()) - 1;
    for (const auto& token : panelTokens())
    {
        int y = moveY(token.row);
        bool highlighted = token.ply >= 0 && token.ply == current;
        if (highlighted)
        {
            setGrayscale(rendererPtr, 0);
            drawRectangle(rendererPtr, token.x - 2, y, token.width + 4, kRowHeight - 1);
        }
        setGrayscale(rendererPtr, highlighted ? 255 : token.ply > current ? 150 : 0);
        drawSystemText(rendererPtr, token.x, y + textOffset, token.text.c_str());
    }
    if (showCaptured)
        renderCaptured(g.capturedLeft, g.capturedTop, g.capturedWidth);
}

/* Clicking a move shows the position after it, stepping through Take Back or Replay
 * so the session history behaves exactly as with the menu commands. */
void Application::handlePanelPress(int x, int y)
{
    const int movesTop = panelGeometry().movesTop;
    for (const auto& token : panelTokens())
    {
        int tokenY = movesTop + token.row * kRowHeight;
        if (token.ply < 0 || x < token.x - 2 || x >= token.x + token.width + 2 || y < tokenY ||
            y >= tokenY + kRowHeight)
            continue;
        size_t target = size_t(token.ply) + 1;
        while (session.past.size() > target && menuItemEnabled(1, 1))
            performMenuAction(1, 1);
        while (session.past.size() < target && menuItemEnabled(1, 2))
            performMenuAction(1, 2);
        return;
    }
}

bool Application::fullscreen() const
{
    return SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN;
}

/* Native window adapter; no original entry point. Borderless desktop full screen; the
 * integer-scaled presentation centers the canvas. */
void Application::toggleFullscreen()
{
    bool on = !fullscreen();
    if (!SDL_SetWindowFullscreen(window, on))
    {
        showAlert("Unable to change full screen mode.");
        return;
    }
    SDL_SyncWindow(window);
    if (!on)
        applyLayout(true);
}
