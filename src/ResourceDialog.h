#ifndef BC_RESOURCE_DIALOG_H
#define BC_RESOURCE_DIALOG_H

#include <SDL3/SDL.h>
#include "original.hpp"
#include <cstdint>

enum class ResourceDialogKind : uint8_t {
    dialog = 0,
    alert = 1,
};

/* Find immutable DLOG/ALRT resource data; nullptr means no matching resource. */
const OriginalDialog *findResourceDialog(int resourceId, ResourceDialogKind kind);

/* Content-coordinate hit test: one-based enabled DITL item, or zero for no hit. */
int hitResourceDialog(const OriginalDialog &dialog, int x, int y);

/* Native mouse adapter for ModalDialog (SETTIME 0x3894, DIALNUMB 0x39ea).
 * Capture only an enabled pushbutton and return its item on release inside it.
 * The retained dialog pointer refers to generated immutable resource data. */
class ResourceDialogButton {
  public:
    void press(const OriginalDialog &dialog, int x, int y);
    int release(const OriginalDialog *activeDialog, int x, int y);
    void cancel();

  private:
    const OriginalDialog *pressedDialog = nullptr;
    int pressedItem = 0;
};

/* Return a borrowed PICT texture, or nullptr when the resource is unavailable. */
using ResourceDialogPictureProvider = SDL_Texture *(*)(void *context, int resourceId);

/* Draw original resource geometry; false means a text or SDL rendering failure.
 * Text substitutes ^0..^3 from parameters, using Apple bitmap fonts or the SDL fallback.
 * Missing picture textures use the built-in About PICT renderer when applicable. */
bool drawResourceDialog(SDL_Renderer *renderer, const OriginalDialog &dialog,
                        const char *const parameters[4] = nullptr,
                        ResourceDialogPictureProvider pictureProvider = nullptr,
                        void *context = nullptr);

#endif
