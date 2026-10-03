#include "animation_host.h"
#include "animation_data.hpp"
#include "original.hpp"
#include "presentation.h"
#include <algorithm>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <cstring>
// Native platform adapter (no direct original address): report an SDL operation failure to the
// host.
static void check_sdl(bool ok, const char *what) {
    if (!ok)
        throw std::runtime_error(std::string(what) + ": " + SDL_GetError());
}

// Native platform adapter (no direct original address): load an extracted binary asset from disk.
static std::vector<uint8_t> read_asset(const std::filesystem::path &p) {
    std::ifstream f(p, std::ios::binary);
    if (!f)
        throw std::runtime_error("Missing " + p.string());
    return {(std::istreambuf_iterator<char>(f)), {}};
}

// Native platform adapter (no direct original address): resolve a recovered animation group and
// sequence in the generated catalog.
static BCAnimClip lookup_clip(const char *name, int sequence) {
    for (auto &g : animation_groups)
        if (std::strcmp(name, g.name) == 0) {
            if (sequence < 0 || unsigned(sequence) >= g.count)
                throw std::runtime_error("Invalid sequence in " + std::string(name));
            auto c = animation_clips[g.first + sequence];
            return {c.first, uint16_t(c.count)};
        }
    throw std::runtime_error("Missing animation group " + std::string(name));
}

// Native platform adapter (no direct original address): load extracted animation pixels and board
// textures, and validate their bounds.
AnimationHost::AnimationHost(SDL_Renderer *r, const std::filesystem::path &p,
                             const std::filesystem::path &pixel_file, uint32_t &state)
    : renderer(r), assets(p), random_state(state) {
    pixels = read_asset(pixel_file);
    textures.resize(std::size(animation_frames) * 2, nullptr);
    for (auto &f : animation_frames)
        if (f.width < 0 || f.height < 0 ||
            uint64_t(f.offset) + uint64_t(f.width) * f.height > pixels.size())
            throw std::runtime_error("Invalid animation pixel span");
    for (int i = 0; i < 2; ++i) {
        auto path =
            assets / "external_graphics/screens" / (i ? "flat_board.png" : "perspective_board.png");
        SDL_Surface *s = SDL_LoadPNG(path.string().c_str());
        check_sdl(s, "Board image");
        background[i] = SDL_CreateTextureFromSurface(r, s);
        SDL_DestroySurface(s);
        check_sdl(background[i], "Board texture");
        SDL_SetTextureScaleMode(background[i], SDL_SCALEMODE_NEAREST);
    }
    for (int i = 0; i < 16; ++i)
        fade_masks[i] = uint16_t(animation_fade_masks[i]);
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
        return lookup_clip("flat_pieces", sequence - 17);
    char name[24];
    std::snprintf(name, sizeof name, "move_%02d", group);
    return lookup_clip(name, sequence);
}
// Native platform adapter (no direct original address): expose recovered movement timing bytes to
// the animation runtime.
const uint8_t *AnimationHost::timing(void *, int type, int i) {
    if (type < 1 || type > 6 || i < 0 || i >= 24)
        throw std::runtime_error("Invalid movement timing");
    return animation_script_bytes + move_timing[(type - 1) * 24 + i].offset;
}
// Native platform adapter (no direct original address): select the standing frame for a board byte
// and view mode.
uint32_t AnimationHost::standing(void *, uint8_t byte, int flat) {
    int shape = (flat ? flat_shapes : perspective_shapes)[byte & 63] - 17;
    return lookup_clip(flat ? "flat_pieces" : "standing", shape).first;
}
// Native platform adapter (no direct original address): check whether the current scene can
// preserve its sprite state for a move.
bool AnimationHost::displays(const Position &p, bool flat) const {
    if (!ready || scene.flat != flat)
        return false;
    uint8_t board[64];
    setup_display_board(&p, board);
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
    count = bc_animation_split_move(move, moves);
    if (!count)
        throw std::runtime_error("Invalid animation move");
    index = 0;
    running = true;
    ready = true;
    original_move = move;
    plan(preserve);
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
    if (preserve) {
        std::memcpy(scene.sprites, previous.sprites, sizeof(scene.sprites));
        std::memcpy(scene.sprite_at, previous.sprite_at, sizeof(scene.sprite_at));
        scene.order = previous.order;
    }
    unsigned sprite = 0;
    for (int i = 0; i < 64; ++i) {
        scene.anchors_x[i] = int16_t((flat ? flat_x : perspective_x)[i]);
        if (preserve || !board[i])
            continue;
        scene.sprite_at[i] = sprite;
        auto &s = scene.sprites[sprite++];
        s.x = s.target_x = scene.anchors_x[i];
        s.y = s.target_y = scene.anchors_y[i >> 3];
        s.frame = standing(this, board[i], flat);
        s.color = (board[i] & 64) != 0;
        s.square = i;
        s.visible = 1;
        s.order = ++scene.order;
    }
    BCAnimationPlan graph{};
    if (!bc_animation_plan_build(&graph, board, moves[index]))
        throw std::runtime_error("Original animation graph could not be built");
    static_assert(sizeof(graph.nodes) == sizeof(scene.nodes));
    std::memcpy(scene.nodes, graph.nodes, sizeof(scene.nodes));
    int role0_type = 0, role1_type = 0;
    for (auto &n : scene.nodes)
        if (n.type == BC_COMBAT) {
            if (n.dest == 0)
                role0_type = board[n.source] & 7;
            else
                role1_type = board[n.source] & 7;
        }
    if (role0_type && role1_type)
        for (int role = 0; role < 2; ++role) {
            int type = role ? role1_type : role0_type;
            int entry = role ? 36 + (role1_type - 1) * 6 + role0_type - 1
                             : (role0_type - 1) * 6 + role1_type - 1;
            combat[role].clear();
            auto stream = combat_selection[entry];
            for (unsigned i = 0; i + 1 < stream.count; ++i)
                combat[role].push_back(lookup_clip(animation_combat_groups[role * 6 + type - 1],
                                                   animation_script_bytes[stream.offset + i]));
            combat[role].push_back({0, 0});
            scene.assets.combat[role] = combat[role].data();
            scene.assets.combat_timing[role] = animation_script_bytes + combat_timing[entry].offset;
        }
    scene.previous_ticks = fast ? 0 : uint32_t(SDL_GetTicks() * 60 / 1000);
    if (bc_animation_activate(&scene, graph.root) < 0)
        throw std::runtime_error("Animation activation failed");
}
// Native platform adapter (no direct original address): advance the runtime, continue compound
// moves, and update promotion artwork.
void AnimationHost::tick(uint32_t ticks) {
    if (!running)
        return;
    int result = bc_animation_step(&scene, ticks);
    if (result < 0)
        throw std::runtime_error("Original animation runtime rejected data");
    if (result == 1) {
        if (++index < count)
            plan(true);
        else {
            running = false;
            if (original_move.special && original_move.piece != 1 && original_move.piece != 6) {
                int d = engine_to_display(original_move.to);
                auto &s = scene.sprites[scene.sprite_at[d]];
                Position promoted{};
                insert_piece(&promoted, original_move.piece, s.color ? 0 : 1, original_move.to);
                uint8_t board[64];
                setup_display_board(&promoted, board);
                scene.board[d] = board[d];
                s.frame = standing(this, board[d], scene.flat);
                s.order = ++scene.order;
            }
        }
    }
}
// Native platform adapter (no direct original address): invalidate playback state and stop queued
// host audio.
void AnimationHost::cancel() {
    running = false;
    ready = false;
    count = 0;
    SDL_DestroyAudioStream(audio);
    audio = nullptr;
}
// Native platform adapter (no direct original address): drive playback without delays for a bounded
// verification run.
void AnimationHost::finish_for_check() {
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
SDL_Texture *AnimationHost::get_texture(unsigned frame, unsigned color) {
    if (frame >= std::size(animation_frames))
        throw std::runtime_error("Animation frame outside catalog");
    unsigned key = frame * 2 + (color != 0);
    if (textures[key] && fading < 0)
        return textures[key];
    auto f = animation_frames[frame];
    SDL_Surface *s = SDL_CreateSurface(f.width, f.height, SDL_PIXELFORMAT_RGBA32);
    check_sdl(s, "Animation surface");
    // Original fade ORs 16-bit words in the padded 2-bit shape, before side recoloring.
    int stride = ((f.width + 7) >> 2) & ~1;
    for (int y = 0; y < f.height; ++y)
        for (int x = 0; x < f.width; ++x) {
            uint8_t v = pixels[f.offset + y * f.width + x];
            if (fading >= 0) {
                int word = (y * stride + x / 4) / 2;
                int shift = 14 - 2 * (x % 8);
                for (int k = 0; k <= phase; ++k)
                    v |= (uint16_t(~fade_masks[(k + word) & 15]) >> shift) & 3;
            }
            v = original_pixel(v, color ? 0 : 1);
            auto *d = static_cast<uint8_t *>(s->pixels) + y * s->pitch + 4 * x;
            d[0] = d[1] = d[2] = v == 1 ? 0 : 255;
            d[3] = v == 3 ? 0 : 255;
        }
    SDL_Texture *t = SDL_CreateTextureFromSurface(renderer, s);
    SDL_DestroySurface(s);
    check_sdl(t, "Animation texture");
    SDL_SetTextureScaleMode(t, SDL_SCALEMODE_NEAREST);
    if (fading < 0)
        textures[key] = t;
    return t;
}
// Native platform adapter (no direct original address): paint the board and visible sprites in
// their depth order.
void AnimationHost::draw(bool backdrop) {
    SDL_Rect viewport{0, 20, 512, 330};
    check_sdl(SDL_SetRenderClipRect(renderer, &viewport), "Animation clipping");
    SDL_FRect src{0, 0, 512, 330}, dst{0, 20, 512, 330};
    if (backdrop)
        check_sdl(SDL_RenderTexture(renderer, background[scene.flat], &src, &dst),
                  "Animation backdrop");
    std::vector<unsigned> order;
    for (unsigned i = 0; i < 32; ++i)
        if (scene.sprites[i].visible)
            order.push_back(i);
    // Native platform adapter (no direct original address): compare sprite depth and preserve the
    // recovered overlap priority.
    std::stable_sort(order.begin(), order.end(), [&](unsigned a, unsigned b) {
        auto &x = scene.sprites[a];
        auto &y = scene.sprites[b];
        return x.y < y.y || (x.y == y.y && x.order > y.order);
    });
    for (unsigned i : order) {
        auto &s = scene.sprites[i];
        auto f = animation_frames[s.frame];
        int saved = fading;
        if (int(i) != fading)
            fading = -1;
        auto *t = get_texture(s.frame, s.color);
        bool temporary = fading >= 0;
        fading = saved;
        SDL_FRect dst{float(s.x - f.hx), float(20 + s.y - f.hy), float(f.width), float(f.height)};
        check_sdl(SDL_RenderTexture(renderer, t, nullptr, &dst), "Animation sprite");
        if (temporary)
            SDL_DestroyTexture(t);
    }
    check_sdl(SDL_SetRenderClipRect(renderer, nullptr), "Restore clipping");
}
// Native platform adapter (no direct original address): play decoded sound through SDL. Related
// MACPLAYS starts at file offset 0x115fc; its busy-driver check is at 0x11608
// (tools/3rd-party/dump.log).
void AnimationHost::sound(void *context, unsigned index, unsigned, int parameter) {
    auto &self = *static_cast<AnimationHost *>(context);
    if (!self.sound_enabled || self.fast)
        return;
    // MACPLAYS 0x11608: reject while sound driver is busy; color is unused on Mac.
    if (self.audio && SDL_GetAudioStreamQueued(self.audio) > 0)
        return;
    SDL_DestroyAudioStream(self.audio);
    self.audio = nullptr;
    char name[40];
    std::snprintf(name, sizeof name, "sfx_%02u.decoded.bin", index);
    auto raw = read_asset(self.assets / "audio" / name);
    if (raw.size() < 10)
        throw std::runtime_error("Truncated original sound");
    int period = std::max(20, int(raw[0] * 256 + raw[1]) - int(int8_t(parameter)) * 8);
    unsigned end = 4 + 2 * (raw[2] * 256 + raw[3]);
    if (end > raw.size() || end < 10)
        throw std::runtime_error("Invalid original sound length");
    SDL_AudioSpec spec{SDL_AUDIO_U8, 1, int((1000000.0 / 44.93) * 180 / period + 0.5)};
    self.audio =
        SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
    check_sdl(self.audio, "Sound device");
    check_sdl(SDL_PutAudioStreamData(self.audio, raw.data() + 10, int(end - 10)), "Sound samples");
    check_sdl(SDL_FlushAudioStream(self.audio), "Flush sound");
    check_sdl(SDL_ResumeAudioStreamDevice(self.audio), "Play sound");
}
// Native platform adapter (no direct original address): render the sixteen-step disappearance
// through SDL. Related FADESHAP starts at file offset 0xbc7a (tools/3rd-party/dump.log); host
// timing and presentation are native.
void AnimationHost::fade(void *context, unsigned sprite) {
    auto &self = *static_cast<AnimationHost *>(context);
    if (sprite >= 32)
        throw std::runtime_error("Invalid fade sprite");
    for (unsigned i = 0; i < 15; ++i) {
        self.random_state = self.random_state * 0x41c64e6d + 0x3039;
        unsigned j = i + 1 + ((self.random_state >> 16) & 0x7fff) % (15 - i);
        std::swap(self.fade_masks[i], self.fade_masks[j]);
    }
    self.fading = int(sprite);
    for (int phase = 0; phase < 16; ++phase) {
        self.phase = phase;
        self.scene.sprites[sprite].order = ++self.scene.order;
        if (!self.fast) {
            auto begin = SDL_GetTicks();
            self.draw();
            check_sdl(SDL_RenderPresent(self.renderer), "Fade present");
            auto elapsed = SDL_GetTicks() - begin;
            if (elapsed < 167)
                SDL_Delay(167 - Uint32(elapsed));
        }
    }
    self.scene.sprites[sprite].visible = 0;
    self.fading = -1;
    self.phase = -1;
}
