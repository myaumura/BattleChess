#include "ApplicationInternal.h"
#include "MacBitmapFont.h"
#include "presentation.h"
#include "adjudication.h"
#include "PgnExport.h"
#include "PgnImport.h"
#include <iterator>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <stdexcept>

/* Native MENU-resource lookup; no original address. Keep titles, actions
 * and shortcuts tied to the extracted menu inventory. */
const OriginalMenu& Application::menuResource(int index)
{
    // Native Lichess menu; labels change with the connection (lichessMenuLabel).
    if (index == kLichessMenu)
    {
        static const std::vector<OriginalItem> items = {
            {"Connect...", 0}, {"Forget Lichess Token", 0}, {"Games...", 0},
            {"Analyse Game on Lichess", 0}, {"-", 0}, {"Play Lichess AI", 0},
            {"Challenge Player...", 0}, {"Find Opponent", 0}, {"Time", 0},
            {"Animations", 0}, {"-", 0}, {"Offer Draw", 0},
            {"Resign", 0}, {"Leave Game", 0}
        };
        static const OriginalMenu lichessMenu{0, "Lichess", items.data(), int(items.size())};
        return lichessMenu;
    }
    // Native File extension appended after Quit so original item indices stay intact.
    if (index == 0)
    {
        static const std::vector<OriginalItem> items = []
        {
            std::vector<OriginalItem> items(std::begin(menu_401), std::end(menu_401));
            items.insert(items.end(), {
                             {"-", 0},
                             {"Export PGN...", 'E'},
                             {"Import PGN...", 'I'},
                             {"Export GIF...", 'G'}
                         });
            return items;
        }();
        static const OriginalMenu fileMenu{401, "File", items.data(), int(items.size())};
        return fileMenu;
    }
    // Native settings extension; generated Macintosh resources remain untouched.
    if (index == 2)
    {
        static const std::vector<OriginalItem> items = []
        {
            std::vector<OriginalItem> items(std::begin(menu_403), std::end(menu_403));
            items.insert(items.end(),
                         {
                             // No separator row: a divider is drawn above this item so
                             // the menu fits the shortest window (see renderMenus).
                             {"Animation Speed", 0},
                             {"Show Both Boards", 0},
                             {"Enter Full Screen", 0},
                             {"Idle Animations", 0},
                             {"Show Coordinates", 0},
                             {"Show Captured Pieces", 0},
                             {"Rotate Board", 0},
                             {"Rotate for Player to Move", 0},
                             {"Rotate 3D Board Too", 0}
                         });
            return items;
        }();
        static const OriginalMenu settingsMenu{403, "Settings", items.data(), int(items.size())};
        return settingsMenu;
    }
    for (auto& m : original_menus)
        if (m.id == menuIds[index])
            return m;
    throw std::runtime_error("Missing MENU");
}

/* Native host for CHESSALE (file offset 0x3aba), using ALRT 406.
 * Queue a message instead of blocking the SDL event loop. */
void Application::showAlert(const std::string& value)
{
    dialogButton.cancel();
    alertId = 406;
    message = value;
    confirmAction = 0;
}

/* Native host for SETTIME, file offset 0x381e. DLOG 403 starts with all
 * existing minute text selected, so typing replaces the old value. */
void Application::openTimeDialog()
{
    dialogButton.cancel();
    textDialog.resourceId = 403;
    textDialog.text = std::to_string(bc_time_dialog_minutes(&thinkingTime));
    textDialog.selectAll();
    textDialog.open = true;
    require(SDL_StartTextInput(window), "Start time input");
}

/* Native dialog completion for SETTIME (0x381e) / SETLEVEL (0x6838).
 * The original dialog has OK only; clamp numeric input before storing seconds. */
void Application::acceptTextDialog()
{
    dialogButton.cancel();
    if (textDialog.resourceId >= kTokenPrompt)
    {
        acceptLichessPrompt();
        return;
    }
    if (textDialog.resourceId != 403)
    {
        if (!textDialog.text.empty())
        {
            if (textDialog.resourceId == 405)
                modem.text("ATDT " + textDialog.text + "\r");
            else
                modem.text(textDialog.text == "+++"
                               ? textDialog.text
                               : textDialog.text + std::string("\0\r", 2));
        }
        textDialog.open = false;
        require(SDL_StopTextInput(window), "Stop modem input");
        return;
    }
    long long minutes = std::strtoll(textDialog.text.c_str(), nullptr, 10);
    bc_time_set_minutes(&thinkingTime, int32_t(std::clamp(minutes, -1LL, 10001LL)));
    settings.level = thinkingTime.level;
    textDialog.open = false;
    require(SDL_StopTextInput(window), "Stop time input");
}

/* Native dialog command adapter for SETTIME 0x381e / SETLEVEL 0x6838.
 * Complete on Enter; ordinary editing only changes the local text field. */
void Application::handleTextKey(SDL_Keycode key, SDL_Keymod modifiers)
{
    if (key == SDLK_RETURN || key == SDLK_KP_ENTER)
    {
        acceptTextDialog();
    }
    else if (key == SDLK_ESCAPE && textDialog.resourceId >= kTokenPrompt)
    {
        textDialog.open = false;
        require(SDL_StopTextInput(window), "Stop Lichess input");
    }
    else
    {
        textDialog.key(key, modifiers);
    }
}

/* Native file adapter for SAVEREQ (file offset 0x8d34). Preserve opaque
 * original bytes and change the active filename only after an atomic write. */
void Application::saveGame(const fs::path& path)
{
    BCSaveFile candidate = saved;
    settings.board_2d = bothBoards ? flatBeforeBoth : flat;
    settings.sound = animation.soundEnabled;
    if (!bc_save_encode(&candidate, &session.game.position, &settings) ||
        !bc_save_write(path.string().c_str(), &candidate))
    {
        showAlert("Unable to save game.");
        return;
    }
    saved = candidate;
    currentFile = path;
}

/* Native event adapter for READLOAD (file offset 0x8c9e). Commit a
 * decoded file only after validation; do not silently replace unsupported player modes. */
void Application::receiveFileResult(const FileResult& result)
{
    fileWaiting = false;
    if (!result.error.empty())
    {
        showAlert(result.error);
        return;
    }
    if (result.path.empty())
        return;
    if (result.operation == 2)
    {
        saveGame(result.path);
        return;
    }
    if (result.operation == int(FileOperation::exportPgn))
    {
        exportPgnFile(result.path);
        return;
    }
    if (result.operation == int(FileOperation::importPgn))
    {
        importPgnFile(result.path);
        return;
    }
    if (result.operation == int(FileOperation::exportGif))
    {
        exportGifFile(result.path);
        return;
    }
    BCSaveFile candidate{};
    Position position{};
    BCSaveSettings loaded{};
    if (!bc_save_read(result.path.c_str(), &candidate, &position, &loaded))
    {
        showAlert("Unable to open game.");
        return;
    }
    // Preserve unsupported mode data by refusing the load, rather than changing the game
    // silently.
    if (loaded.white_player > 2 || loaded.black_player > 2)
    {
        showAlert("Unsupported player control in saved game.");
        return;
    }
    if (loaded.level > 10)
    {
        showAlert("Unsupported level in saved game.");
        return;
    }
    computer.cancel();
    hint.reset();
    computerEnding = 0;
    animation.cancel();
    session = GameSession();
    // LOADREQ expands the board before GETSETTI replaces player controls.
    session.computerRequested = playerForSide(settings, position.side) == 1;
    session.bookEligible = false;
    bc_setup_commit(&session.game, &position);
    saved = candidate;
    sendBoard();
    settings = loaded;
    if (bothBoards)
        flatBeforeBoth = loaded.board_2d;
    else
        flat = loaded.board_2d;
    animation.soundEnabled = loaded.sound;
    currentFile = result.path;
    // GETSETTI (0x74b4) invokes SETLEVEL. Custom minutes are not stored in
    // the 78-byte file, so loading level 10 opens SETTIME again.
    if (loaded.level == 10)
        openTimeDialog();
    else
        bc_time_select_level(&thinkingTime, loaded.level);
}

/* Native PGN import; no original entry point. Load the main line as committed moves
 * so Take Back and Replay can browse it; the board shows the final position. */
void Application::importPgnFile(const fs::path& path)
{
    std::ifstream in(path, std::ios::binary);
    std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (!in && !in.eof())
    {
        showAlert("Unable to open PGN.");
        return;
    }
    PgnImport imported = importPgn(text);
    if (!imported.error.empty())
    {
        showAlert("Unable to import PGN. " + imported.error);
        return;
    }
    resetGame();
    session.game = imported.start;
    for (const BCMove& move : imported.moves)
        if (!session.apply(move, false))
            throw std::runtime_error("Imported PGN move rejected after validation");
    session.bookEligible = false;
    // A finished game stays on screen: no ending alert, whose OK would reset the board.
    BCOutcome outcome = bc_adjudicate(&session.game);
    session.endingAnnounced = outcome == BC_CHECKMATE || outcome == BC_STALEMATE;
    session.computerRequested =
        !session.endingAnnounced && playerForSide(settings, session.game.position.side) == 1;
    sendBoard();
}

/* Native OS picker; no original entry point. Preserve the original save data format. */
void Application::openFileDialog(bool write)
{
    fileWaiting = true;
    showFileDialog(window, write, currentFile.string());
}

/* Native PGN picker; no original entry point. Suggest the saved game's name. */
void Application::openPgnDialog()
{
    fileWaiting = true;
    fs::path suggestion = currentFile.empty()
                              ? fs::path("Battle Chess.pgn")
                              : fs::path(currentFile).replace_extension(".pgn");
    showPgnExportDialog(window, suggestion.string());
}

/* Native PGN export; no original entry point. The 78-byte save holds only the
 * position, so the moves come from this session's undo snapshots. */
void Application::exportPgnFile(fs::path path)
{
    if (path.extension().empty())
        path += ".pgn";
    static const char* const players[] = {"Human", "Mac", "Modem"};
    PgnTags tags;
    if (std::time_t now = std::time(nullptr); auto* local = std::localtime(&now))
    {
        char date[16];
        std::strftime(date, sizeof date, "%Y.%m.%d", local);
        tags.date = date;
    }
    tags.white = settings.white_player < 3 ? players[settings.white_player] : "?";
    tags.black = settings.black_player < 3 ? players[settings.black_player] : "?";
    std::string pgn = exportPgn(session.past, session.game, tags);
    fs::path temporary = path;
    temporary += ".tmp";
    std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
    bool written = !pgn.empty() && out && (out << pgn) && (out.close(), !out.fail());
    std::error_code error;
    if (!written || (fs::rename(temporary, path, error), error))
    {
        fs::remove(temporary, error);
        showAlert("Unable to export PGN.");
    }
}

/* Native menu availability; no direct original entry point. Protect modal
 * transitions while allowing force/cancel commands during asynchronous search. */
bool Application::menuItemEnabled(int m, int item)
{
    if (modem.busy() || fileWaiting || alertId || textDialog.open || lichess.lobbyOpen ||
        animation.busy() || session.pending || !session.promotion.empty())
        return false;
    if (editing)
        return m == 5 && item >= 0 && item < 3;
    if (m == kLichessMenu)
        return lichessMenuVisible() && lichessItemEnabled(item);
    // Online play: only commands that cannot help, undo or replace the game.
    if (lichess.inGame)
        return (m == 0 && (item == 6 || item == kExportPgnItem)) ||
            (m == 2 && (item == 0 || (item == 1 && !bothBoards) ||
                item == kAnimationSpeedItem || item == kBothBoardsItem ||
                item == kFullScreenItem || item == kCoordinatesItem ||
                item == kCapturedItem || (item == kRotateItem && !autoRotate) ||
                item == kAutoRotateItem || item == kRotate3DItem)) ||
            (m == 4 && item == 0);
    if (computer.busy())
        return (m == 1 && item == 0) ||
            (m == 0 && (item == 0 || item == 1 || item == 6 || item == kExportPgnItem ||
                item == kImportPgnItem)) ||
            (m == 2 && (item == 2 || item == 3 || item == 5 || item == 6 ||
                item == kFullScreenItem || item == kIdleAnimationsItem ||
                item == kCoordinatesItem || item == kCapturedItem ||
                (item == kRotateItem && !autoRotate) || item == kAutoRotateItem ||
                item == kRotate3DItem));
    return (m == 0 && (item <= 4 || item == 6 || item == kExportPgnItem ||
            item == kImportPgnItem || item == kExportGifItem)) ||
        (m == 1 &&
            ((item == 0 && playerForSide(settings, session.game.position.side) == 1) || item == 3 ||
                (item == 1 && !session.past.empty()) || (item == 2 && !session.future.empty()))) ||
        (m == 2 &&
            (item == 0 || (item == 1 && !bothBoards) || item == 2 || item == 3 || item == 5 ||
                item == 6 || (modem.connected() && (item == 4 || item == 7)) ||
                item == kAnimationSpeedItem || item == kBothBoardsItem ||
                item == kFullScreenItem || item == kIdleAnimationsItem ||
                item == kCoordinatesItem || item == kCapturedItem ||
                (item == kRotateItem && !autoRotate) || item == kAutoRotateItem ||
                item == kRotate3DItem)) ||
        (m == 6 && modem.connected() && item >= 0 && item < 4) || (m == 4 && item == 0) ||
        (m == 3 && item >= 0 && item <= 10);
}

/* Native dispatch for CHECKOPT (file offset 0x58bc) and BOARDSET
 * (0x2d9c). Route original menu items to recovered behavior or platform adapters. */
void Application::performMenuAction(int m, int item)
{
    if (!menuItemEnabled(m, item))
        return;
    // HANDLEME 0x10604 clears menu highlighting for both MenuSelect and MenuKey.
    menu = menuItem = speedMenuItem = -1;
    if (m == 0)
    {
        if (item == 0)
        {
            alertId = 402;
            message = original_message_new_game;
            confirmAction = 1;
        }
        if (item == 1)
            openFileDialog(false);
        if (item == 2)
        {
            if (currentFile.empty())
                openFileDialog(true);
            else
                saveGame(currentFile);
        }
        if (item == 3)
            openFileDialog(true);
        if (item == 4)
        {
            computer.cancel();
            hint.reset();
            computerEnding = 0;
            animation.cancel();
            previousFlat = flat;
            flat = true;
            editing = true;
            setup.begin(session.game.position);
            session.selected = -1;
        }
        if (item == 6)
            requestQuit();
        if (item == kExportPgnItem)
            openPgnDialog();
        if (item == kImportPgnItem)
        {
            fileWaiting = true;
            showPgnImportDialog(window);
        }
        if (item == kExportGifItem)
            openGifDialog();
    }
    if (m == kLichessMenu)
        lichessMenuAction(item);
    if (m == 1 && item == 0)
    {
        if (computer.busy())
            computer.force();
        else
            session.computerRequested = true;
    }
    if (m == 1 && item == 3)
    {
        hint.reset();
        session.selected = -1;
        session.computerRequested = false;
        computer.start(session.game, session.past, session.bookEligible, settings.level,
                       bc_time_search_seconds(&thinkingTime), true);
    }
    if (m == 1 && item == 1)
    {
        computer.cancel();
        hint.reset();
        computerEnding = 0;
        animation.cancel();
        session.mateAnimationDone = false;
        session.undo();
    }
    if (m == 1 && item == 2)
    {
        computer.cancel();
        hint.reset();
        computerEnding = 0;
        animation.cancel();
        session.mateAnimationDone = false;
        session.replay();
    }
    if (m == 2 && item == 0)
        animation.soundEnabled = !animation.soundEnabled;
    if (m == 2 && item == kAnimationSpeedItem)
    {
        menu = 2;
        menuItem = kAnimationSpeedItem;
    }
    if (m == 2 && item == 1)
    {
        animation.cancel();
        flat = !flat;
        session.selected = -1;
    }
    if (m == 2 && item == kBothBoardsItem)
        setBothBoards(!bothBoards);
    if (m == 2 && item == kFullScreenItem)
        toggleFullscreen();
    if (m == 2 && item == kCoordinatesItem)
        showCoordinates = !showCoordinates;
    if (m == 2 && item == kRotateItem)
    {
        rotateFlat = !rotateFlat;
        session.selected = -1;
    }
    if (m == 2 && item == kAutoRotateItem)
        autoRotate = !autoRotate; // Turning it off keeps the current orientation.
    if (m == 2 && item == kRotate3DItem)
    {
        rotate3D = !rotate3D;
        session.selected = -1;
    }
    if (m == 2 && item == kCapturedItem)
    {
        showCaptured = !showCaptured;
        applyLayout(true);
    }
    if (m == 2 && item == kIdleAnimationsItem)
        idle.setEnabled(!idle.enabled(), SDL_GetTicks());
    if (m == 2 && item >= 2 && item <= 7)
    {
        computer.cancel();
        hint.reset();
        session.selected = -1;
        if (item < 5)
            settings.white_player = uint8_t(item - 2);
        else
            settings.black_player = uint8_t(item - 5);
        if (item == 4 || item == 7)
            modem.text("ATE\r");
        session.computerRequested = playerForSide(settings, session.game.position.side) == 1;
    }
    if (m == 6)
    {
        if (item == 0 || item == 3)
        {
            textDialog.resourceId = item == 0 ? 405 : 404;
            textDialog.text.clear();
            textDialog.cursor = textDialog.anchor = 0;
            textDialog.open = true;
            require(SDL_StartTextInput(window), "Start modem input");
        }
        if (item == 1)
            modem.hangUp();
        if (item == 2)
        {
            autoAnswer = !autoAnswer;
            modem.text(autoAnswer ? "ATS0=1\r" : "ATS0=0\r");
        }
    }
    if (m == 3)
    {
        if (item == 10)
            openTimeDialog();
        else if (bc_time_select_level(&thinkingTime, unsigned(item)))
            settings.level = thinkingTime.level;
    }
    if (m == 4)
    {
        alertId = 400;
        message.clear();
    }
    if (m == 5)
    {
        if (item == 0)
        {
            alertId = 402;
            message = original_message_clear_board;
            confirmAction = 2;
        }
        if (item == 1)
            setup.restore();
        if (item == 2)
        {
            if (auto error = setup.done(session.game))
                showAlert(error);
            else
            {
                session.past.clear();
                session.future.clear();
                session.bookEligible = false;
                session.computerRequested =
                    playerForSide(settings, session.game.position.side) == 1;
                session.mateAnimationDone = false;
                session.endingAnnounced = false;
                editing = false;
                flat = previousFlat;
                sendBoard();
            }
        }
    }
}

/* Native alert acknowledgement; DOCHECKM (file offset 0x716a) calls
 * the reset at 0x5692 after terminal messages. Cancel must not change the board.
 * Native change: outside modem games the finished board stays on screen (for PGN/GIF
 * export, Take Back and review) until New Game; modem games keep the original reset,
 * which both machines perform together. */
void Application::dismissAlert(int item)
{
    dialogButton.cancel();
    if (item == 1 && confirmAction == 3 && modem.connected() &&
        (settings.white_player == 2 || settings.black_player == 2) && !modem.ended())
    {
        waitingForRemoteEnd = true;
        return;
    }
    int command = confirmAction;
    alertId = 0;
    confirmAction = 0;
    if (command >= kConfirmAcceptChallenge)
    {
        lichessDismissAlert(command, item);
        return;
    }
    if (item == 1)
    {
        if (command == 1)
        {
            resetGame();
            sendBoard();
        }
        if (command == 2)
            setup.clear();
        if (command == 3 && modem.connected() &&
            (settings.white_player == 2 || settings.black_player == 2))
            resetGame();
    }
}

/* Native MenuSelect adapter (original call 0x103f6): hit-test visible headings. */
int Application::menuHeadingAt(int x, int y)
{
    if (y < 0 || y >= 20)
        return -1;
    for (int i = 0; i < kMenuCount; ++i)
        if ((editing
                 ? i == 5
                 : i != 5 && (i != 6 || modem.connected()) &&
                 (i != kLichessMenu || lichessMenuVisible())) &&
            x >= menuX[i] && x<menuX[i] + menuWidth[i])
            return i;
    return -1;
}

SDL_Rect Application::speedMenuBounds() const
{
    return {menuX[2] + 249, 22 + kAnimationSpeedItem * 18, 128, 5 * 18 + 4};
}

/* Native MenuSelect adapter (0x103f6): track headings and enabled rows while held. */
void Application::trackMenu(int x, int y)
{
    if (menu < 0)
        return;
    int heading = menuHeadingAt(x, y);
    if (heading >= 0)
        menu = heading;
    speedMenuItem = -1;
    if (menu == 2 && menuItem == kAnimationSpeedItem && menuItemEnabled(2, kAnimationSpeedItem))
    {
        auto bounds = speedMenuBounds();
        if (x >= bounds.x && x < bounds.x + bounds.w && y >= bounds.y && y < bounds.y + bounds.h)
        {
            if (y >= bounds.y + 2 && y < bounds.y + 2 + 5 * 18)
                speedMenuItem = (y - bounds.y - 2) / 18;
            return;
        }
    }
    menuItem = -1;
    if (x >= menuLeft(menu) && x < menuLeft(menu) + 250 && y >= 22)
    {
        int item = (y - 22) / 18;
        if (item < menuResource(menu).count && menuItemEnabled(menu, item) &&
            std::string(menuResource(menu).items[item].label) != "-")
            menuItem = item;
    }
}

/* Native ModalDialog/MenuSelect adapter (0x3894/0x103f6): dispatch mouse
 * commands only on release; a drag cannot activate a different button.
 * Native extension: releasing over a menu title keeps that menu open, so a
 * second click selects an item (press-drag-release still works). */
void Application::handlePointerRelease(int x, int y)
{
    if (textDialog.open && textDialog.resourceId >= kTokenPrompt)
    {
        lichessPromptRelease(x, y);
        return;
    }
    if (lichess.lobbyOpen && !alertId && !textDialog.open)
    {
        lichessLobbyRelease(x, y);
        return;
    }
    const auto* dialog = textDialog.open
                             ? findResourceDialog(textDialog.resourceId, ResourceDialogKind::dialog)
                             : alertId
                             ? findResourceDialog(alertId, ResourceDialogKind::alert)
                             : nullptr;
    int item = dialogButton.release(dialog, x, y);
    if (dialog)
    {
        if (item == 1 && textDialog.open)
            acceptTextDialog();
        else if (item == 2 && textDialog.open && textDialog.resourceId == 404)
        {
            textDialog.open = false;
            require(SDL_StopTextInput(window), "Stop modem input");
        }
        else if (alertId && item)
            dismissAlert(item);
        return;
    }
    if (menu < 0)
        return;
    if (!menuSticky && menuHeadingAt(x, y) >= 0)
    {
        trackMenu(x, y);
        menuItem = speedMenuItem = -1;
        menuSticky = true;
        return;
    }
    trackMenu(x, y);
    int selectedMenu = menu, selectedItem = menuItem;
    int selectedSpeed = speedMenuItem;
    menu = menuItem = speedMenuItem = -1;
    if (selectedMenu == 2 && selectedItem == kAnimationSpeedItem && selectedSpeed >= 0 &&
        menuItemEnabled(2, kAnimationSpeedItem))
    {
        animation.speed = 1 + selectedSpeed * 0.25;
        return;
    }
    if (selectedItem >= 0)
        performMenuAction(selectedMenu, selectedItem);
    // Animation Speed reopens its submenu; keep it open for a click there.
    menuSticky = menu >= 0;
}

/* Native side-by-side hit test; no original entry point. The cropped right view
 * maps back to flat-board content coordinates. */
int Application::boardSquareAt(int x, int y) const
{
    // A rotated 2D board shows display square 63 - i where square i would be.
    auto flatSquare = [&](int square) { return square >= 0 && rotateFlat ? 63 - square : square; };
    if (dualView() && x >= logicalWidth)
        return x >= flatViewOrigin() && x < flatViewOrigin() + kFlatViewWidth
                   ? flatSquare(original_hit_square(x - flatViewOrigin() + kFlatViewLeft, y - 20,
                                                    true))
                   : -1;
    int square = original_hit_square(x, y - 20, flat);
    if (!flat && rotated3D() && square >= 0)
        return 63 - square;
    return flat ? flatSquare(square) : square;
}

/* Native side-by-side toggle; no original entry point. The perspective board
 * hosts the recovered animations, so the left view is always 3D while active. */
void Application::setBothBoards(bool on)
{
    if (on == bothBoards)
        return;
    if (on)
        flatBeforeBoth = flat;
    bool target = on ? false : flatBeforeBoth;
    if (flat != target)
    {
        animation.cancel();
        flat = target;
    }
    bothBoards = on;
    session.selected = -1;
    applyLayout(true);
}

/* Native idle gate; no original entry point. Idle clips play only on the 3D board,
 * on a human turn, while nothing else animates or waits for an answer. */
bool Application::idleAllowed() const
{
    // Idle clips are placed in White-side screen squares; not yet for a rotated 3D board.
    return !flat && !rotated3D() && !editing && !lichess.inGame && !animation.busy() && !session.pending &&
        !session.promotionWalk && !session.mateAnimationDone && session.promotion.empty() &&
        menu < 0 && !alertId && !textDialog.open && !fileWaiting && !computer.busy() &&
        !modem.busy() && playerForSide(settings, session.game.position.side) == 0 &&
        bc_adjudicate(&session.game) == BC_ONGOING;
}

/* Native over-the-board rotation; no original entry point. The 2D board (and the 3D
 * board with Rotate 3D Board Too) faces the
 * player at the Mac: the side to move when both sides are human, the human side against
 * the Mac, the player's colour on Lichess. It turns only between moves, never while a
 * move animates, and not during board setup. */
void Application::updateAutoRotate()
{
    if (!autoRotate || editing || animation.busy() || session.pending || session.promotionWalk)
        return;
    bool black;
    if (lichess.inGame)
        black = lichess.color == "black";
    else if (settings.white_player == 0 && settings.black_player == 0)
        black = session.game.position.side != 0;
    else
        black = settings.black_player == 0 && settings.white_player != 0;
    if (rotateFlat != black)
    {
        rotateFlat = black;
        session.selected = -1;
    }
}

void Application::updateIdle()
{
    uint8_t display[64];
    setup_display_board(&session.game.position, display);
    idle.update(SDL_GetTicks(), idleAllowed(), display, animation.speed);
}

/* Native hit-test dispatcher; no single original entry point. Modal
 * dialogs and setup consume clicks before ordinary board interaction. */
void Application::handlePointerPress(int x, int y)
{
    if (fileWaiting || modem.busy())
        return;
    if (textDialog.open && textDialog.resourceId >= kTokenPrompt)
    {
        lichessPromptPress(x, y);
        return;
    }
    // Online: a click never waits for an animation; it finishes it and carries on.
    if (lichess.inGame && !alertId && !textDialog.open &&
        (animation.busy() || (session.pending && !session.promotionWalk)))
        finishLichessAnimation();
    if (textDialog.open)
    {
        const auto& dialog = *findResourceDialog(textDialog.resourceId, ResourceDialogKind::dialog);
        dialogButton.press(dialog, x, y);
        int item = hitResourceDialog(dialog, x, y);
        if (item == (textDialog.resourceId == 404 ? 4 : 2))
        {
            const auto& field =
                original_dialog_items[dialog.first + (textDialog.resourceId == 404 ? 3 : 1)];
            int relativeX = x - dialog.left - field.left - 4;
            textDialog.cursor = textDialog.anchor =
                chicago_available
                    ? macBitmapCaret(chicago_glyphs, textDialog.text, relativeX)
                    : std::min(textDialog.text.size(), size_t(std::max(0, relativeX / 8)));
        }
        return;
    }
    if (alertId)
    {
        auto* d = findResourceDialog(alertId, ResourceDialogKind::alert);
        dialogButton.press(*d, x, y);
        return;
    }
    if (lichess.lobbyOpen)
    {
        lichessLobbyPress(x, y);
        return;
    }
    if (animation.busy() || session.pending)
        return;
    if (!session.promotion.empty())
    {
        auto* d = findResourceDialog(408, ResourceDialogKind::dialog);
        int item = hitResourceDialog(*d, x, y);
        const int piece[] = {4, 5, 2, 3};
        if (item >= 2 && item <= 5)
            for (auto m : session.promotion)
                if (m.piece == piece[item - 2])
                {
                    animation.cancel();
                    sendMove(m, session.game.position.side);
                    if (session.apply(m, false))
                        session.computerRequested =
                            playerForSide(settings, session.game.position.side) == 1;
                    return;
                }
        return;
    }
    if (y < 20)
    {
        int heading = menuHeadingAt(x, y);
        // Clicking the title of an open click-opened menu closes it.
        bool close = menuSticky && menu >= 0 && heading == menu;
        menu = close ? -1 : heading;
        menuItem = speedMenuItem = -1;
        menuSticky = false;
        return;
    }
    if (menu >= 0)
        return;
    if (dualView() && y >= 20 + original_content_height)
    {
        handlePanelPress(x, y);
        return;
    }
    if (y >= 20 + original_content_height)
        return; // The captured strip under the single board.
    int display = boardSquareAt(x, y);
    if (editing)
    {
        if (auto error = setup.click(x, y - 20, display < 0 ? -1 : display_to_engine(display)))
            showAlert(error);
        return;
    }
    // Only a live online game restricts the board; afterwards it is an ordinary board again.
    if (lichess.inGame && !lichessOwnTurn())
        return;
    // A finished game stays on the board as it ended; Take Back or New Game continue.
    if (session.endingAnnounced)
        return;
    // READOPTI 0x5812 permits board input after history browsing, even on a Mac side.
    if (!lichess.inGame && playerForSide(settings, session.game.position.side) == 2)
        return;
    if (computer.busy() ||
        (session.computerRequested && playerForSide(settings, session.game.position.side) == 1))
        return;
    hint.reset();
    if (session.mateAnimationDone || bc_adjudicate(&session.game) == BC_STALEMATE)
        return;
    unsigned previousSide = session.game.position.side;
    session.click(display);
    if (session.game.position.side != previousSide && lichess.inGame)
    {
        // The player's own online move follows the same animation policy, and is sent at
        // once when it does not animate.
        const BCMove move = *session.pending;
        BCGame before{};
        before.position = session.animationBefore;
        bool capture = before.position.board[move.to].piece ||
        (before.position.board[move.from].piece == 6 &&
            (move.from & 7) != (move.to & 7));
        auto style = chooseLichessAnimation(lichess.policy, lichess.speed,
                                            lichess.meta.clockInitial > 0,
                                            lichessClock(lichess.color == "white"), capture);
        if (style == LichessMoveAnimation::none)
        {
            sendMove(move, previousSide);
            session.pending.reset();
        }
        else
        {
            lichess.fastPending = style == LichessMoveAnimation::fast;
        }
        return;
    }
    if (session.game.position.side != previousSide)
    {
        // Playing on after an online game makes it a local game again.
        lichess.onlineSession = false;
        session.computerRequested = playerForSide(settings, session.game.position.side) == 1;
    }
}
