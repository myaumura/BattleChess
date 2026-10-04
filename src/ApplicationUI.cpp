#include "ApplicationInternal.h"
#include "mac_bitmap_font.h"
#include "presentation.h"
#include "adjudication.h"
#include <cstdlib>
#include <stdexcept>

/* Native MENU-resource lookup; no original address. Keep titles, actions
 * and shortcuts tied to the extracted menu inventory. */
const OriginalMenu &Application::menuResource(int index) {
    for (auto &m : original_menus)
        if (m.id == menuIds[index])
            return m;
    throw std::runtime_error("Missing MENU");
}

/* Native host for CHESSALE (file offset 0x3aba), using ALRT 406.
 * Queue a message instead of blocking the SDL event loop. */
void Application::showAlert(const std::string &value) {
    dialogButton.cancel();
    alertId = 406;
    message = value;
    confirmAction = 0;
}

/* Native host for SETTIME, file offset 0x381e. DLOG 403 starts with all
 * existing minute text selected, so typing replaces the old value. */
void Application::openTimeDialog() {
    dialogButton.cancel();
    textDialog.resourceId = 403;
    textDialog.text = std::to_string(bc_time_dialog_minutes(&thinkingTime));
    textDialog.selectAll();
    textDialog.open = true;
    require(SDL_StartTextInput(window), "Start time input");
}
/* Native dialog completion for SETTIME (0x381e) / SETLEVEL (0x6838).
 * The original dialog has OK only; clamp numeric input before storing seconds. */
void Application::acceptTextDialog() {
    dialogButton.cancel();
    if (textDialog.resourceId != 403) {
        if (!textDialog.text.empty()) {
            if (textDialog.resourceId == 405)
                modem.text("ATDT " + textDialog.text + "\r");
            else
                modem.text(textDialog.text == "+++" ? textDialog.text
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
void Application::handleTextKey(SDL_Keycode key, SDL_Keymod modifiers) {
    if (key == SDLK_RETURN || key == SDLK_KP_ENTER)
        acceptTextDialog();
    else
        textDialog.key(key, modifiers);
}

/* Native file adapter for SAVEREQ (file offset 0x8d34). Preserve opaque
 * original bytes and change the active filename only after an atomic write. */
void Application::saveGame(const fs::path &path) {
    BCSaveFile candidate = saved;
    settings.board_2d = flat;
    settings.sound = animation.sound_enabled;
    if (!bc_save_encode(&candidate, &session.game.position, &settings) ||
        !bc_save_write(path.string().c_str(), &candidate)) {
        showAlert("Unable to save game.");
        return;
    }
    saved = candidate;
    currentFile = path;
}

/* Native event adapter for READLOAD (file offset 0x8c9e). Commit a
 * decoded file only after validation; do not silently replace unsupported player modes. */
void Application::receiveFileResult(const FileResult &result) {
    fileWaiting = false;
    if (!result.error.empty()) {
        showAlert(result.error);
        return;
    }
    if (result.path.empty())
        return;
    if (result.operation == 2) {
        saveGame(result.path);
        return;
    }
    BCSaveFile candidate{};
    Position position{};
    BCSaveSettings loaded{};
    if (!bc_save_read(result.path.c_str(), &candidate, &position, &loaded)) {
        showAlert("Unable to open game.");
        return;
    }
    // Preserve unsupported mode data by refusing the load, rather than changing the game
    // silently.
    if (loaded.white_player > 2 || loaded.black_player > 2) {
        showAlert("Unsupported player control in saved game.");
        return;
    }
    if (loaded.level > 10) {
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
    flat = loaded.board_2d;
    animation.sound_enabled = loaded.sound;
    currentFile = result.path;
    // GETSETTI (0x74b4) invokes SETLEVEL. Custom minutes are not stored in
    // the 78-byte file, so loading level 10 opens SETTIME again.
    if (loaded.level == 10)
        openTimeDialog();
    else
        bc_time_select_level(&thinkingTime, loaded.level);
}

/* Native OS picker; no original entry point. Preserve the original save data format. */
void Application::openFileDialog(bool write) {
    fileWaiting = true;
    showFileDialog(window, write, currentFile.string());
}


/* Native menu availability; no direct original entry point. Protect modal
 * transitions while allowing force/cancel commands during asynchronous search. */
bool Application::menuItemEnabled(int m, int item) {
    if (modem.busy() || fileWaiting || alertId || textDialog.open || animation.busy() ||
        session.pending || !session.promotion.empty())
        return false;
    if (editing)
        return m == 5 && item >= 0 && item < 3;
    if (computer.busy())
        return (m == 1 && item == 0) || (m == 0 && (item == 0 || item == 1 || item == 6)) ||
               (m == 2 && (item == 2 || item == 3 || item == 5 || item == 6));
    return (m == 0 && (item <= 4 || item == 6)) ||
           (m == 1 &&
            ((item == 0 && playerForSide(settings, session.game.position.side) == 1) || item == 3 ||
             (item == 1 && !session.past.empty()) || (item == 2 && !session.future.empty()))) ||
           (m == 2 && (item == 0 || item == 1 || item == 2 || item == 3 || item == 5 || item == 6 ||
                       (modem.connected() && (item == 4 || item == 7)))) ||
           (m == 6 && modem.connected() && item >= 0 && item < 4) || (m == 4 && item == 0) ||
           (m == 3 && item >= 0 && item <= 10);
}

/* Native dispatch for CHECKOPT (file offset 0x58bc) and BOARDSET
 * (0x2d9c). Route original menu items to recovered behavior or platform adapters. */
void Application::performMenuAction(int m, int item) {
    if (!menuItemEnabled(m, item))
        return;
    // HANDLEME 0x10604 clears menu highlighting for both MenuSelect and MenuKey.
    menu = menuItem = -1;
    if (m == 0) {
        if (item == 0) {
            alertId = 402;
            message = original_message_new_game;
            confirmAction = 1;
        }
        if (item == 1)
            openFileDialog(false);
        if (item == 2) {
            if (currentFile.empty())
                openFileDialog(true);
            else
                saveGame(currentFile);
        }
        if (item == 3)
            openFileDialog(true);
        if (item == 4) {
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
    }
    if (m == 1 && item == 0) {
        if (computer.busy())
            computer.force();
        else
            session.computerRequested = true;
    }
    if (m == 1 && item == 3) {
        hint.reset();
        session.selected = -1;
        session.computerRequested = false;
        computer.start(session.game, session.past, session.bookEligible, settings.level,
                       bc_time_search_seconds(&thinkingTime), true);
    }
    if (m == 1 && item == 1) {
        computer.cancel();
        hint.reset();
        computerEnding = 0;
        animation.cancel();
        session.mateAnimationDone = false;
        session.undo();
    }
    if (m == 1 && item == 2) {
        computer.cancel();
        hint.reset();
        computerEnding = 0;
        animation.cancel();
        session.mateAnimationDone = false;
        session.replay();
    }
    if (m == 2 && item == 0)
        animation.sound_enabled = !animation.sound_enabled;
    if (m == 2 && item == 1) {
        animation.cancel();
        flat = !flat;
        session.selected = -1;
    }
    if (m == 2 && item >= 2 && item <= 7) {
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
    if (m == 6) {
        if (item == 0 || item == 3) {
            textDialog.resourceId = item == 0 ? 405 : 404;
            textDialog.text.clear();
            textDialog.cursor = textDialog.anchor = 0;
            textDialog.open = true;
            require(SDL_StartTextInput(window), "Start modem input");
        }
        if (item == 1)
            modem.hang_up();
        if (item == 2) {
            autoAnswer = !autoAnswer;
            modem.text(autoAnswer ? "ATS0=1\r" : "ATS0=0\r");
        }
    }
    if (m == 3) {
        if (item == 10)
            openTimeDialog();
        else if (bc_time_select_level(&thinkingTime, unsigned(item)))
            settings.level = thinkingTime.level;
    }
    if (m == 4) {
        alertId = 400;
        message.clear();
    }
    if (m == 5) {
        if (item == 0) {
            alertId = 402;
            message = original_message_clear_board;
            confirmAction = 2;
        }
        if (item == 1)
            setup.restore();
        if (item == 2) {
            if (auto error = setup.done(session.game))
                showAlert(error);
            else {
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
 * the reset at 0x5692 after terminal messages. Cancel must not change the board. */
void Application::dismissAlert(int item) {
    dialogButton.cancel();
    if (item == 1 && confirmAction == 3 && modem.connected() &&
        (settings.white_player == 2 || settings.black_player == 2) && !modem.ended()) {
        waitingForRemoteEnd = true;
        return;
    }
    int command = confirmAction;
    alertId = 0;
    confirmAction = 0;
    if (item == 1) {
        if (command == 1) {
            resetGame();
            sendBoard();
        }
        if (command == 2)
            setup.clear();
        if (command == 3)
            resetGame();
    }
}

/* Native MenuSelect adapter (original call 0x103f6): hit-test visible headings. */
int Application::menuHeadingAt(int x, int y) {
    if (y < 0 || y >= 20)
        return -1;
    for (int i = 0; i < 7; ++i)
        if ((editing ? i == 5 : i != 5 && (i != 6 || modem.connected())) && x >= menuX[i] &&
            x < menuX[i] + menuWidth[i])
            return i;
    return -1;
}

/* Native MenuSelect adapter (0x103f6): track headings and enabled rows while held. */
void Application::trackMenu(int x, int y) {
    if (menu < 0)
        return;
    int heading = menuHeadingAt(x, y);
    if (heading >= 0)
        menu = heading;
    menuItem = -1;
    if (x >= menuX[menu] && x < menuX[menu] + 250 && y >= 22) {
        int item = (y - 22) / 18;
        if (item < menuResource(menu).count && menuItemEnabled(menu, item) &&
            std::string(menuResource(menu).items[item].label) != "-")
            menuItem = item;
    }
}

/* Native ModalDialog/MenuSelect adapter (0x3894/0x103f6): dispatch mouse
 * commands only on release; a drag cannot activate a different button. */
void Application::handlePointerRelease(int x, int y) {
    const auto *dialog = textDialog.open ? resource_dialog_find(textDialog.resourceId, false)
                         : alertId       ? resource_dialog_find(alertId, true)
                                         : nullptr;
    int item = dialogButton.release(dialog, x, y);
    if (dialog) {
        if (item == 1 && textDialog.open)
            acceptTextDialog();
        else if (item == 2 && textDialog.open && textDialog.resourceId == 404) {
            textDialog.open = false;
            require(SDL_StopTextInput(window), "Stop modem input");
        } else if (alertId && item)
            dismissAlert(item);
        return;
    }
    if (menu < 0)
        return;
    trackMenu(x, y);
    int selectedMenu = menu, selectedItem = menuItem;
    menu = menuItem = -1;
    if (selectedItem >= 0)
        performMenuAction(selectedMenu, selectedItem);
}

/* Native hit-test dispatcher; no single original entry point. Modal
 * dialogs and setup consume clicks before ordinary board interaction. */
void Application::handlePointerPress(int x, int y) {
    if (fileWaiting || modem.busy())
        return;
    if (textDialog.open) {
        const auto &dialog = *resource_dialog_find(textDialog.resourceId, false);
        dialogButton.press(dialog, x, y);
        int item = resource_dialog_hit(dialog, x, y);
        if (item == (textDialog.resourceId == 404 ? 4 : 2)) {
            const auto &field =
                original_dialog_items[dialog.first + (textDialog.resourceId == 404 ? 3 : 1)];
            int relativeX = x - dialog.left - field.left - 4;
            textDialog.cursor = textDialog.anchor =
                chicago_available
                    ? mac_bitmap_caret(chicago_glyphs, textDialog.text, relativeX)
                    : std::min(textDialog.text.size(), size_t(std::max(0, relativeX / 8)));
        }
        return;
    }
    if (alertId) {
        auto *d = resource_dialog_find(alertId, true);
        dialogButton.press(*d, x, y);
        return;
    }
    if (animation.busy() || session.pending)
        return;
    if (!session.promotion.empty()) {
        auto *d = resource_dialog_find(408, false);
        int item = resource_dialog_hit(*d, x, y);
        const int piece[] = {4, 5, 2, 3};
        if (item >= 2 && item <= 5)
            for (auto m : session.promotion)
                if (m.piece == piece[item - 2]) {
                    animation.cancel();
                    sendMove(m, session.game.position.side);
                    if (session.apply(m, false))
                        session.computerRequested =
                            playerForSide(settings, session.game.position.side) == 1;
                    return;
                }
        return;
    }
    if (y < 20) {
        menu = menuHeadingAt(x, y);
        menuItem = -1;
        return;
    }
    if (menu >= 0)
        return;
    int display = original_hit_square(x, y - 20, flat);
    if (editing) {
        if (auto error = setup.click(x, y - 20, display < 0 ? -1 : display_to_engine(display)))
            showAlert(error);
        return;
    }
    // READOPTI 0x5812 permits board input after history browsing, even on a Mac side.
    if (playerForSide(settings, session.game.position.side) == 2 || computer.busy() ||
        (session.computerRequested && playerForSide(settings, session.game.position.side) == 1))
        return;
    hint.reset();
    if (session.mateAnimationDone || bc_adjudicate(&session.game) == BC_STALEMATE)
        return;
    unsigned previousSide = session.game.position.side;
    session.click(display);
    if (session.game.position.side != previousSide)
        session.computerRequested = playerForSide(settings, session.game.position.side) == 1;
}
