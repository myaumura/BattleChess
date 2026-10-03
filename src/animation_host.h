#ifndef BC_ANIMATION_HOST_H
#define BC_ANIMATION_HOST_H

#include <SDL3/SDL.h>
#include "animation.h"
#include "animation_plan.h"
#include <filesystem>
#include <memory>
#include <vector>
#include <array>

class AnimationHost {
  public:
    AnimationHost(SDL_Renderer *, const std::filesystem::path &assets,
                  const std::filesystem::path &pixel_file, uint32_t &random_state);
    ~AnimationHost();
    void begin(const Position &, BCMove, bool flat);
    void tick(uint32_t ticks);
    void draw(bool backdrop = true);
    bool displays(const Position &, bool flat) const;
    void cancel();
    // Native platform adapter (no direct original address): expose whether playback still owns the
    // move.
    bool busy() const {
        return running;
    }
    void finish_for_check();
    bool sound_enabled = true, fast = false;
    BCAnimation scene{};

  private:
    SDL_Renderer *renderer;
    SDL_AudioStream *audio = nullptr;
    std::filesystem::path assets;
    std::vector<uint8_t> pixels;
    std::vector<SDL_Texture *> textures;
    SDL_Texture *background[2]{};
    std::array<std::vector<BCAnimClip>, 2> combat;
    BCMove moves[2]{};
    size_t count = 0, index = 0;
    bool running = false, ready = false;
    BCMove original_move{};
    int fading = -1, phase = -1;
    uint16_t fade_masks[16]{};
    uint32_t &random_state;
    void plan(bool preserve = false);
    SDL_Texture *get_texture(unsigned frame, unsigned color);
    static BCAnimClip clip(void *, int, int);
    static const uint8_t *timing(void *, int, int);
    static uint32_t standing(void *, uint8_t, int);
    static void sound(void *, unsigned, unsigned, int);
    static void fade(void *, unsigned);
};
#endif
