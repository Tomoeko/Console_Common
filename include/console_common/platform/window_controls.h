#ifndef CONSOLE_COMMON_WINDOW_CONTROLS_H
#define CONSOLE_COMMON_WINDOW_CONTROLS_H

#include "console_common/platform/platform.h"

/* Zero initialize once per window. Press ownership survives scene changes. */
typedef struct CcWindowControls {
    bool fullscreen_key_held;
    bool fullscreen_key_consumed;
} CcWindowControls;

/* Call before routing each event to a scene, including focus-loss events.
 * F toggles fullscreen once per physical press. Text editors retain F input.
 * True consumes the event even when the native window rejects the request. */
bool cc_window_controls_event(CcWindowControls *controls, CcPlatform *platform,
                              const CcEvent *event, bool text_input_active);

#endif
