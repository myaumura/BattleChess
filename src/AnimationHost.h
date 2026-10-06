#ifndef BC_ANIMATION_HOST_H
#define BC_ANIMATION_HOST_H

#include <SDL3/SDL.h>
#include "animation.h"
#include "Game.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <vector>

class AnimationHost {
  public:
    // The renderer and shared RNG state must outlive this host.
    AnimationHost(SDL_Renderer *rendererPtr, const std::filesystem::path &assetsPath,
                  const std::filesystem::path &pixelFile, uint32_t &sharedRandomState);
    ~AnimationHost();

    void begin(const Position &position, BCMove move, bool flat);
    void tick(uint32_t ticks);
    void draw(bool backdrop = true);
    bool displays(const Position &position, bool flat) const;
    void cancel();
    // Native platform adapter (no direct original address): playback still owns the move.
    bool busy() const {
        return running;
    }
    // Drive playback to completion with a bounded native verification loop.
    void finishForCheck();

    bool soundEnabled = true;
    bool fast = false;
    BCAnimation scene{};

  private:
    // SDL resources and extracted animation data.
    SDL_Renderer *renderer;
    SDL_AudioStream *audio = nullptr;
    std::filesystem::path assets;
    std::vector<uint8_t> pixels;
    std::vector<SDL_Texture *> textures;
    SDL_Texture *background[2]{};
    std::array<std::vector<BCAnimClip>, 2> combat;
    // Compound moves play one submove at a time.
    BCMove subMoves[2]{};
    size_t moveCount = 0, moveIndex = 0;
    bool running = false, ready = false;
    BCMove originalMove{};

    // Fade state shares the original RNG with computer search.
    int fadingSprite = -1, fadePhase = -1;
    uint16_t fadeMasks[16]{};
    uint32_t &randomState;

    void loadFramePixels(const std::filesystem::path &pixelFile);
    void loadBackgrounds();

    void plan(bool preserve = false);
    void bindRuntimeAssets(bool flat);
    void initializeSprites(const BCAnimation &previous, const uint8_t *board, bool flat,
                           bool preserve);
    void configureCombat(const uint8_t *board);
    void configureCombatRole(int role, int role0Type, int role1Type);
    void finishMove();

    SDL_Texture *getTexture(unsigned frame, unsigned color);
    void fillFrameSurface(SDL_Surface *surface, unsigned frame, unsigned color);
    void drawBackdrop();
    std::vector<unsigned> spriteDrawOrder() const;
    void drawSprite(unsigned spriteIndex);

    void playSound(unsigned soundIndex, int parameter);
    void startSoundStream(const std::vector<uint8_t> &raw, int parameter);
    void shuffleFadeMasks();
    void presentFadeFrame();

    // Callbacks bound to the recovered C animation runtime.
    static BCAnimClip clip(void *context, int group, int sequence);
    static const uint8_t *timing(void *context, int displayType, int stepIndex);
    static uint32_t standing(void *context, uint8_t displayByte, int flat);
    static void sound(void *context, unsigned soundIndex, unsigned color, int parameter);
    static void fade(void *context, unsigned sprite);
};

#endif
