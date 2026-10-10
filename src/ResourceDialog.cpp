#include "ResourceDialog.h"
#include "MacBitmapFont.h"
#include "QuickDrawControl.h"
#include <algorithm>
#include <cstring>
#include <optional>
#include <string>
#include <string_view>

namespace {
    constexpr int kItemTypeMask = 0x7f;
    constexpr int kDisabledItemFlag = 0x80;
    constexpr int kAboutPictureResourceId = 10001;

    enum class DialogItemType : uint8_t {
        pushButton = 4,
        checkBox = 5,
        radioButton = 6,
        staticText = 8,
        editText = 16,
        picture = 64,
    };

    DialogItemType itemType(const OriginalDialogItem &item) {
        return static_cast<DialogItemType>(item.type & kItemTypeMask);
    }

    bool isControlItem(DialogItemType type) {
        return type == DialogItemType::pushButton || type == DialogItemType::checkBox ||
               type == DialogItemType::radioButton;
    }

    bool isTextItem(DialogItemType type) {
        return isControlItem(type) || type == DialogItemType::staticText ||
               type == DialogItemType::editText;
    }
} // namespace

// Native platform adapter (no direct original address): find dialog geometry recovered from
// DLOG/ALRT and DITL resources.
const OriginalDialog *findResourceDialog(int resourceId, ResourceDialogKind kind) {
    for (const auto &dialog : original_dialogs)
        if (dialog.id == resourceId && dialog.alert == static_cast<int>(kind))
            return &dialog;
    return nullptr;
}
// Native platform adapter (no direct original address): return the one-based enabled DITL item
// under a content-coordinate click.
int hitResourceDialog(const OriginalDialog &dialog, int x, int y) {
    x -= dialog.left;
    y -= dialog.top;
    for (int itemIndex = 0; itemIndex < dialog.count; ++itemIndex) {
        const auto &itemData = original_dialog_items[dialog.first + itemIndex];
        const auto type = itemType(itemData);
        if (!(itemData.type & kDisabledItemFlag) &&
            (isControlItem(type) || type == DialogItemType::editText ||
             type == DialogItemType::picture) &&
            x >= itemData.left && x < itemData.right && y >= itemData.top && y < itemData.bottom)
            return itemIndex + 1;
    }
    return 0;
}
/* Native TrackControl adapter; no original entry point. A field/picture click
 * never captures a button, and an earlier press cannot carry into this one. */
void ResourceDialogButton::press(const OriginalDialog &dialog, int x, int y) {
    cancel();
    int item = hitResourceDialog(dialog, x, y);
    if (item && original_dialog_items[dialog.first + item - 1].type ==
                    static_cast<int>(DialogItemType::pushButton)) {
        pressedDialog = &dialog;
        pressedItem = item;
    }
}
/* Native ModalDialog adapter; no original entry point. Leaving and re-entering
 * is valid, but releasing over another item or a replacement dialog cancels. */
int ResourceDialogButton::release(const OriginalDialog *activeDialog, int x, int y) {
    int item = activeDialog && activeDialog == pressedDialog &&
                       hitResourceDialog(*activeDialog, x, y) == pressedItem
                   ? pressedItem
                   : 0;
    cancel();
    return item;
}
/* Native lifetime/input boundary; no original entry point. Dialog replacement,
 * keyboard completion and focus loss invalidate a pending mouse press. */
void ResourceDialogButton::cancel() {
    pressedDialog = nullptr;
    pressedItem = 0;
}

namespace {
    // Native platform adapter (no direct original address): expand DITL text parameter markers and
    // translate Mac line endings.
    std::string substituteDialogText(const char *text, const char *const parameters[4]) {
        std::string result;
        for (const char *character = text; *character; ++character) {
            if (*character == '^' && character[1] >= '0' && character[1] <= '3') {
                unsigned parameterDigit = *++character;
                if (parameters && parameters[parameterDigit - '0'])
                    result += parameters[parameterDigit - '0'];
            } else
                result += *character == '\r' ? '\n' : *character;
        }
        return result;
    }
    // Keep the original glyph-width scan and last-space wrapping decision.
    std::optional<size_t> wrappedLineLength(std::string_view text, float maxWidth) {
        size_t length = 0;
        int width = 0;
        std::string_view remaining = text;
        while (!remaining.empty() && remaining.front() != '\n') {
            size_t before = remaining.size();
            int code = macBitmapCharacter(remaining);
            if (code < 0)
                return std::nullopt;
            int advance = chicago_available ? chicago_glyphs[code].advance : 8;
            if (advance <= 0)
                return std::nullopt;
            if (length && width + advance > maxWidth)
                break;
            width += advance;
            length += before - remaining.size();
        }
        if (length < text.size() && text[length] != '\n') {
            auto space = text.rfind(' ', length);
            if (space != std::string_view::npos && space)
                length = space;
        }
        return length;
    }

    // Native platform adapter (no direct original address): fit recovered DITL text into its
    // resource bounds using the supplied Chicago strike, or the explicit SDL fallback.
    bool drawDialogText(SDL_Renderer *renderer, const SDL_FRect &bounds, const std::string &text) {
        std::string remainingText = text;
        float y = bounds.y;
        const int ascent = chicago_available ? chicago_ascent : 8;
        const int lineHeight =
            chicago_available ? chicago_ascent + chicago_descent + chicago_leading : 10;
        while (!remainingText.empty() && y < bounds.y + bounds.h) {
            auto length = wrappedLineLength(remainingText, bounds.w);
            if (!length)
                return false;
            auto line = remainingText.substr(0, *length);
            if (!(chicago_available ? macBitmapDraw(renderer, chicago_glyphs, chicago_pixels,
                                                    bounds.x, y + ascent, line)
                                    : SDL_RenderDebugText(renderer, bounds.x, y, line.c_str())))
                return false;
            size_t consumed = *length;
            if (consumed < remainingText.size() &&
                (remainingText[consumed] == ' ' || remainingText[consumed] == '\n'))
                ++consumed;
            remainingText.erase(0, consumed);
            y += lineHeight;
        }
        return true;
    }
    bool drawAboutBitmap(SDL_Renderer *renderer, float originX, float originY) {
        for (int y = 0; y < 100; ++y)
            for (int x = 0; x < 188; ++x) {
                unsigned bit = x + 2;
                if ((original_about_bitmap[y * 24 + bit / 8] >> (7 - bit % 8)) & 1)
                    if (!SDL_RenderPoint(renderer, originX + 26 + x, originY + y))
                        return false;
            }
        return true;
    }

    bool drawAboutText(SDL_Renderer *renderer, float originX, float originY) {
        SDL_Rect previousClip{};
        bool clipped = SDL_RenderClipEnabled(renderer);
        if (!SDL_GetRenderClipRect(renderer, &previousClip))
            return false;
        for (const auto &textRun : original_about_text) {
            SDL_Rect clip{int(originX + textRun.left), int(originY + textRun.top),
                          textRun.right - textRun.left, textRun.bottom - textRun.top};
            if (clipped) {
                SDL_Rect intersection{};
                if (!SDL_GetRectIntersection(&clip, &previousClip, &intersection))
                    continue;
                clip = intersection;
            }
            const bool useGeneva = geneva_available && textRun.font == 3 && textRun.size == 9 &&
                                   !std::strcmp(textRun.font_name, "Geneva");
            if (!SDL_SetRenderClipRect(renderer, &clip) ||
                !(useGeneva ? macBitmapDraw(renderer, geneva_glyphs, geneva_pixels,
                                            originX + textRun.x, originY + textRun.y, textRun.text)
                            : SDL_RenderDebugText(renderer, originX + textRun.x,
                                                  originY + textRun.y - 8, textRun.text))) {
                SDL_SetRenderClipRect(renderer, clipped ? &previousClip : nullptr);
                return false;
            }
        }
        return SDL_SetRenderClipRect(renderer, clipped ? &previousClip : nullptr);
    }

    // Native platform adapter (no direct original address): paint the bitmap and text extracted
    // from About PICT 10001 within its DITL destination.
    bool drawAboutPicture(SDL_Renderer *renderer, const SDL_FRect &item) {
        // PICT frame is (-1,-1,252,240); the DITL destination has the same 241x253 size.
        const float originX = item.x - original_about_frame[1],
                    originY = item.y - original_about_frame[0];
        return drawAboutBitmap(renderer, originX, originY) &&
               drawAboutText(renderer, originX, originY);
    }

    bool drawDialogFrame(SDL_Renderer *renderer, const OriginalDialog &dialog) {
        SDL_FRect bounds{float(dialog.left), float(dialog.top), float(dialog.right - dialog.left),
                         float(dialog.bottom - dialog.top)};
        return SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255) &&
               SDL_RenderFillRect(renderer, &bounds) &&
               SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255) && SDL_RenderRect(renderer, &bounds);
    }

    bool drawStopAlertIcon(SDL_Renderer *renderer, const OriginalDialog &dialog) {
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
        return true;
    }

    bool drawPictureItem(SDL_Renderer *renderer, const OriginalDialogItem &itemData,
                         const SDL_FRect &bounds, ResourceDialogPictureProvider pictureProvider,
                         void *context) {
        auto *texture = pictureProvider ? pictureProvider(context, itemData.resource_id) : nullptr;
        if (texture)
            return SDL_RenderTexture(renderer, texture, nullptr, &bounds);
        if (itemData.resource_id == kAboutPictureResourceId)
            return drawAboutPicture(renderer, bounds);
        return true;
    }

    bool drawControlFrame(SDL_Renderer *renderer, const OriginalDialog &dialog, int itemIndex,
                          const OriginalDialogItem &itemData, const SDL_FRect &bounds) {
        SDL_Rect control{int(bounds.x), int(bounds.y), int(bounds.w), int(bounds.h)};
        // System7 CDEF0+0x3da: standard pushbutton oval axes are half its height.
        if (!(itemType(itemData) == DialogItemType::pushButton
                  ? drawQuickDrawRoundFrame(renderer, control, control.h / 2, 1)
                  : SDL_RenderRect(renderer, &bounds)))
            return false;
        // ALRT stage bit3 selects item1/2; RINGBUTT file0x10e0e requires exact type4.
        // All recovered alerts have identical four stage nibbles (0x5555).
        if (dialog.alert && itemIndex == ((dialog.stages >> 3) & 1) &&
            itemData.type == static_cast<int>(DialogItemType::pushButton)) {
            SDL_Rect ring{control.x - 4, control.y - 4, control.w + 8, control.h + 8};
            if (!drawQuickDrawRoundFrame(renderer, ring, 16, 3))
                return false;
        }
        return true;
    }

    bool centerControlText(SDL_FRect &bounds, const std::string &text) {
        const int width =
            chicago_available ? macBitmapWidth(chicago_glyphs, text) : int(text.size() * 8);
        if (width < 0)
            return false;
        bounds.x += std::max(0, (int(bounds.w) - width) / 2);
        const int height = chicago_available ? chicago_ascent + chicago_descent : 8;
        bounds.y += (int(bounds.h) - height) / 2;
        bounds.h = height;
        return true;
    }

    bool drawDialogItem(SDL_Renderer *renderer, const OriginalDialog &dialog, int itemIndex,
                        const char *const parameters[4],
                        ResourceDialogPictureProvider pictureProvider, void *context) {
        const auto &itemData = original_dialog_items[dialog.first + itemIndex];
        const auto type = itemType(itemData);
        SDL_FRect bounds{float(dialog.left + itemData.left), float(dialog.top + itemData.top),
                         float(itemData.right - itemData.left),
                         float(itemData.bottom - itemData.top)};
        if (type == DialogItemType::picture)
            return drawPictureItem(renderer, itemData, bounds, pictureProvider, context);

        auto text = substituteDialogText(itemData.text, parameters);
        if (isControlItem(type) &&
            (!drawControlFrame(renderer, dialog, itemIndex, itemData, bounds) ||
             !centerControlText(bounds, text)))
            return false;
        if (type == DialogItemType::editText && !SDL_RenderRect(renderer, &bounds))
            return false;
        if (isTextItem(type) && !drawDialogText(renderer, bounds, text))
            return false;
        return true;
    }
} // namespace

// Native platform adapter (no direct original address): paint DLOG/ALRT geometry and DITL controls,
// resolving picture items from PICT resources.
bool drawResourceDialog(SDL_Renderer *renderer, const OriginalDialog &dialog,
                        const char *const parameters[4],
                        ResourceDialogPictureProvider pictureProvider, void *context) {
    if (!drawDialogFrame(renderer, dialog) || !drawStopAlertIcon(renderer, dialog))
        return false;
    for (int itemIndex = 0; itemIndex < dialog.count; ++itemIndex)
        if (!drawDialogItem(renderer, dialog, itemIndex, parameters, pictureProvider, context))
            return false;
    return true;
}
