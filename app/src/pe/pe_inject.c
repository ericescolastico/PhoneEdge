#include "pe_inject.h"

#include "control_msg.h"
#include "input_events.h"

void
pe_inject_init(struct pe_inject *inj, struct sc_mouse_processor *mp,
               struct sc_key_processor *kp) {
    inj->mp = mp;
    inj->kp = kp;
    inj->sc_buttons = 0;
}

uint8_t
pe_inject_buttons_to_sc(uint8_t b) {
    uint8_t sc = 0;
    if (b & PE_BTN_LEFT) {
        sc |= SC_MOUSE_BUTTON_LEFT;
    }
    if (b & PE_BTN_RIGHT) {
        sc |= SC_MOUSE_BUTTON_RIGHT;
    }
    if (b & PE_BTN_MIDDLE) {
        sc |= SC_MOUSE_BUTTON_MIDDLE;
    }
    if (b & PE_BTN_X1) {
        sc |= SC_MOUSE_BUTTON_X1;
    }
    if (b & PE_BTN_X2) {
        sc |= SC_MOUSE_BUTTON_X2;
    }
    return sc;
}

uint16_t
pe_inject_mods_to_sc(uint8_t hid) {
    uint16_t m = 0;
    if (hid & PE_HID_LCTRL) {
        m |= SC_MOD_LCTRL;
    }
    if (hid & PE_HID_LSHIFT) {
        m |= SC_MOD_LSHIFT;
    }
    if (hid & PE_HID_LALT) {
        m |= SC_MOD_LALT;
    }
    if (hid & PE_HID_LGUI) {
        m |= SC_MOD_LGUI;
    }
    if (hid & PE_HID_RCTRL) {
        m |= SC_MOD_RCTRL;
    }
    if (hid & PE_HID_RSHIFT) {
        m |= SC_MOD_RSHIFT;
    }
    if (hid & PE_HID_RALT) {
        m |= SC_MOD_RALT;
    }
    if (hid & PE_HID_RGUI) {
        m |= SC_MOD_RGUI;
    }
    return m;
}

static void
op_move(void *ud, int dx, int dy, bool placement, unsigned delay_ms) {
    (void) placement;
    (void) delay_ms;
    struct pe_inject *inj = ud;
    struct sc_mouse_motion_event evt = {
        // relative mode: no absolute position
        .position = {.screen_size = {0, 0}, .point = {0, 0}},
        .pointer_id = SC_POINTER_ID_MOUSE,
        .xrel = dx,
        .yrel = dy,
        .buttons_state = inj->sc_buttons,
    };
    inj->mp->ops->process_mouse_motion(inj->mp, &evt);
}

static void
op_buttons(void *ud, uint8_t buttons) {
    struct pe_inject *inj = ud;
    uint8_t now = pe_inject_buttons_to_sc(buttons);
    uint8_t before = inj->sc_buttons;
    if (now == before) {
        return;
    }

    // The HID report carries the whole state, so one event is enough; the
    // action/button fields are informative
    uint8_t changed = now ^ before;
    inj->sc_buttons = now;
    struct sc_mouse_click_event evt = {
        .position = {.screen_size = {0, 0}, .point = {0, 0}},
        .action = (now & changed) ? SC_ACTION_DOWN : SC_ACTION_UP,
        .button = (enum sc_mouse_button) (changed & -changed),
        .pointer_id = SC_POINTER_ID_MOUSE,
        .buttons_state = now,
    };
    inj->mp->ops->process_mouse_click(inj->mp, &evt);
}

static void
op_wheel(void *ud, float horizontal, float vertical) {
    struct pe_inject *inj = ud;
    if (!inj->mp->ops->process_mouse_scroll) {
        return;
    }
    struct sc_mouse_scroll_event evt = {
        .position = {.screen_size = {0, 0}, .point = {0, 0}},
        .hscroll = horizontal,
        .vscroll = vertical,
        .buttons_state = inj->sc_buttons,
    };
    inj->mp->ops->process_mouse_scroll(inj->mp, &evt);
}

static void
op_key(void *ud, uint8_t scancode, bool down, uint8_t hid_mods) {
    struct pe_inject *inj = ud;
    struct sc_key_event evt = {
        .action = down ? SC_ACTION_DOWN : SC_ACTION_UP,
        .keycode = SC_KEYCODE_UNKNOWN,
        .scancode = (enum sc_scancode) scancode,
        .repeat = false,
        .mods_state = pe_inject_mods_to_sc(hid_mods),
    };
    inj->kp->ops->process_key(inj->kp, &evt, 0 /* no ack to wait */);
}

const struct pe_phone_ops pe_inject_ops = {
    .move = op_move,
    .buttons = op_buttons,
    .wheel = op_wheel,
    .key = op_key,
};
