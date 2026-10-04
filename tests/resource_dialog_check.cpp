#include "ResourceDialog.h"
#include "mac_bitmap_font.h"
#include "QuickDrawControl.h"
#include <cassert>
#include <cstring>
#include <cstdio>
// Native validation: compare both corners against original System7 screenshot pixels,
// docs/evidence/original-new-game.png x216..223,y246..273 (ring and pushbutton combined).
static void check_control_pixels(SDL_Renderer *renderer, SDL_Surface *surface) {
    constexpr unsigned char original_left[28] = {
        0x07, 0x1f, 0x3f, 0x7c, 0x71, 0xf6, 0xe4, 0xe8, 0xe8, 0xe8, 0xe8, 0xe8, 0xe8, 0xe8,
        0xe8, 0xe8, 0xe8, 0xe8, 0xe8, 0xe8, 0xe8, 0xe4, 0xf6, 0x71, 0x7c, 0x3f, 0x1f, 0x07};
    assert(SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255));
    assert(SDL_RenderClear(renderer));
    assert(SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255));
    assert(drawQuickDrawRoundFrame(renderer, {100, 100, 68, 28}, 16, 3));
    assert(drawQuickDrawRoundFrame(renderer, {104, 104, 60, 20}, 10, 1));
    assert(SDL_RenderPresent(renderer));
    auto pixels = static_cast<const unsigned char *>(surface->pixels);
    for (int y = 0; y < 28; ++y)
        for (int x = 0; x < 8; ++x) {
            bool ink = ((original_left[y] >> (7 - x)) & 1) != 0;
            assert((pixels[(100 + y) * surface->pitch + (100 + x) * 4] == 0) == ink);
            assert((pixels[(100 + y) * surface->pitch + (167 - x) * 4] == 0) == ink);
        }
}
// Native validation (no original entrypoint): Run the resource dialog check regression assertions.
int main(int argc, char **argv) {
    // Native serial-text boundary: Mac Roman high bytes and DEL cannot abort rendering.
    auto decoded_serial = mac_bitmap_decode_roman(chicago_available ? chicago_glyphs : nullptr,
                                                  std::string("A\x80\x7f", 3));
    assert(decoded_serial == "A\xc3\x84?");
    if (chicago_available)
        assert(mac_bitmap_width(chicago_glyphs, decoded_serial) > 0);
    const auto *p = findResourceDialog(408, ResourceDialogKind::dialog);
    assert(p && p->ditl == 409 && p->count == 5);
    assert(p->left == 68 && p->top == 70 && p->right == 306 && p->bottom == 258);
    assert(hitResourceDialog(*p, 79, 125) == 2);
    assert(hitResourceDialog(*p, 129, 125) == 0);
    assert(hitResourceDialog(*p, 136, 125) == 3);
    assert(hitResourceDialog(*p, 184, 125) == 3);
    assert(hitResourceDialog(*p, 186, 125) == 4);
    assert(hitResourceDialog(*p, 240, 125) == 5);
    const auto *confirm = findResourceDialog(402, ResourceDialogKind::alert);
    assert(confirm && confirm->ditl == 29019 && confirm->stages == 0x5555);
    assert(findResourceDialog(406, ResourceDialogKind::alert)->stages == 0x5555);
    assert(findResourceDialog(407, ResourceDialogKind::alert)->stages == 0x5555);
    assert(hitResourceDialog(*confirm, 158, 174) == 1);
    assert(hitResourceDialog(*confirm, 311, 174) == 2);
    const auto *about = findResourceDialog(400, ResourceDialogKind::alert);
    assert(about && about->count == 3);
    // Native button lifetime: no second activation, stale dialog or lost-focus press.
    ResourceDialogButton button;
    button.press(*confirm, 158, 174);
    assert(button.release(confirm, 158, 174) == 1);
    assert(button.release(confirm, 158, 174) == 0);
    button.press(*confirm, 158, 174);
    assert(button.release(about, 158, 174) == 0);
    button.press(*confirm, 158, 174);
    button.cancel();
    assert(button.release(confirm, 158, 174) == 0);
    const auto &picture = original_dialog_items[about->first + 1];
    assert(picture.resource_id == 10001 && (picture.type & 127) == 64);
    assert(hitResourceDialog(*about, about->left + picture.left, about->top + picture.top) == 0);
    button.press(*about, about->left + picture.left, about->top + picture.top);
    assert(button.release(about, about->left + picture.left, about->top + picture.top) == 0);
    assert(!std::strcmp(original_message_new_game, "OK to start New Game?"));
    assert(!std::strcmp(original_message_clear_board, "OK to Clear Board?"));
    assert(!std::strcmp(original_message_checkmate, "Check and mate."));
    assert(findResourceDialog(401, ResourceDialogKind::alert)->count == 0);
    assert(SDL_Init(SDL_INIT_VIDEO));
    SDL_Surface *surface = SDL_CreateSurface(512, 370, SDL_PIXELFORMAT_RGBA32);
    assert(surface);
    SDL_Renderer *renderer = SDL_CreateSoftwareRenderer(surface);
    assert(renderer);
    check_control_pixels(renderer, surface);
    if (chicago_available) {
        assert(chicago_ascent == 12 && chicago_descent == 3 && chicago_leading == 1);
        int width = chicago_glyphs['W'].advance;
        assert(mac_bitmap_caret(chicago_glyphs, "Wi", -2) == 0);
        assert(mac_bitmap_caret(chicago_glyphs, "Wi", width) == 1);
        assert(mac_bitmap_caret(chicago_glyphs, "Wi", 100) == 2);
        assert(mac_bitmap_width(chicago_glyphs, "Wi") == width + chicago_glyphs['i'].advance);
    }
    const char *args[] = {original_message_new_game, "", nullptr, nullptr};
    assert(drawResourceDialog(renderer, *confirm, args));
    assert(SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255));
    assert(SDL_RenderClear(renderer));
    assert(drawResourceDialog(renderer, *about));
    for (const auto &run : original_about_text) {
        assert(run.font == 3 && run.size == 9 && !std::strcmp(run.font_name, "Geneva"));
        if (!geneva_available)
            continue;
        int x = run.x;
        std::string_view text = run.text;
        assert(mac_bitmap_width(geneva_glyphs, text) >= 0);
        while (!text.empty()) {
            int code = mac_bitmap_character(text);
            assert(code >= 0);
            const auto &g = geneva_glyphs[code];
            for (int row = 0; row < g.height; ++row)
                for (int col = 0; col < g.width; ++col)
                    if (geneva_pixels[g.offset + row * g.width + col]) {
                        assert(x + g.x + col >= run.left && x + g.x + col < run.right);
                        assert(run.y - g.y + row >= run.top && run.y - g.y + row < run.bottom);
                    }
            x += g.advance;
        }
    }
    assert(mac_bitmap_width(geneva_glyphs, std::string_view("\xc0\xaf", 2)) == -1);
    SDL_Rect clip{1, 2, 3, 4}, restored{};
    assert(SDL_SetRenderClipRect(renderer, &clip));
    assert(drawResourceDialog(renderer, *about));
    assert(SDL_GetRenderClipRect(renderer, &restored));
    assert(SDL_RectsEqual(&clip, &restored));
    assert(SDL_SetRenderClipRect(renderer, nullptr));
    assert(sizeof(original_about_bitmap) == 2400);
    assert(sizeof(original_about_text) / sizeof(original_about_text[0]) == 16);
    assert(SDL_RenderPresent(renderer));
    if (argc > 1)
        assert(SDL_SaveBMP(surface, argv[1]));
    SDL_DestroyRenderer(renderer);
    SDL_DestroySurface(surface);
    SDL_Quit();
    std::puts("original dialog geometry, bitmap text bounds and renderer: ok");
}
