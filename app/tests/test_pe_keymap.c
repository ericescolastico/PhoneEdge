#include "common.h"

#include <assert.h>
#include <string.h>

#include "edge/pe_keymap.h"

static void test_letters_and_digits(void) {
    // Windows set 1 scancodes: A=0x1E, S=0x1F, Q=0x10, Z=0x2C, M=0x32
    assert(pe_keymap_from_windows(0x1E, false, 0) == 0x04); // A
    assert(pe_keymap_from_windows(0x1F, false, 0) == 0x16); // S
    assert(pe_keymap_from_windows(0x10, false, 0) == 0x14); // Q
    assert(pe_keymap_from_windows(0x2C, false, 0) == 0x1D); // Z
    assert(pe_keymap_from_windows(0x32, false, 0) == 0x10); // M
    assert(pe_keymap_from_windows(0x02, false, 0) == 0x1E); // 1
    assert(pe_keymap_from_windows(0x0B, false, 0) == 0x27); // 0
}

// Every letter has the right usage: a..z -> 0x04..0x1D
static void test_alphabet(void) {
    // QWERTY rows in scancode order
    const char *rows[] = {"qwertyuiop", "asdfghjkl", "zxcvbnm"};
    const uint8_t first[] = {0x10, 0x1E, 0x2C};
    for (int r = 0; r < 3; ++r) {
        for (size_t i = 0; i < strlen(rows[r]); ++i) {
            uint8_t hid = pe_keymap_from_windows(first[r] + i, false, 0);
            assert(hid == 0x04 + (rows[r][i] - 'a'));
        }
    }
}

static void test_function_and_nav(void) {
    for (int i = 0; i < 10; ++i) {
        assert(pe_keymap_from_windows(0x3B + i, false, 0) == 0x3A + i);
    }
    assert(pe_keymap_from_windows(0x57, false, 0) == 0x44); // F11
    assert(pe_keymap_from_windows(0x58, false, 0) == 0x45); // F12
    assert(pe_keymap_from_windows(0x47, true, 0) == 0x4A);  // Home
    assert(pe_keymap_from_windows(0x48, true, 0) == 0x52);  // Up
    assert(pe_keymap_from_windows(0x53, true, 0) == 0x4C);  // Delete
    // keypad 7 (no prefix) is not Home (prefix)
    assert(pe_keymap_from_windows(0x47, false, 0) == 0x5F);
    assert(pe_keymap_from_windows(0x1C, true, 0) == 0x58);  // Keypad Enter
    assert(pe_keymap_from_windows(0x1C, false, 0) == 0x28); // Enter
}

static void test_modifiers(void) {
    assert(pe_keymap_from_windows(0x1D, false, 0) == 0xE0); // Left Ctrl
    assert(pe_keymap_from_windows(0x1D, true, 0) == 0xE4);  // Right Ctrl
    assert(pe_keymap_from_windows(0x2A, false, 0) == 0xE1); // Left Shift
    assert(pe_keymap_from_windows(0x36, false, 0) == 0xE5); // Right Shift
    assert(pe_keymap_from_windows(0x38, false, 0) == 0xE2); // Left Alt
    assert(pe_keymap_from_windows(0x38, true, 0) == 0xE6);  // AltGr
    assert(pe_keymap_from_windows(0x5B, true, 0) == 0xE3);  // Left Win

    assert(pe_keymap_hid_mod_bit(0xE0) == PE_HID_LCTRL);
    assert(pe_keymap_hid_mod_bit(0xE6) == PE_HID_RALT);
    assert(pe_keymap_hid_mod_bit(0xE7) == PE_HID_RGUI);
    assert(pe_keymap_hid_mod_bit(0x04) == 0);
    assert(pe_keymap_hid_mod_bit(0xE8) == 0);

    assert(pe_keymap_generic_mods(PE_HID_RCTRL | PE_HID_LALT)
           == (PE_MOD_CTRL | PE_MOD_ALT));
    assert(pe_keymap_generic_mods(0) == 0);
}

static void test_pause_numlock(void) {
    // Pause/Break and NumLock share the scancode 0x45: the virtual-key wins
    assert(pe_keymap_from_windows(0x45, false, 0x13) == PE_SC_PAUSE);
    assert(pe_keymap_from_windows(0x45, true, 0x13) == PE_SC_PAUSE);
    assert(pe_keymap_from_windows(0x45, true, 0x90) == 0x53);
    assert(pe_keymap_from_windows(0x45, false, 0x90) == 0x53);
    // Ctrl+Pause is VK_CANCEL, scancode 0x46 with the extended prefix
    assert(pe_keymap_from_windows(0x46, true, 0x03) == PE_SC_PAUSE);
    assert(pe_keymap_from_windows(0x46, true, 0) == PE_SC_PAUSE);
    // Scroll Lock is 0x46 without prefix
    assert(pe_keymap_from_windows(0x46, false, 0x91) == 0x47);
}

static void test_abnt2_and_iso(void) {
    assert(pe_keymap_from_windows(0x73, false, 0) == 0x87); // ABNT2 "/ ?"
    assert(pe_keymap_from_windows(0x7E, false, 0) == 0x85); // ABNT2 numpad "."
    assert(pe_keymap_from_windows(0x56, false, 0) == 0x64); // ISO "\ |"
    assert(pe_keymap_from_windows(0x29, false, 0) == 0x35); // ' "
    assert(pe_keymap_from_windows(0x28, false, 0) == 0x34); // ~ ^
    assert(pe_keymap_from_windows(0x1A, false, 0) == 0x2F); // ´ `
    assert(pe_keymap_from_windows(0x27, false, 0) == 0x33); // ç
}

static void test_unknown(void) {
    assert(pe_keymap_from_windows(0, false, 0) == 0);
    assert(pe_keymap_from_windows(0x7F, false, 0) == 0);
    assert(pe_keymap_from_windows(0x200, false, 0) == 0);
    assert(pe_keymap_from_windows(0x20, true, 0) == 0); // media key
}

static void test_no_collisions(void) {
    // Two different Windows keys never produce the same HID usage
    uint8_t seen[256] = {0};
    for (int ext = 0; ext < 2; ++ext) {
        for (int sc = 1; sc < 0x80; ++sc) {
            if (sc == 0x45) {
                continue; // vk-dependent
            }
            uint8_t hid = pe_keymap_from_windows(sc, ext, 0);
            if (hid) {
                // the extended Ctrl+Break and Pause both map to Pause only
                assert(!seen[hid]);
                seen[hid] = 1;
            }
        }
    }
}

static void test_key_names(void) {
    assert(pe_key_from_name("a") == 0x04);
    assert(pe_key_from_name("Z") == 0x1D);
    assert(pe_key_from_name("1") == 0x1E);
    assert(pe_key_from_name("0") == 0x27);
    assert(pe_key_from_name("F1") == 0x3A);
    assert(pe_key_from_name("f12") == 0x45);
    assert(pe_key_from_name("f13") == 0);
    assert(pe_key_from_name("F0") == 0);
    assert(pe_key_from_name("Pause") == PE_SC_PAUSE);
    assert(pe_key_from_name("BREAK") == PE_SC_PAUSE);
    assert(pe_key_from_name("ScrollLock") == 0x47);
    assert(pe_key_from_name("") == 0);
    assert(pe_key_from_name("nonsense") == 0);
    assert(pe_key_from_name("a-very-very-long-key-name") == 0);
}

static void test_hotkey_parse(void) {
    struct pe_hotkey hk;
    char err[100];

    assert(pe_hotkey_parse("ctrl+alt+pause", &hk, err, sizeof(err)));
    assert(hk.mods == (PE_MOD_CTRL | PE_MOD_ALT) && hk.key == PE_SC_PAUSE);

    assert(pe_hotkey_parse(" Ctrl + Shift + F12 ", &hk, err, sizeof(err)));
    assert(hk.mods == (PE_MOD_CTRL | PE_MOD_SHIFT) && hk.key == 0x45);

    assert(pe_hotkey_parse("win+alt+q", &hk, err, sizeof(err)));
    assert(hk.mods == (PE_MOD_GUI | PE_MOD_ALT) && hk.key == 0x14);

    // keys that nobody types may be used alone
    assert(pe_hotkey_parse("pause", &hk, err, sizeof(err)));
    assert(hk.mods == 0);
    assert(pe_hotkey_parse("f9", &hk, err, sizeof(err)));

    // ... but not letters
    assert(!pe_hotkey_parse("a", &hk, err, sizeof(err)));
    assert(strstr(err, "modifier"));
    assert(!pe_hotkey_parse("ctrl+alt", &hk, err, sizeof(err)));
    assert(strstr(err, "no main key"));
    assert(!pe_hotkey_parse("ctrl+a+b", &hk, err, sizeof(err)));
    assert(!pe_hotkey_parse("ctrl++a", &hk, err, sizeof(err)));
    assert(!pe_hotkey_parse("ctrl+banana", &hk, err, sizeof(err)));
    assert(strstr(err, "banana"));
    assert(!pe_hotkey_parse("", &hk, err, sizeof(err)));
}

static void test_hotkey_format(void) {
    struct pe_hotkey hk = {PE_MOD_CTRL | PE_MOD_ALT, PE_SC_PAUSE};
    char buf[64];
    pe_hotkey_format(&hk, buf, sizeof(buf));
    assert(!strcmp(buf, "Ctrl+Alt+Pause"));
    hk.mods = PE_MOD_SHIFT | PE_MOD_GUI;
    hk.key = 0x45;
    pe_hotkey_format(&hk, buf, sizeof(buf));
    assert(!strcmp(buf, "Shift+Win+F12"));
}

int main(int argc, char *argv[]) {
    (void) argc;
    (void) argv;

    test_letters_and_digits();
    test_alphabet();
    test_function_and_nav();
    test_modifiers();
    test_pause_numlock();
    test_abnt2_and_iso();
    test_unknown();
    test_no_collisions();
    test_key_names();
    test_hotkey_parse();
    test_hotkey_format();
    return 0;
}
