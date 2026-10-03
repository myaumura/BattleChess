#ifndef BC_RESOURCE_DIALOG_H
#define BC_RESOURCE_DIALOG_H
#include <SDL3/SDL.h>
#include "original.hpp"
/* Original resource geometry; optional Apple bitmap fonts, otherwise explicit SDL fallback. */
const OriginalDialog *resource_dialog_find(int id, bool alert);
int resource_dialog_hit(const OriginalDialog &, int x, int y);
/* Native mouse adapter for ModalDialog (SETTIME 0x3894, DIALNUMB 0x39ea).
 * Capture only an enabled pushbutton and return its item on release inside it.
 * The retained dialog pointer refers to generated immutable resource data. */
class ResourceDialogButton {
  public:
    void press(const OriginalDialog &, int x, int y);
    int release(const OriginalDialog *active_dialog, int x, int y);
    void cancel();

  private:
    const OriginalDialog *pressed_dialog = nullptr;
    int pressed_item = 0;
};
using ResourceDialogPicture = SDL_Texture *(*)(void *context, int resource_id);
bool resource_dialog_draw(SDL_Renderer *, const OriginalDialog &,
                          const char *const parameters[4] = nullptr,
                          ResourceDialogPicture = nullptr, void *context = nullptr);
#endif
