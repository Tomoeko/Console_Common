#include "console_common/platform/window_controls.h"

bool cc_window_controls_event(CcWindowControls *controls, CcPlatform *platform,
                              const CcEvent *event, bool text_input_active) {
    if (!controls || !event)
        return false;
    if (event->cancel_capture) {
        *controls = (CcWindowControls){0};
        return false;
    }
    if ((event->type != CC_EVENT_KEY_DOWN && event->type != CC_EVENT_KEY_UP) ||
        (event->key != 'f' && event->key != 'F'))
        return false;
    if (event->type == CC_EVENT_KEY_UP) {
        bool consumed = controls->fullscreen_key_consumed;
        *controls = (CcWindowControls){0};
        return consumed;
    }
    if (event->key_repeat)
        return controls->fullscreen_key_held ? controls->fullscreen_key_consumed
                                             : !text_input_active;
    if (controls->fullscreen_key_held)
        return controls->fullscreen_key_consumed;

    controls->fullscreen_key_held = true;
    controls->fullscreen_key_consumed = !text_input_active;
    if (controls->fullscreen_key_consumed)
        cc_platform_set_fullscreen(platform, !cc_platform_is_fullscreen(platform));
    return controls->fullscreen_key_consumed;
}
