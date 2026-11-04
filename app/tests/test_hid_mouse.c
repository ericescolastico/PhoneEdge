#include "common.h"

#include <assert.h>

#include "hid/hid_mouse.h"

static void test_scroll_keeps_buttons(void) {
    struct sc_hid_mouse hid;
    sc_hid_mouse_init(&hid);

    struct sc_mouse_scroll_event evt = {
        .hscroll = 0,
        .vscroll = -1,
        .buttons_state = SC_MOUSE_BUTTON_LEFT | SC_MOUSE_BUTTON_RIGHT,
    };
    struct sc_hid_input in;
    assert(sc_hid_mouse_generate_input_from_scroll(&hid, &in, &evt));
    // a scroll while buttons are held must not release them on the device
    assert(in.data[0] == 0x03);
    assert((int8_t) in.data[3] == -1);

    evt.buttons_state = 0;
    assert(sc_hid_mouse_generate_input_from_scroll(&hid, &in, &evt));
    assert(in.data[0] == 0);
}

static void test_motion_and_clamp(void) {
    struct sc_mouse_motion_event evt = {
        .xrel = 300,
        .yrel = -300,
        .buttons_state = SC_MOUSE_BUTTON_MIDDLE,
    };
    struct sc_hid_input in;
    sc_hid_mouse_generate_input_from_motion(&in, &evt);
    assert(in.data[0] == 0x04);
    assert((int8_t) in.data[1] == 127);
    assert((int8_t) in.data[2] == -127);
}

int main(int argc, char *argv[]) {
    (void) argc;
    (void) argv;
    test_scroll_keeps_buttons();
    test_motion_and_clamp();
    return 0;
}
