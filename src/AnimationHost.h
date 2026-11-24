#ifndef BC_ANIMATION_HOST_H
#define BC_ANIMATION_HOST_H

#include <SDL3/SDL.h>
#include "animation.h"
#include "Game.h"
#include <array>
#include <algorithm>
#include <array>
#include <cstddef>
#include <iterator>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <vector>

class AnimationHost
{
public:
    // The renderer and shared RNG state must outlive this host.
    AnimationHost(SDL_Renderer* rendererPtr, const std::filesystem::path& assetsPath,
                  const std::filesystem::path& pixelFile, uint32_t& sharedRandomState);
    ~AnimationHost();

    void begin(const Position& position, BCMove move, bool flat);
    void tick(uint32_t ticks);
    void draw(bool backdrop = true);
    bool displays(const Position& position, bool flat) const;
    void cancel();
    // Native platform adapter (no direct original address): playback still owns the move.
    bool busy() const
    {
        return running;
    }

    // Drive playback to completion with a bounded native verification loop.
    void finishForCheck();
    // Native idle extension: draw one catalog frame, recolored for its side, at a board
    // anchor in content coordinates (the same anchor convention as sprites).
    void drawFrame(unsigned frame, bool white, int x, int y);

    bool soundEnabled = true;
    bool fast = false;
    // Native session preference: 1x through 2x in quarter steps; outside the original save format.
    double speed = 1;
    // Native online floor (Lichess Fast animations): plays a move at least this fast
    // without changing the session preference above.
    double minimumSpeed = 1;

    double playbackSpeed() const
    {
        return speed > minimumSpeed ? speed : minimumSpeed;
    }

    // Native rotated 2D board: flat scenes are drawn turned by 180 degrees.
    bool mirrorFlat = false;
    /* Map a flat-board coordinate to the rotated board, piecewise between the original
     * anchors so every square's anchor lands exactly on its opposite square's anchor. */
    static int mirrorFlatAxis(int value, bool vertical);
    BCAnimation scene{};
    // Native recording hook: when set, each of the sixteen fade phases calls it instead of
    // presenting to the window, so offscreen exports capture the disappearance.
    std::function<void()> fadeFrame;
    // Fades consume this stream; exporters save and restore it to keep search results unchanged.
    uint32_t& sharedRandomState()
    {
        return randomState;
    }

    // Each fade also permutes the dither masks in place; exporters restore their order too.
    std::array<uint16_t, 16> fadeMaskOrder() const
    {
        std::array<uint16_t, 16> order{};
        std::copy(std::begin(fadeMasks), std::end(fadeMasks), order.begin());
        return order;
    }

    void restoreFadeMaskOrder(const std::array<uint16_t, 16>& order)
    {
        std::copy(order.begin(), order.end(), std::begin(fadeMasks));
    }

private:
    // SDL resources and extracted animation data.
    SDL_Renderer* renderer;
    SDL_AudioStream* audio = nullptr;
    std::filesystem::path assets;
    std::vector<uint8_t> pixels;
    std::vector<SDL_Texture*> textures;
    SDL_Texture* background[2]{};
    std::array<std::vector<BCAnimClip>, 2> combat;
    // Compound moves play one submove at a time.
    BCMove subMoves[2]{};
    size_t moveCount = 0, moveIndex = 0;
    bool running = false, ready = false;
    BCMove originalMove{};
    uint32_t previousHostTicks = 0;
    double pendingTicks = 0;

    // Fade state shares the original RNG with computer search.
    int fadingSprite = -1, fadePhase = -1;
    uint16_t fadeMasks[16]{};
    uint32_t& randomState;

    void loadFramePixels(const std::filesystem::path& pixelFile);
    void loadBackgrounds();

    void plan(bool preserve = false);
    void bindRuntimeAssets(bool flat);
    void initializeSprites(const BCAnimation& previous, const uint8_t* board, bool flat,
                           bool preserve);
    void configureCombat(const uint8_t* board);
    void configureCombatRole(int role, int role0Type, int role1Type);
    void finishMove();

    SDL_Texture* getTexture(unsigned frame, unsigned color);
    void fillFrameSurface(SDL_Surface* surface, unsigned frame, unsigned color);
    void drawBackdrop();
    std::vector<unsigned> spriteDrawOrder() const;
    void drawSprite(unsigned spriteIndex);

    void playSound(unsigned soundIndex, int parameter);
    void startSoundStream(const std::vector<uint8_t>& raw, int parameter);
    void shuffleFadeMasks();
    void presentFadeFrame();

    // Callbacks bound to the recovered C animation runtime.
    static BCAnimClip clip(void* context, int group, int sequence);
    static const uint8_t* timing(void* context, int displayType, int stepIndex);
    static uint32_t standing(void* context, uint8_t displayByte, int flat);
    static void sound(void* context, unsigned soundIndex, unsigned color, int parameter);
    static void fade(void* context, unsigned sprite);
};

#endif
