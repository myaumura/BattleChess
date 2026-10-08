#include "AnimationHost.h"
#include "animation_data.hpp"
#include "animation_plan.h"
#include "original.hpp"
#include "presentation.h"
#include <algorithm>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <cstring>
namespace {
    // Native platform adapter (no direct original address): report an SDL operation failure to the
    // host.
    void checkSDL(bool ok, const char *what) {
        if (!ok)
            throw std::runtime_error(std::string(what) + ": " + SDL_GetError());
    }

    // Native platform adapter (no direct original address): load an extracted binary asset from
    // disk.
    std::vector<uint8_t> readAsset(const std::filesystem::path &path) {
        std::ifstream file(path, std::ios::binary);
        if (!file)
            throw std::runtime_error("Missing " + path.string());
        return {(std::istreambuf_iterator<char>(file)), {}};
    }

    // Native platform adapter (no direct original address): resolve a recovered animation group and
    // sequence in the generated catalog.
    BCAnimClip lookupClip(const char *name, int sequence) {
        for (auto &groupData : animation_groups)
            if (std::strcmp(name, groupData.name) == 0) {
                if (sequence < 0 || unsigned(sequence) >= groupData.count)
                    throw std::runtime_error("Invalid sequence in " + std::string(name));
                auto clipData = animation_clips[groupData.first + sequence];
                return {clipData.first, uint16_t(clipData.count)};
            }
        throw std::runtime_error("Missing animation group " + std::string(name));
    }
} // namespace

// Native platform adapter (no direct original address): load extracted animation pixels and board
// textures, and validate their bounds.
AnimationHost::AnimationHost(SDL_Renderer *rendererPtr, const std::filesystem::path &assetsPath,
                             const std::filesystem::path &pixelFile, uint32_t &sharedRandomState)
    : renderer(rendererPtr), assets(assetsPath), randomState(sharedRandomState) {
    loadFramePixels(pixelFile);
    loadBackgrounds();
    for (int i = 0; i < 16; ++i)
        fadeMasks[i] = uint16_t(animation_fade_masks[i]);
}

void AnimationHost::loadFramePixels(const std::filesystem::path &pixelFile) {
    pixels = readAsset(pixelFile);
    textures.resize(std::size(animation_frames) * 2, nullptr);
    for (auto &frameData : animation_frames)
        if (frameData.width < 0 || frameData.height < 0 ||
            uint64_t(frameData.offset) + uint64_t(frameData.width) * frameData.height >
                pixels.size())
            throw std::runtime_error("Invalid animation pixel span");
}

void AnimationHost::loadBackgrounds() {
    for (int viewIndex = 0; viewIndex < 2; ++viewIndex) {
        auto path = assets / "external_graphics/screens" /
                    (viewIndex ? "flat_board.png" : "perspective_board.png");
        SDL_Surface *surface = SDL_LoadPNG(path.string().c_str());
        checkSDL(surface, "Board image");
        background[viewIndex] = SDL_CreateTextureFromSurface(renderer, surface);
        SDL_DestroySurface(surface);
        checkSDL(background[viewIndex], "Board texture");
        SDL_SetTextureScaleMode(background[viewIndex], SDL_SCALEMODE_NEAREST);
    }
}

// Native platform adapter (no direct original address): release SDL textures and the playback
// stream owned by this host.
AnimationHost::~AnimationHost() {
    for (auto *t : textures)
        SDL_DestroyTexture(t);
    for (auto *t : background)
        SDL_DestroyTexture(t);
    SDL_DestroyAudioStream(audio);
}
// Native platform adapter (no direct original address): translate runtime clip requests into
// generated catalog entries.
BCAnimClip AnimationHost::clip(void *, int group, int sequence) {
    if (group == -1)
        return lookupClip("flat_pieces", sequence - 17);
    char name[24];
    std::snprintf(name, sizeof name, "move_%02d", group);
    return lookupClip(name, sequence);
}
// Native platform adapter (no direct original address): expose recovered movement timing bytes to
// the animation runtime.
const uint8_t *AnimationHost::timing(void *, int displayType, int stepIndex) {
    if (displayType < 1 || displayType > 6 || stepIndex < 0 || stepIndex >= 24)
        throw std::runtime_error("Invalid movement timing");
    return animation_script_bytes + move_timing[(displayType - 1) * 24 + stepIndex].offset;
}
// Native platform adapter (no direct original address): select the standing frame for a board byte
// and view mode.
uint32_t AnimationHost::standing(void *, uint8_t displayByte, int flat) {
    int shape = (flat ? flat_shapes : perspective_shapes)[displayByte & 63] - 17;
    return lookupClip(flat ? "flat_pieces" : "standing", shape).first;
}
// Native platform adapter (no direct original address): check whether the current scene can
// preserve its sprite state for a move.
bool AnimationHost::displays(const Position &position, bool flat) const {
    if (!ready || scene.flat != flat)
        return false;
    uint8_t board[64];
    setup_display_board(&position, board);
    for (int i = 0; i < 64; ++i)
        if ((board[i] & 0x47) != (scene.board[i] & 0x47))
            return false;
    return true;
}
// Native platform adapter (no direct original address): initialize a move and split compound moves
// for sequential playback.
void AnimationHost::begin(const Position &position, BCMove move, bool flat) {
    if (running)
        throw std::runtime_error("Animation already active");
    bool preserve = displays(position, flat);
    if (!preserve) {
        bc_animation_reset(&scene);
        scene.flat = flat;
        setup_display_board(&position, scene.board);
    }
    moveCount = bc_animation_split_move(move, subMoves);
    if (!moveCount)
        throw std::runtime_error("Invalid animation move");
    moveIndex = 0;
    running = true;
    ready = true;
    originalMove = move;
    plan(preserve);
    previousHostTicks = scene.previous_ticks;
    pendingTicks = 0;
}
// Native platform adapter (no direct original address): bind extracted assets and sprite state to
// the recovered animation graph.
void AnimationHost::plan(bool preserve) {
    BCAnimation previous = scene;
    uint8_t board[64];
    std::memcpy(board, scene.board, 64);
    bool flat = scene.flat;
    bc_animation_reset(&scene);
    scene.flat = flat;
    std::memcpy(scene.board, board, 64);
    bindRuntimeAssets(flat);
    initializeSprites(previous, board, flat, preserve);

    BCAnimationPlan graph{};
    if (!bc_animation_plan_build(&graph, board, subMoves[moveIndex]))
        throw std::runtime_error("Original animation graph could not be built");
    static_assert(sizeof(graph.nodes) == sizeof(scene.nodes));
    std::memcpy(scene.nodes, graph.nodes, sizeof(scene.nodes));
    configureCombat(board);

    scene.previous_ticks = fast ? 0 : uint32_t(SDL_GetTicks() * 60 / 1000);
    if (bc_animation_activate(&scene, graph.root) < 0)
        throw std::runtime_error("Animation activation failed");
}

void AnimationHost::bindRuntimeAssets(bool flat) {
    scene.assets.context = this;
    scene.assets.clip = clip;
    scene.assets.timing = timing;
    scene.assets.standing = standing;
    scene.assets.sound = sound;
    scene.assets.fade = fade;
    for (int i = 0; i < 8; ++i) {
        scene.assets.offset_x[i] = int8_t(animation_offset_x[i]);
        scene.assets.offset_y[i] = int8_t(animation_offset_y[i]);
        scene.anchors_y[i] = int16_t((flat ? flat_y : perspective_y)[i]);
    }
}

void AnimationHost::initializeSprites(const BCAnimation &previous, const uint8_t *board, bool flat,
                                      bool preserve) {
    if (preserve) {
        std::memcpy(scene.sprites, previous.sprites, sizeof(scene.sprites));
        std::memcpy(scene.sprite_at, previous.sprite_at, sizeof(scene.sprite_at));
        scene.order = previous.order;
    }
    unsigned spriteIndex = 0;
    for (int displaySquare = 0; displaySquare < 64; ++displaySquare) {
        scene.anchors_x[displaySquare] = int16_t((flat ? flat_x : perspective_x)[displaySquare]);
        if (preserve || !board[displaySquare])
            continue;
        scene.sprite_at[displaySquare] = spriteIndex;
        auto &sprite = scene.sprites[spriteIndex++];
        sprite.x = sprite.target_x = scene.anchors_x[displaySquare];
        sprite.y = sprite.target_y = scene.anchors_y[displaySquare >> 3];
        sprite.frame = standing(this, board[displaySquare], flat);
        sprite.color = (board[displaySquare] & 64) != 0;
        sprite.square = displaySquare;
        sprite.visible = 1;
        sprite.order = ++scene.order;
    }
}

void AnimationHost::configureCombat(const uint8_t *board) {
    int role0Type = 0, role1Type = 0;
    for (auto &node : scene.nodes)
        if (node.type == BC_COMBAT) {
            if (node.dest == 0)
                role0Type = board[node.source] & 7;
            else
                role1Type = board[node.source] & 7;
        }
    if (role0Type && role1Type)
        for (int role = 0; role < 2; ++role)
            configureCombatRole(role, role0Type, role1Type);
}

void AnimationHost::configureCombatRole(int role, int role0Type, int role1Type) {
    int pieceType = role ? role1Type : role0Type;
    int selectionIndex =
        role ? 36 + (role1Type - 1) * 6 + role0Type - 1 : (role0Type - 1) * 6 + role1Type - 1;
    combat[role].clear();
    auto selection = combat_selection[selectionIndex];
    for (unsigned clipIndex = 0; clipIndex + 1 < selection.count; ++clipIndex)
        combat[role].push_back(lookupClip(animation_combat_groups[role * 6 + pieceType - 1],
                                          animation_script_bytes[selection.offset + clipIndex]));
    combat[role].push_back({0, 0});
    scene.assets.combat[role] = combat[role].data();
    scene.assets.combat_timing[role] =
        animation_script_bytes + combat_timing[selectionIndex].offset;
}

// Native platform adapter (no direct original address): advance the runtime, continue compound
// moves, and update promotion artwork.
void AnimationHost::tick(uint32_t ticks) {
    if (!running)
        return;
    // Native speed control: retain HANDLEAL's original six-tick gate at 1x.
    uint64_t passes = 1;
    if (!fast && speed > 1) {
        pendingTicks += uint32_t(ticks - previousHostTicks) * speed;
        passes = uint64_t(pendingTicks / 6);
        pendingTicks -= passes * 6;
    }
    previousHostTicks = ticks;
    while (running && passes) {
        --passes;
        int result =
            bc_animation_step(&scene, fast || speed == 1 ? ticks : scene.previous_ticks + 6);
        if (result < 0)
            throw std::runtime_error("Original animation runtime rejected data");
        if (result == 1) {
            if (++moveIndex < moveCount)
                plan(true);
            else
                finishMove();
        }
    }
}

void AnimationHost::finishMove() {
    running = false;
    if (originalMove.special && originalMove.piece != 1 && originalMove.piece != 6) {
        int destination = engine_to_display(originalMove.to);
        auto &sprite = scene.sprites[scene.sprite_at[destination]];
        Position promoted{};
        insert_piece(&promoted, originalMove.piece, sprite.color ? 0 : 1, originalMove.to);
        uint8_t board[64];
        setup_display_board(&promoted, board);
        scene.board[destination] = board[destination];
        sprite.frame = standing(this, board[destination], scene.flat);
        sprite.order = ++scene.order;
    }
}

// Native platform adapter (no direct original address): invalidate playback state and stop queued
// host audio.
void AnimationHost::cancel() {
    running = false;
    ready = false;
    moveCount = 0;
    SDL_DestroyAudioStream(audio);
    audio = nullptr;
}
// Native platform adapter (no direct original address): drive playback without delays for a bounded
// verification run.
void AnimationHost::finishForCheck() {
    bool saved = fast;
    fast = true;
    for (unsigned i = 0; running && i < 20000; ++i)
        tick(scene.previous_ticks + 6);
    fast = saved;
    if (running)
        throw std::runtime_error("Animation did not terminate");
}
// Native platform adapter (no direct original address): convert indexed frames to SDL textures,
// applying the recovered fade masks before recoloring.
SDL_Texture *AnimationHost::getTexture(unsigned frame, unsigned color) {
    if (frame >= std::size(animation_frames))
        throw std::runtime_error("Animation frame outside catalog");
    unsigned textureKey = frame * 2 + (color != 0);
    if (textures[textureKey] && fadingSprite < 0)
        return textures[textureKey];
    auto frameData = animation_frames[frame];
    SDL_Surface *surface =
        SDL_CreateSurface(frameData.width, frameData.height, SDL_PIXELFORMAT_RGBA32);
    checkSDL(surface, "Animation surface");
    fillFrameSurface(surface, frame, color);
    SDL_Texture *texture = SDL_CreateTextureFromSurface(renderer, surface);
    SDL_DestroySurface(surface);
    checkSDL(texture, "Animation texture");
    SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_NEAREST);
    if (fadingSprite < 0)
        textures[textureKey] = texture;
    return texture;
}

void AnimationHost::fillFrameSurface(SDL_Surface *surface, unsigned frame, unsigned color) {
    auto frameData = animation_frames[frame];
    // Original fade ORs 16-bit words in the padded 2-bit shape, before side recoloring.
    int stride = ((frameData.width + 7) >> 2) & ~1;
    for (int y = 0; y < frameData.height; ++y)
        for (int x = 0; x < frameData.width; ++x) {
            uint8_t pixel = pixels[frameData.offset + y * frameData.width + x];
            if (fadingSprite >= 0) {
                int wordIndex = (y * stride + x / 4) / 2;
                int bitShift = 14 - 2 * (x % 8);
                for (int phaseIndex = 0; phaseIndex <= fadePhase; ++phaseIndex)
                    pixel |= (uint16_t(~fadeMasks[(phaseIndex + wordIndex) & 15]) >> bitShift) & 3;
            }
            pixel = original_pixel(pixel, color ? 0 : 1);
            auto *rgba = static_cast<uint8_t *>(surface->pixels) + y * surface->pitch + 4 * x;
            rgba[0] = rgba[1] = rgba[2] = pixel == 1 ? 0 : 255;
            rgba[3] = pixel == 3 ? 0 : 255;
        }
}

// Native platform adapter (no direct original address): paint the board and visible sprites in
// their depth order.
void AnimationHost::draw(bool backdrop) {
    SDL_Rect viewport{0, 20, 512, 330};
    checkSDL(SDL_SetRenderClipRect(renderer, &viewport), "Animation clipping");
    if (backdrop)
        drawBackdrop();
    for (unsigned spriteIndex : spriteDrawOrder())
        drawSprite(spriteIndex);
    checkSDL(SDL_SetRenderClipRect(renderer, nullptr), "Restore clipping");
}

void AnimationHost::drawBackdrop() {
    SDL_FRect src{0, 0, 512, 330}, dst{0, 20, 512, 330};
    checkSDL(SDL_RenderTexture(renderer, background[scene.flat], &src, &dst), "Animation backdrop");
}

std::vector<unsigned> AnimationHost::spriteDrawOrder() const {
    std::vector<unsigned> order;
    for (unsigned i = 0; i < 32; ++i)
        if (scene.sprites[i].visible)
            order.push_back(i);
    // Native platform adapter (no direct original address): compare sprite depth and preserve the
    // recovered overlap priority.
    std::stable_sort(order.begin(), order.end(), [&](unsigned leftIndex, unsigned rightIndex) {
        auto &leftSprite = scene.sprites[leftIndex];
        auto &rightSprite = scene.sprites[rightIndex];
        return leftSprite.y < rightSprite.y ||
               (leftSprite.y == rightSprite.y && leftSprite.order > rightSprite.order);
    });
    return order;
}

void AnimationHost::drawSprite(unsigned spriteIndex) {
    auto &sprite = scene.sprites[spriteIndex];
    auto frameData = animation_frames[sprite.frame];
    int savedFadingSprite = fadingSprite;
    if (int(spriteIndex) != fadingSprite)
        fadingSprite = -1;
    auto *texture = getTexture(sprite.frame, sprite.color);
    bool temporaryTexture = fadingSprite >= 0;
    fadingSprite = savedFadingSprite;
    SDL_FRect destination{float(sprite.x - frameData.hx), float(20 + sprite.y - frameData.hy),
                          float(frameData.width), float(frameData.height)};
    checkSDL(SDL_RenderTexture(renderer, texture, nullptr, &destination), "Animation sprite");
    if (temporaryTexture)
        SDL_DestroyTexture(texture);
}

// Native platform adapter (no direct original address): play decoded sound through SDL. Related
// MACPLAYS starts at file offset 0x115fc; its busy-driver check is at 0x11608
// (tools/3rd-party/dump.log).
void AnimationHost::sound(void *context, unsigned soundIndex, unsigned, int parameter) {
    auto &self = *static_cast<AnimationHost *>(context);
    if (!self.soundEnabled || self.fast)
        return;
    // MACPLAYS 0x11608: reject while sound driver is busy; color is unused on Mac.
    if (self.audio && SDL_GetAudioStreamQueued(self.audio) > 0)
        return;
    self.playSound(soundIndex, parameter);
}

void AnimationHost::playSound(unsigned soundIndex, int parameter) {
    SDL_DestroyAudioStream(audio);
    audio = nullptr;
    char name[40];
    std::snprintf(name, sizeof name, "sfx_%02u.decoded.bin", soundIndex);
    auto raw = readAsset(assets / "audio" / name);
    startSoundStream(raw, parameter);
}

void AnimationHost::startSoundStream(const std::vector<uint8_t> &raw, int parameter) {
    if (raw.size() < 10)
        throw std::runtime_error("Truncated original sound");
    int period = std::max(20, int(raw[0] * 256 + raw[1]) - int(int8_t(parameter)) * 8);
    unsigned end = 4 + 2 * (raw[2] * 256 + raw[3]);
    if (end > raw.size() || end < 10)
        throw std::runtime_error("Invalid original sound length");
    SDL_AudioSpec spec{SDL_AUDIO_U8, 1, int((1000000.0 / 44.93) * 180 / period + 0.5)};
    audio = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
    checkSDL(audio, "Sound device");
    checkSDL(SDL_PutAudioStreamData(audio, raw.data() + 10, int(end - 10)), "Sound samples");
    checkSDL(SDL_FlushAudioStream(audio), "Flush sound");
    checkSDL(SDL_ResumeAudioStreamDevice(audio), "Play sound");
}

// Native platform adapter (no direct original address): render the sixteen-step disappearance
// through SDL. Related FADESHAP starts at file offset 0xbc7a (tools/3rd-party/dump.log); host
// timing and presentation are native.
void AnimationHost::fade(void *context, unsigned sprite) {
    auto &self = *static_cast<AnimationHost *>(context);
    if (sprite >= 32)
        throw std::runtime_error("Invalid fade sprite");
    self.shuffleFadeMasks();
    self.fadingSprite = int(sprite);
    for (int fadePhase = 0; fadePhase < 16; ++fadePhase) {
        self.fadePhase = fadePhase;
        self.scene.sprites[sprite].order = ++self.scene.order;
        if (!self.fast)
            self.presentFadeFrame();
    }
    self.scene.sprites[sprite].visible = 0;
    self.fadingSprite = -1;
    self.fadePhase = -1;
    // Fade playback already consumed its wall time; do not advance the graph again for it.
    if (!self.fast && self.speed > 1)
        self.previousHostTicks = uint32_t(SDL_GetTicks() * 60 / 1000);
}

void AnimationHost::shuffleFadeMasks() {
    for (unsigned maskIndex = 0; maskIndex < 15; ++maskIndex) {
        randomState = randomState * 0x41c64e6d + 0x3039;
        unsigned swapIndex = maskIndex + 1 + ((randomState >> 16) & 0x7fff) % (15 - maskIndex);
        std::swap(fadeMasks[maskIndex], fadeMasks[swapIndex]);
    }
}

void AnimationHost::presentFadeFrame() {
    auto begin = SDL_GetTicks();
    draw();
    checkSDL(SDL_RenderPresent(renderer), "Fade present");
    auto elapsed = SDL_GetTicks() - begin;
    unsigned delay = unsigned(167 / speed);
    if (elapsed < delay)
        SDL_Delay(delay - Uint32(elapsed));
}
