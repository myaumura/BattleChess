#include "resource_dialog.h"
#include "mac_bitmap_font.h"
#include "quickdraw_control.h"
#include <cstring>
#include <string>
#include <algorithm>
// Native platform adapter (no direct original address): find dialog geometry recovered from
// DLOG/ALRT and DITL resources.
const OriginalDialog *resource_dialog_find(int id, bool alert) {
    for (const auto &dialog : original_dialogs)
        if (dialog.id == id && dialog.alert == int(alert))
            return &dialog;
    return nullptr;
}
// Native platform adapter (no direct original address): return the one-based enabled DITL item
// under a content-coordinate click.
int resource_dialog_hit(const OriginalDialog &dialog, int x, int y) {
    x -= dialog.left;
    y -= dialog.top;
    for (int item_index = 0; item_index < dialog.count; ++item_index) {
        const auto &item_data = original_dialog_items[dialog.first + item_index];
        int type = item_data.type & 127;
        if (!(item_data.type & 128) &&
            (type == 4 || type == 5 || type == 6 || type == 16 || type == 64) &&
            x >= item_data.left && x < item_data.right && y >= item_data.top &&
            y < item_data.bottom)
            return item_index + 1;
    }
    return 0;
}
/* Native TrackControl adapter; no original entry point. A field/picture click
 * never captures a button, and an earlier press cannot carry into this one. */
void ResourceDialogButton::press(const OriginalDialog &dialog, int x, int y) {
    cancel();
    int item = resource_dialog_hit(dialog, x, y);
    if (item && original_dialog_items[dialog.first + item - 1].type == 4) {
        pressed_dialog = &dialog;
        pressed_item = item;
    }
}
/* Native ModalDialog adapter; no original entry point. Leaving and re-entering
 * is valid, but releasing over another item or a replacement dialog cancels. */
int ResourceDialogButton::release(const OriginalDialog *active_dialog, int x, int y) {
    int item = active_dialog && active_dialog == pressed_dialog &&
                       resource_dialog_hit(*active_dialog, x, y) == pressed_item
                   ? pressed_item
                   : 0;
    cancel();
    return item;
}
/* Native lifetime/input boundary; no original entry point. Dialog replacement,
 * keyboard completion and focus loss invalidate a pending mouse press. */
void ResourceDialogButton::cancel() {
    pressed_dialog = nullptr;
    pressed_item = 0;
}
// Native platform adapter (no direct original address): expand DITL text parameter markers and
// translate Mac line endings.
static std::string substitute(const char *text, const char *const parameters[4]) {
    std::string result;
    for (const char *p = text; *p; ++p) {
        if (*p == '^' && p[1] >= '0' && p[1] <= '3') {
            unsigned parameter_digit = *++p;
            if (parameters && parameters[parameter_digit - '0'])
                result += parameters[parameter_digit - '0'];
        } else
            result += *p == '\r' ? '\n' : *p;
    }
    return result;
}
// Native platform adapter (no direct original address): fit recovered DITL text into its resource
// bounds using the supplied Chicago strike, or the explicit SDL fallback.
static bool debug_text(SDL_Renderer *renderer, const SDL_FRect &bounds, const std::string &text) {
    std::string rest = text;
    float y = bounds.y;
    const int ascent = chicago_available ? chicago_ascent : 8;
    const int line_height =
        chicago_available ? chicago_ascent + chicago_descent + chicago_leading : 10;
    while (!rest.empty() && y < bounds.y + bounds.h) {
        size_t length = 0;
        int width = 0;
        std::string_view remaining = rest;
        while (!remaining.empty() && remaining.front() != '\n') {
            size_t before = remaining.size();
            int code = mac_bitmap_character(remaining);
            if (code < 0)
                return false;
            int advance = chicago_available ? chicago_glyphs[code].advance : 8;
            if (advance <= 0)
                return false;
            if (length && width + advance > bounds.w)
                break;
            width += advance;
            length += before - remaining.size();
        }
        if (length < rest.size() && rest[length] != '\n') {
            auto space = rest.rfind(' ', length);
            if (space != std::string::npos && space)
                length = space;
        }
        auto line = rest.substr(0, length);
        if (!(chicago_available ? mac_bitmap_draw(renderer, chicago_glyphs, chicago_pixels,
                                                  bounds.x, y + ascent, line)
                                : SDL_RenderDebugText(renderer, bounds.x, y, line.c_str())))
            return false;
        if (length < rest.size() && (rest[length] == ' ' || rest[length] == '\n'))
            ++length;
        rest.erase(0, length);
        y += line_height;
    }
    return true;
}
// Native platform adapter (no direct original address): paint the bitmap and text extracted from
// About PICT 10001 within its DITL destination.
static bool about_picture(SDL_Renderer *renderer, const SDL_FRect &item) {
    // PICT frame is (-1,-1,252,240); the DITL destination has the same 241x253 size.
    const float origin_x = item.x - original_about_frame[1],
                origin_y = item.y - original_about_frame[0];
    for (int y = 0; y < 100; ++y)
        for (int x = 0; x < 188; ++x) {
            unsigned bit = x + 2;
            if ((original_about_bitmap[y * 24 + bit / 8] >> (7 - bit % 8)) & 1)
                if (!SDL_RenderPoint(renderer, origin_x + 26 + x, origin_y + y))
                    return false;
        }
    SDL_Rect old{};
    bool clipped = SDL_RenderClipEnabled(renderer);
    if (!SDL_GetRenderClipRect(renderer, &old))
        return false;
    for (const auto &text_run : original_about_text) {
        SDL_Rect clip{int(origin_x + text_run.left), int(origin_y + text_run.top),
                      text_run.right - text_run.left, text_run.bottom - text_run.top};
        if (clipped) {
            SDL_Rect intersection{};
            if (!SDL_GetRectIntersection(&clip, &old, &intersection))
                continue;
            clip = intersection;
        }
        const bool use_geneva = geneva_available && text_run.font == 3 && text_run.size == 9 &&
                                !std::strcmp(text_run.font_name, "Geneva");
        if (!SDL_SetRenderClipRect(renderer, &clip) ||
            !(use_geneva
                  ? mac_bitmap_draw(renderer, geneva_glyphs, geneva_pixels, origin_x + text_run.x,
                                    origin_y + text_run.y, text_run.text)
                  : SDL_RenderDebugText(renderer, origin_x + text_run.x, origin_y + text_run.y - 8,
                                        text_run.text))) {
            SDL_SetRenderClipRect(renderer, clipped ? &old : nullptr);
            return false;
        }
    }
    return SDL_SetRenderClipRect(renderer, clipped ? &old : nullptr);
}
// Native platform adapter (no direct original address): paint DLOG/ALRT geometry and DITL controls,
// resolving picture items from PICT resources.
bool resource_dialog_draw(SDL_Renderer *renderer, const OriginalDialog &dialog,
                          const char *const parameters[4], ResourceDialogPicture picture,
                          void *context) {
    SDL_FRect bounds{float(dialog.left), float(dialog.top), float(dialog.right - dialog.left),
                     float(dialog.bottom - dialog.top)};
    if (!SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255) ||
        !SDL_RenderFillRect(renderer, &bounds) || !SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255) ||
        !SDL_RenderRect(renderer, &bounds))
        return false;
    // ERROR (file0x3b10/0x3b22) and modem alerts (0x7990/0x7a60) call StopAlert.
    // System7 reference pixels establish ICON0 at content-relative (20,10), size32x32.
    if (dialog.alert && (dialog.id == 402 || dialog.id == 406 || dialog.id == 407)) {
        for (int y = 0; y < 32; ++y)
            for (int x = 0; x < 32; ++x)
                if (((system_stop_icon[y * 4 + x / 8] >> (7 - x % 8)) & 1) &&
                    !SDL_RenderPoint(renderer, float(dialog.left + 20 + x),
                                     float(dialog.top + 10 + y)))
                    return false;
    }
    for (int item_index = 0; item_index < dialog.count; ++item_index) {
        const auto &item_data = original_dialog_items[dialog.first + item_index];
        int type = item_data.type & 127;
        SDL_FRect item{float(dialog.left + item_data.left), float(dialog.top + item_data.top),
                       float(item_data.right - item_data.left),
                       float(item_data.bottom - item_data.top)};
        if (type == 64) {
            auto *texture = picture ? picture(context, item_data.resource_id) : nullptr;
            if (texture) {
                if (!SDL_RenderTexture(renderer, texture, nullptr, &item))
                    return false;
            } else if (item_data.resource_id == 10001 && !about_picture(renderer, item))
                return false;
            continue;
        }
        auto text = substitute(item_data.text, parameters);
        if (type == 4 || type == 5 || type == 6) {
            SDL_Rect control{int(item.x), int(item.y), int(item.w), int(item.h)};
            // System7 CDEF0+0x3da: standard pushbutton oval axes are half its height.
            if (!(type == 4 ? quickdraw_round_frame(renderer, control, control.h / 2, 1)
                            : SDL_RenderRect(renderer, &item)))
                return false;
            // ALRT stage bit3 selects item1/2; RINGBUTT file0x10e0e requires exact type4.
            // All recovered alerts have identical four stage nibbles (0x5555).
            if (dialog.alert && item_index == ((dialog.stages >> 3) & 1) && item_data.type == 4) {
                SDL_Rect ring{control.x - 4, control.y - 4, control.w + 8, control.h + 8};
                if (!quickdraw_round_frame(renderer, ring, 16, 3))
                    return false;
            }
            const int width =
                chicago_available ? mac_bitmap_width(chicago_glyphs, text) : int(text.size() * 8);
            if (width < 0)
                return false;
            item.x += std::max(0, (int(item.w) - width) / 2);
            const int height = chicago_available ? chicago_ascent + chicago_descent : 8;
            item.y += (int(item.h) - height) / 2;
            item.h = height;
        }
        if (type == 16 && !SDL_RenderRect(renderer, &item))
            return false;
        if ((type == 4 || type == 5 || type == 6 || type == 8 || type == 16) &&
            !debug_text(renderer, item, text))
            return false;
    }
    return true;
}
