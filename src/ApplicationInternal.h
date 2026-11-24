#ifndef BC_APPLICATION_INTERNAL_H
#define BC_APPLICATION_INTERNAL_H

#include "AnimationHost.h"
#include "ComputerPlayer.h"
#include "FileDialog.h"
#include "GameSession.h"
#include "IdleAnimator.h"
#include "ModemHost.h"
#include "ResourceDialog.h"
#include "save_game.h"
#include "SDLHelpers.h"
#include "SetupUI.h"
#include "TextDialog.h"
#include "time_control.h"

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
                         kIdleAnimationsItem = 12;
    // Native, opt-in bishop idle clips (Settings → Idle Animations).
    IdleAnimator idle;
    bool idleAllowed() const;
    void updateIdle();
    int lastGifFrames = 0, gifProgressFrames = 0;
    bool gifProgressCaptured = false;
    // Native side-by-side view: perspective board left, flat board right.
    bool bothBoards = false, flatBeforeBoth = false;
    int presentedWidth = 0, presentedHeight = 0;
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
    const int menuIds[7] = {401, 402, 403, 404, 400, 405, 406};
    const int menuX[7] = {30, 80, 130, 220, 8, 30, 275};
    const int menuWidth[7] = {45, 45, 80, 45, 18, 160, 60};
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

    // Information panel and full screen (ApplicationPanel.cpp).
    int viewHeight() const;
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
