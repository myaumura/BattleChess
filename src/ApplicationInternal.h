#ifndef BC_APPLICATION_INTERNAL_H
#define BC_APPLICATION_INTERNAL_H

#include "animation_host.h"
#include "computer_player.h"
#include "FileDialog.h"
#include "GameSession.h"
#include "modem_host.h"
#include "resource_dialog.h"
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

    bool flat;
    bool autoAnswer = false, waitingForRemoteEnd = false;
    std::vector<BCMove> remotePromotion;
    uint8_t remotePromotionPiece = 0;
    std::optional<BCMove> hint;
    Uint64 hintStarted = 0;
    unsigned computerEnding = 0;
    int menu = -1, menuItem = -1;
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
    void receiveFileResult(const FileResult &);
    void openFileDialog(bool write);
    bool menuItemEnabled(int menu, int item);
    void performMenuAction(int menu, int item);
    void dismissAlert(int item);
    int menuHeadingAt(int x, int y);
    void trackMenu(int x, int y);
    void handlePointerRelease(int x, int y);
    void handlePointerPress(int x, int y);

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
    void renderBoard();
    void renderMenus();
    void renderDialogs();
    void captureScreenshot();

    // Existing native integration probes (ApplicationChecks.cpp).
    void checkModem();
    void checkSmoke();
    void checkModemLocalMove();
    void checkModemRemoteMove();
    void checkModemPromotion();
    void checkModemWhiteQuit();
    void checkModemBlackAndLocalQuit();
    void checkBoardInteraction();
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
