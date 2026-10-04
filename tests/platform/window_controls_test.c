#include "console_common/platform/window_controls.h"

#include <assert.h>
#include <stdio.h>

struct CcPlatform {
    bool fullscreen;
    bool reject_request;
    unsigned requests;
};

bool cc_platform_is_fullscreen(CcPlatform *platform) {
    return platform && platform->fullscreen;
}

bool cc_platform_set_fullscreen(CcPlatform *platform, bool fullscreen) {
    if (!platform)
        return false;
    ++platform->requests;
    if (platform->reject_request)
        return false;
    platform->fullscreen = fullscreen;
    return true;
}

static bool send_key(CcWindowControls *controls, CcPlatform *platform, CcKey key,
                     bool down, bool repeat, bool editing) {
    CcEvent event = {.type = down ? CC_EVENT_KEY_DOWN : CC_EVENT_KEY_UP,
                     .key = key,
                     .key_repeat = repeat};
    return cc_window_controls_event(controls, platform, &event, editing);
}

static void test_one_toggle_per_press(void) {
    CcPlatform window = {0};
    CcWindowControls controls = {0};
    assert(send_key(&controls, &window, (CcKey)'f', true, false, false));
    assert(window.fullscreen && window.requests == 1);
    assert(send_key(&controls, &window, (CcKey)'F', true, false, false));
    assert(send_key(&controls, &window, (CcKey)'F', true, true, false));
    assert(window.fullscreen && window.requests == 1);
    assert(send_key(&controls, &window, (CcKey)'F', false, false, false));
    assert(send_key(&controls, &window, (CcKey)'F', true, false, false));
    assert(!window.fullscreen && window.requests == 2);
    assert(send_key(&controls, &window, (CcKey)'f', false, false, false));
    assert(!send_key(&controls, &window, CC_KEY_ESCAPE, true, false, false));
    assert(!send_key(&controls, &window, (CcKey)'a', true, false, false));
    assert(!cc_window_controls_event(
        &controls, &window, &(CcEvent){.type = CC_EVENT_WINDOW_RESIZED}, false));
    assert(window.requests == 2);
}

static void test_focus_loss_and_repeat(void) {
    CcPlatform window = {0};
    CcWindowControls controls = {0};
    assert(send_key(&controls, &window, (CcKey)'f', true, false, false));
    assert(!cc_window_controls_event(
        &controls, &window,
        &(CcEvent){.type = CC_EVENT_POINTER_LEAVE, .cancel_capture = true}, false));
    assert(send_key(&controls, &window, (CcKey)'f', true, true, false));
    assert(window.fullscreen && window.requests == 1);
    assert(send_key(&controls, &window, (CcKey)'f', true, false, false));
    assert(!window.fullscreen && window.requests == 2);
}

static void test_editor_owns_entire_press(void) {
    CcPlatform window = {0};
    CcWindowControls controls = {0};
    assert(!send_key(&controls, &window, (CcKey)'f', true, false, true));
    assert(!send_key(&controls, &window, (CcKey)'F', true, true, true));
    /* Closing an editor while F is held must not turn its text into a shortcut. */
    assert(!send_key(&controls, &window, (CcKey)'F', true, false, false));
    assert(!send_key(&controls, &window, (CcKey)'F', true, true, false));
    assert(!send_key(&controls, &window, (CcKey)'f', false, false, false));
    assert(window.requests == 0);

    assert(send_key(&controls, &window, (CcKey)'F', true, false, false));
    /* Opening an editor cannot leak repeats or release from a consumed press. */
    assert(send_key(&controls, &window, (CcKey)'f', true, true, true));
    assert(send_key(&controls, &window, (CcKey)'F', false, false, true));
    assert(window.fullscreen && window.requests == 1);
    assert(!send_key(&controls, &window, (CcKey)'f', true, true, true));
    assert(!send_key(&controls, &window, (CcKey)'f', true, false, true));
    assert(window.requests == 1);
}

static void test_native_rejection_and_independent_windows(void) {
    CcPlatform first = {.reject_request = true};
    CcPlatform second = {0};
    CcWindowControls first_controls = {0};
    CcWindowControls second_controls = {0};
    assert(send_key(&first_controls, &first, (CcKey)'f', true, false, false));
    assert(!first.fullscreen && first.requests == 1);
    first.reject_request = false;
    assert(send_key(&first_controls, &first, (CcKey)'F', true, true, false));
    assert(!first.fullscreen && first.requests == 1);
    assert(send_key(&second_controls, &second, (CcKey)'F', true, false, false));
    assert(second.fullscreen && second.requests == 1);
    assert(send_key(&first_controls, &first, (CcKey)'f', false, false, false));
    assert(send_key(&first_controls, &first, (CcKey)'f', true, false, false));
    assert(first.fullscreen && first.requests == 2);
    assert(!cc_window_controls_event(NULL, &first, &(CcEvent){0}, false));
    assert(!cc_window_controls_event(&first_controls, &first, NULL, false));
}

int main(void) {
    test_one_toggle_per_press();
    test_focus_loss_and_repeat();
    test_editor_owns_entire_press();
    test_native_rejection_and_independent_windows();
    puts("Shared fullscreen shortcuts, text input and focus recovery passed.");
    return 0;
}
