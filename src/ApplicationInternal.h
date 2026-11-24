#ifndef BC_APPLICATION_INTERNAL_H
#define BC_APPLICATION_INTERNAL_H

#include "AnimationHost.h"
#include "ComputerPlayer.h"
#include "FileDialog.h"
#include "GameSession.h"
#include "IdleAnimator.h"
#include "LichessProtocol.h"
#include "ModemHost.h"
#include "ResourceDialog.h"
#include "save_game.h"
#include "SDLHelpers.h"
#include "SetupUI.h"
#include "TextDialog.h"
#include "time_control.h"
#include <algorithm>
#include <deque>
#include <memory>

class LichessClient;
struct LichessMessage;

/* Native Lichess session state (ApplicationLichess.cpp); no original counterpart.
 * The SDL thread owns all of it; the client only moves bytes. */
struct LichessState {
    std::unique_ptr<LichessClient> client;
    bool offline = false;            // Smoke checks record requests instead of sending them.
    std::vector<std::string> outbox; // Recorded requests in offline mode.
    std::string token, username;
    bool tokenFromPrompt = false, connecting = false, connected = false;
    Uint64 reconnectEventsAt = 0, reconnectGameAt = 0;
    // The current or last online game.
    std::string gameId, color, opponent, speed;
    bool inGame = false, onlineSession = false, seeking = false;
    LichessGameFull meta;
    LichessGameState state;
    Uint64 stateAt = 0;
    bool opponentOffersDraw = false, drawPrompted = false, opponentGone = false;
    size_t timeControl = 0;
    LichessAnimationPolicy policy = LichessAnimationPolicy::automatic;
    bool fastPending = false; // The pending animation is an online Fast one.
    // A game we asked for (AI, challenge, seek, accepted challenge) opens directly; games
    // already running on the account are offered for resuming instead of taking over.
    bool expectingGame = false, challenging = false;
    // Games in progress on the account (GET /api/account/playing and the event stream).
    struct LobbyGame {
        std::string id, color, opponent, speed;
        bool myTurn = false, boardPlayable = true;
        long long secondsLeft = -1;
    };
    std::vector<LobbyGame> games;
    // The lobby: a modal list of running games and incoming challenges.
    bool lobbyOpen = false, lobbyOnConnect = false;
    int lobbySelection = 0, lobbyPressed = -1, lobbyClickRow = -1;
    Uint64 lobbyClickAt = 0;
    std::string lobbyStatus;
    std::deque<LichessChallenge> challenges;
    std::string promptedChallenge;
    LichessState();
    ~LichessState();
    LichessState &operator=(LichessState &&) noexcept;
};

struct ApplicationOptions {
    fs::path data, screenshotPath;
    std::string serialPath;
    bool modemQuitWindow = false;
    bool smoke = false, check = false, flat = false, animationCheck = false, modemCheck = false;
    bool both = false;
};

/* Renderer-owned artwork, loaded once and shared by every host drawing path. */
struct GameTextures {
    GameTextures(SDL_Renderer *, const fs::path &assets, bool check);
    Texture perspective{nullptr, SDL_DestroyTexture}, flatBoard{nullptr, SDL_DestroyTexture};
    std::vector<Texture> standing, flatPieces, promote;
};

/* Internal host interface. These files share one SDL-thread-owned state; the
 * recovered C engine and asynchronous hosts keep their existing interfaces.
 * Window, renderer, textures and animation outlive Application in runApplication. */
class Application {
  public:
    Application(const ApplicationOptions &, SDL_Window *, SDL_Renderer *, GameTextures &,
                AnimationHost &, uint32_t &randomState);
    int run();

  private:
    const ApplicationOptions &options;
    SDL_Window *window;
    SDL_Renderer *rendererPtr;
    GameTextures &textures;
    AnimationHost &animation;
    GameSession session;
    ComputerPlayer computer;
    ModemHost modem;
    Cursor blockedCursor, thinkingCursor, checkedCursor;

    bool flat;
    bool autoAnswer = false, waitingForRemoteEnd = false;
    std::vector<BCMove> remotePromotion;
    uint8_t remotePromotionPiece = 0;
    std::optional<BCMove> hint;
    Uint64 hintStarted = 0;
    unsigned computerEnding = 0;
    int menu = -1, menuItem = -1, speedMenuItem = -1;
    // Native extension: a menu released over its title stays open for click-to-select.
    bool menuSticky = false;
    // Native menu rows appended to File (menu 0) and Settings (menu 2).
    static constexpr int kExportPgnItem = 8, kImportPgnItem = 9, kExportGifItem = 10;
    static constexpr int kAnimationSpeedItem = 9, kBothBoardsItem = 10, kFullScreenItem = 11,
                         kIdleAnimationsItem = 12, kCoordinatesItem = 13, kCapturedItem = 14,
                         kRotateItem = 15, kAutoRotateItem = 16;
    // Native rotated 2D board (Settings → Rotate 2D Board): Black's side at the bottom.
    // With Rotate for Player to Move it follows the player at the Mac instead.
    bool rotateFlat = false, autoRotate = false;
    void updateAutoRotate();
    // Native board aids, on by default (Settings → Show Coordinates / Show Captured Pieces).
    bool showCoordinates = true, showCaptured = true;
    // Captured-pieces strip under the single board, or the bottom row of the panel.
    static constexpr int kCapturedHeight = 26;
    // Native Lichess menu rows.
    enum LichessItem {
        kLichessConnect = 0,
        kLichessForget,
        kLichessGames,
        kLichessAnalyse,
        kLichessSeparator1,
        kLichessPlayAi,
        kLichessChallenge,
        kLichessSeek,
        kLichessTime,
        kLichessAnimations,
        kLichessSeparator2,
        kLichessDraw,
        kLichessResign,
        kLichessLeave,
        kLichessItemCount
    };
    // Native text prompts outside the original dialog resources.
    static constexpr int kTokenPrompt = 900, kUsernamePrompt = 901;
    // Native alert commands beyond the original ones.
    static constexpr int kConfirmAcceptChallenge = 20, kConfirmAcceptDraw = 21,
                         kConfirmResign = 22;
    LichessState lichess;
    // Native, opt-in bishop idle clips (Settings → Idle Animations).
    IdleAnimator idle;
    bool idleAllowed() const;
    void updateIdle();
    int lastGifFrames = 0, gifProgressFrames = 0;
    bool gifProgressCaptured = false;
    // Native side-by-side view: perspective board left, flat board right.
    bool bothBoards = false, flatBeforeBoth = false;
    int presentedWidth = 0, presentedHeight = 0;
    // Space beyond the both-boards canvas at the chosen whole-pixel scale (full screen or
    // an enlarged window): extra width centres the flat board, extra height lengthens the
    // move list, so the screen is used instead of padded.
    int fillWidth = 0, fillHeight = 0;
    int flatViewOrigin() const;
    int panelTextHeight() const;
    SDL_RendererLogicalPresentation presentedMode = SDL_LOGICAL_PRESENTATION_DISABLED;
    // Information panel under both boards (ApplicationPanel.cpp).
    static constexpr int kPanelHeight = 112;
    struct PanelToken {
        std::string text;
        int ply; // Index of the move in the line, or -1 for a move number.
        int x, row, width;
    };
    std::vector<BCGame> panelLine;
    std::vector<std::string> panelSan;
    // The flat view is cropped to its board grid plus a margin, not the full 511-point port.
    static constexpr int kFlatViewMargin = 8;
    static constexpr int kFlatViewLeft = flat_hit_x[0] - kFlatViewMargin;
    static constexpr int kFlatViewWidth = flat_hit_x[8] + kFlatViewMargin - kFlatViewLeft;
    bool running = true, quitting = false;
    Uint64 quitDeadline = 0;
    int rendered = 0;
    BCSaveFile saved{};
    BCSaveSettings settings;
    BCThinkingTime thinkingTime{0, 6};
    TextDialog textDialog;
    fs::path currentFile;
    float mouseX = 0, mouseY = 0;
    bool fileWaiting = false, editing = false, previousFlat = false;
    SetupUI setup{};
    int alertId = 0, confirmAction = 0;
    ResourceDialogButton dialogButton;
    std::string message;
    Uint32 fileEvent;
    const int logicalWidth = original_content_width;
    // Menu 7 is the native Lichess menu, shown in the Modem slot when no serial device is open.
    static constexpr int kMenuCount = 8, kLichessMenu = 7;
    const int menuIds[8] = {401, 402, 403, 404, 400, 405, 406, 0};
    const int menuX[8] = {30, 80, 130, 220, 8, 30, 275, 275};
    const int menuWidth[8] = {45, 45, 80, 45, 18, 160, 60, 60};
    enum class ModemCheckStage : uint8_t {
        localMove,
        remoteMove,
        remotePromotion,
        whiteQuit,
        blackQuit,
        localQuit,
    };
    ModemCheckStage modemCheckStage = ModemCheckStage::localMove;
    Uint64 modemCheckDeadline = 0;

    // Startup and event dispatch (Application.cpp).
    void handleEvent(const SDL_Event &);
    void requestQuit();

    // Menus, dialogs and pointer input (ApplicationUI.cpp).
    const OriginalMenu &menuResource(int index);
    void showAlert(const std::string &value);
    void openTimeDialog();
    void acceptTextDialog();
    void handleTextKey(SDL_Keycode, SDL_Keymod);
    void saveGame(const fs::path &path);
    void exportPgnFile(fs::path path);
    void importPgnFile(const fs::path &path);
    void receiveFileResult(const FileResult &);
    void openFileDialog(bool write);
    void openPgnDialog();

    // Animated GIF export (ApplicationRecord.cpp).
    void openGifDialog();
    bool exportGifFile(fs::path path);
    bool menuItemEnabled(int menu, int item);
    void performMenuAction(int menu, int item);
    void dismissAlert(int item);
    int menuHeadingAt(int x, int y);
    // Left edge of an open menu's 250-point list, kept inside the visible width.
    int menuLeft(int menu) const {
        return std::min(menuX[menu], viewWidth() - 250);
    }
    SDL_Rect speedMenuBounds() const;
    void trackMenu(int x, int y);
    void handlePointerRelease(int x, int y);
    void handlePointerPress(int x, int y);
    int boardSquareAt(int x, int y) const;
    void setBothBoards(bool on);

    // Turn scheduling and remote play (ApplicationGame.cpp).
    void sendBoard();
    void resetGame();
    void sendMove(BCMove, unsigned movingSide);
    void syncAnimation();
    void sendEnding();
    void finishTurn();
    void updateComputer();
    void updateModem();

    // Board, menus and dialogs (ApplicationRender.cpp).
    void render();
    bool dualView() const;
    int viewWidth() const;
    void applyLayout(bool resizeWindow);
    const Position &flatViewPosition() const;
    void renderBoard();
    void renderBoardView(bool viewFlat, bool primary);
    void renderMenus();
    void renderDialogs();
    void captureScreenshot();

    // Lichess online play (ApplicationLichess.cpp).
    bool lichessAvailable() const;
    bool lichessMenuVisible() const;
    const char *lichessMenuLabel(int item) const;
    bool lichessItemEnabled(int item) const;
    void lichessMenuAction(int item);
    void lichessRequest(const std::string &tag, const std::string &method, const std::string &path,
                        const std::string &form = "");
    void lichessOpenStream(const std::string &name, const std::string &method,
                           const std::string &path, const std::string &form = "");
    void lichessCloseStream(const std::string &name);
    void lichessConnect(const std::string &token, bool fromPrompt);
    void lichessDisconnect();
    void lichessJoinGame(const std::string &id, const std::string &color,
                         const std::string &opponent);
    void lichessLeaveGame();
    // Lobby (ApplicationLichess.cpp).
    enum LobbyButton { kLobbyOpen, kLobbyDecline, kLobbyTime, kLobbyPlayAi, kLobbyChallenge,
                       kLobbySeek, kLobbyClose, kLobbyButtonCount };
    struct LobbyRow {
        bool challenge;
        size_t index;
    };
    std::vector<LobbyRow> lobbyRows() const;
    std::string lobbyRowText(const LobbyRow &row) const;
    SDL_Rect lobbyBox() const;
    SDL_Rect lobbyButtonRect(int button) const;
    const char *lobbyButtonLabel(int button) const;
    bool lobbyButtonEnabled(int button) const;
    void openLichessLobby();
    void renderLichessLobby();
    void lichessLobbyPress(int x, int y);
    void lichessLobbyRelease(int x, int y);
    void lichessLobbyKey(SDL_Keycode key);
    void lichessLobbyAction(int button);
    void lichessAnalyse();
    void lichessStartGame(const std::string &kind, const std::string &username = "");
    void lichessDismissAlert(int command, int item);
    void updateLichess();
    void handleLichessMessage(const LichessMessage &message);
    void handleLichessEvent(const Json &event);
    void handleLichessGameLine(const Json &event);
    void syncLichessGame(bool full);
    void finishLichessAnimation();
    bool lichessOwnTurn() const;
    long long lichessClock(bool white) const;
    std::string lichessColorPreference() const;
    void renderLichessClocks();
    void openLichessPrompt(int kind);
    void renderLichessPrompt();
    bool lichessPromptPress(int x, int y);
    void lichessPromptRelease(int x, int y);
    void acceptLichessPrompt();

    // Information panel and full screen (ApplicationPanel.cpp).
    int viewHeight() const;
    int panelHeight() const;
    // Pieces of each engine side (0 White, 1 Black) captured so far, most valuable first.
    std::array<std::vector<uint8_t>, 2> capturedPieces() const;
    void renderCaptured(int top, int width);
    void renderBelowBoard();
    void renderCoordinates(bool viewFlat);
    const std::vector<std::string> &panelMoves();
    std::vector<PanelToken> panelTokens();
    void renderPanel();
    void handlePanelPress(int x, int y);
    bool fullscreen() const;
    void toggleFullscreen();

    // Existing native integration probes (ApplicationChecks.cpp).
    void checkModem();
    void checkSmoke();
    void checkBothBoards();
    void checkMovePanel();
    void checkIdleAnimations();
    void checkBoardAids();
    void checkLichess();
    void checkPgnImportAndGifExport();
    void checkModemLocalMove();
    void checkModemRemoteMove();
    void checkModemPromotion();
    void checkModemWhiteQuit();
    void checkModemBlackAndLocalQuit();
    void checkBoardInteraction();
    void checkCustomCursors();
    void preparePromotionPosition();
    void checkPromotionSelection();
    void checkMenuTracking();
    void checkGameDialogs();
    void checkSavedGameDialogs(const fs::path &temporary);
    void checkNewGameConfirmation();
    void checkBoardSetup();
    void checkCheckmateReset();
    void checkStalemateReset();
    void queueFocusLossEvents();
    void checkFocusLossAndQueueAboutEvents();
    void checkAboutAndPromotionWalk();
    void checkThinkingTime();
    void checkComputerPlay();
    void checkLoadedPlayerModes();
    void checkForcedComputerMove();
    void checkMoveHintAndHistory();
    void checkComputerForceAndReset();
    void waitForComputerResult();
};

uint8_t playerForSide(const BCSaveSettings &, unsigned side);
int checkAnimation(AnimationHost &, SDL_Renderer *, const fs::path &screenshotPath);

#endif
