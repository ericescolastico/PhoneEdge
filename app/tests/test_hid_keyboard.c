#include "common.h"

#include <assert.h>

#include "hid/hid_keyboard.h"

static struct sc_key_event
key(enum sc_scancode scancode, enum sc_action action, uint16_t mods) {
    struct sc_key_event evt = {
        .action = action,
        .keycode = SC_KEYCODE_UNKNOWN,
        .scancode = scancode,
        .repeat = false,
        .mods_state = mods,
    };
    return evt;
}

static void test_letter(void) {
    struct sc_hid_keyboard hid;
    sc_hid_keyboard_init(&hid);

    struct sc_hid_input in;
    struct sc_key_event down = key(SC_SCANCODE_A, SC_ACTION_DOWN, 0);
    assert(sc_hid_keyboard_generate_input_from_key(&hid, &in, &down));
    assert(in.data[0] == 0);
    assert(in.data[2] == SC_SCANCODE_A);

    struct sc_key_event up = key(SC_SCANCODE_A, SC_ACTION_UP, 0);
    assert(sc_hid_keyboard_generate_input_from_key(&hid, &in, &up));
    assert(in.data[2] == 0);
}

static void test_abnt2_keys(void) {
    struct sc_hid_keyboard hid;
    sc_hid_keyboard_init(&hid);

    struct sc_hid_input in;
    // International1 (ABNT2 "/ ?") and Keypad Comma (ABNT2 numpad ".")
    struct sc_key_event k1 = key((enum sc_scancode) 0x87, SC_ACTION_DOWN, 0);
    assert(sc_hid_keyboard_generate_input_from_key(&hid, &in, &k1));
    assert(in.data[2] == 0x87);

    struct sc_key_event k2 = key((enum sc_scancode) 0x85, SC_ACTION_DOWN, 0);
    assert(sc_hid_keyboard_generate_input_from_key(&hid, &in, &k2));
    // the report lists the pressed keys by ascending scancode
    assert(in.data[2] == 0x85);
    assert(in.data[3] == 0x87);

    // Out of the supported range
    struct sc_key_event k3 = key((enum sc_scancode) 0x8C, SC_ACTION_DOWN, 0);
    assert(!sc_hid_keyboard_generate_input_from_key(&hid, &in, &k3));
}

static void test_modifier_only(void) {
    struct sc_hid_keyboard hid;
    sc_hid_keyboard_init(&hid);

    struct sc_hid_input in;
    struct sc_key_event ctrl =
        key(SC_SCANCODE_LCTRL, SC_ACTION_DOWN, SC_MOD_LCTRL);
    assert(sc_hid_keyboard_generate_input_from_key(&hid, &in, &ctrl));
    assert(in.data[0] == (1 << 0));
    assert(in.data[2] == 0);
}

int main(int argc, char *argv[]) {
    (void) argc;
    (void) argv;

    test_letter();
    test_abnt2_keys();
    test_modifier_only();
    return 0;
}
