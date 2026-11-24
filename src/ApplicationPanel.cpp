#include "ApplicationInternal.h"
#include "MacBitmapFont.h"
#include "PgnExport.h"
#include "adjudication.h"
#include "presentation.h"
#include <algorithm>
#include <cstring>
#include <string>

/* Native side-by-side information panel; no original entry point. It sits under both
 * boards: game status on the left, the session's move list on the right. */

namespace {
    constexpr int kPanelPadding = 8, kRowHeight = 18, kStatusWidth = 200, kTokenGap = 6;

    const char *playerName(uint8_t player) {
        return player == 0 ? "Human" : player == 1 ? "Mac" : "Modem";
    }
}

int Application::viewHeight() const {
    return 20 + original_content_height + (dualView() ? kPanelHeight : 0);
}

/* The whole line of play: committed history, the shown game, then the moves Replay
 * would restore (future is a stack, nearest move last). */
const std::vector<std::string> &Application::panelMoves() {
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
    for (size_t i = 0; i + 1 < line.size(); ++i) {
        BCMove move;
        if (!recoverMove(line[i], line[i + 1], move))
            break; // A position set up mid-session ends the readable line.
        panelSan.push_back(sanMove(line[i], move));
    }
    return panelSan;
}

/* Lay out move numbers and moves in rows, keeping each number with its moves, and
 * scroll so the most recent committed move stays visible. */
std::vector<Application::PanelToken> Application::panelTokens() {
    const auto &moves = panelMoves();
    std::vector<PanelToken> tokens;
    const int left = kStatusWidth + kPanelPadding, right = viewWidth() - kPanelPadding;
    int x = left, row = 0;
    bool blackFirst = !panelLine.empty() && panelLine.front().position.side;
    size_t i = 0;
    for (int number = 1; i < moves.size(); ++number) {
        // A group is "n. white black", or "n... black" when Black moved first.
        std::vector<PanelToken> group;
        std::string label = std::to_string(number) + (blackFirst && i == 0 ? "..." : ".");
        group.push_back({label, -1, 0, 0, systemTextWidth(label)});
        size_t count = blackFirst && i == 0 ? 1 : 2;
        for (size_t k = 0; k < count && i < moves.size(); ++k, ++i)
            group.push_back({moves[i], int(i), 0, 0, systemTextWidth(moves[i])});
        int width = -kTokenGap;
        for (auto &token : group)
            width += token.width + kTokenGap;
        if (x > left && x + width > right) {
            x = left;
            ++row;
        }
        for (auto &token : group) {
            token.x = x;
            token.row = row;
            x += token.width + kTokenGap;
            tokens.push_back(token);
        }
        x += kTokenGap;
    }
    const int visible = (kPanelHeight - 2 * kPanelPadding) / kRowHeight;
    int currentRow = 0;
    for (auto &token : tokens)
        if (token.ply == int(session.past.size()) - 1)
            currentRow = token.row;
    int firstRow = std::max(0, currentRow - visible + 1);
    std::vector<PanelToken> shown;
    for (auto &token : tokens)
        if (token.row >= firstRow && token.row < firstRow + visible) {
            token.row -= firstRow;
            shown.push_back(token);
        }
    return shown;
}

void Application::renderPanel() {
    if (!dualView())
        return;
    const int top = 20 + original_content_height, width = viewWidth();
    setGrayscale(rendererPtr, 255);
    drawRectangle(rendererPtr, 0, top, width, kPanelHeight);
    setGrayscale(rendererPtr, 0);
    drawRectangle(rendererPtr, 0, top, width, 1);
    drawRectangle(rendererPtr, kStatusWidth, top + kPanelPadding, 1,
                  kPanelHeight - 2 * kPanelPadding);
    const int textOffset = chicago_available ? 0 : 4;
    auto rowY = [&](int row) { return top + kPanelPadding + row * kRowHeight; };

    // Status: outcome or side to move, computer activity, players and level.
    const char *side = session.game.position.side ? "Black" : "White";
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
    if (lichess.onlineSession) {
        auto player = [](const LichessPlayer &p) {
            return p.rating ? p.name + " (" + std::to_string(p.rating) + ")" : p.name;
        };
        lines[1] = !lichess.inGame        ? "Lichess game over"
                   : lichess.opponentGone ? "Opponent left the game"
                   : lichessOwnTurn()     ? "Your move"
                                          : "Waiting for " + lichess.opponent;
        lines[2] = "White: " + player(lichess.meta.white);
        lines[3] = "Black: " + player(lichess.meta.black);
        lines[4] = "Lichess " + lichess.speed + ", animations " +
                   lichessPolicyName(lichess.policy);
    }
    for (int row = 0; row < 5; ++row)
        drawSystemText(rendererPtr, kPanelPadding, rowY(row) + textOffset, lines[row].c_str());

    // Moves: the last committed move inverted, moves Replay would restore in gray.
    const int current = int(session.past.size()) - 1;
    for (const auto &token : panelTokens()) {
        int y = rowY(token.row);
        bool highlighted = token.ply >= 0 && token.ply == current;
        if (highlighted) {
            setGrayscale(rendererPtr, 0);
            drawRectangle(rendererPtr, token.x - 2, y, token.width + 4, kRowHeight - 1);
        }
        setGrayscale(rendererPtr, highlighted ? 255 : token.ply > current ? 150 : 0);
        drawSystemText(rendererPtr, token.x, y + textOffset, token.text.c_str());
    }
}

/* Clicking a move shows the position after it, stepping through Take Back or Replay
 * so the session history behaves exactly as with the menu commands. */
void Application::handlePanelPress(int x, int y) {
    const int top = 20 + original_content_height;
    for (const auto &token : panelTokens()) {
        int tokenY = top + kPanelPadding + token.row * kRowHeight;
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

bool Application::fullscreen() const {
    return SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN;
}

/* Native window adapter; no original entry point. Borderless desktop full screen; the
 * integer-scaled presentation centers the canvas. */
void Application::toggleFullscreen() {
    bool on = !fullscreen();
    if (!SDL_SetWindowFullscreen(window, on)) {
        showAlert("Unable to change full screen mode.");
        return;
    }
    SDL_SyncWindow(window);
    if (!on)
        applyLayout(true);
}
