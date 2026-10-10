/* SDL keyboard/mouse event bridge from the window thread to guest pad readers. */
#ifndef BB_RUNTIME_PAD_INPUT_H
#define BB_RUNTIME_PAD_INPUT_H
#include "runtime_pad_bindings.h"

union SDL_Event;
#ifdef __cplusplus
extern "C" {
#endif

/* active excludes lost focus, the settings overlay and system text entry.
 * mouse_captured is the actual SDL relative-mode state, not the preference. */
void runtime_pad_input_configure(int active, int mouse_captured, int enabled,
                                 float sensitivity, int invert_y, int dpad_camera);
void runtime_pad_input_event(const union SDL_Event *event);
/* Replace the complete map atomically and release held KBM input. */
int runtime_pad_input_set_bindings(const int *bindings, unsigned count);
/* Publish relative motion at a fixed time cadence, independent of pad polling. */
void runtime_pad_input_publish(void);

#ifdef __cplusplus
}
#endif
#endif
