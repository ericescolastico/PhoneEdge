#include "pe_keymap.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

// Windows scancodes (set 1, no prefix) to HID usages, 0 if unmapped.
// The index is the scancode.
static const uint8_t SET1_TO_HID[0x80] = {
    [0x01] = 0x29, // Esc
    [0x02] = 0x1E, [0x03] = 0x1F, [0x04] = 0x20, [0x05] = 0x21, // 1 2 3 4
    [0x06] = 0x22, [0x07] = 0x23, [0x08] = 0x24, [0x09] = 0x25, // 5 6 7 8
    [0x0A] = 0x26, [0x0B] = 0x27,                               // 9 0
    [0x0C] = 0x2D, // -
    [0x0D] = 0x2E, // =
    [0x0E] = 0x2A, // Backspace
    [0x0F] = 0x2B, // Tab
    [0x10] = 0x14, [0x11] = 0x1A, [0x12] = 0x08, [0x13] = 0x15, // Q W E R
    [0x14] = 0x17, [0x15] = 0x1C, [0x16] = 0x18, [0x17] = 0x0C, // T Y U I
    [0x18] = 0x12, [0x19] = 0x13,                               // O P
    [0x1A] = 0x2F, // [
    [0x1B] = 0x30, // ]
    [0x1C] = 0x28, // Enter
    [0x1D] = 0xE0, // Left Ctrl
    [0x1E] = 0x04, [0x1F] = 0x16, [0x20] = 0x07, [0x21] = 0x09, // A S D F
    [0x22] = 0x0A, [0x23] = 0x0B, [0x24] = 0x0D, [0x25] = 0x0E, // G H J K
    [0x26] = 0x0F,                                              // L
    [0x27] = 0x33, // ;
    [0x28] = 0x34, // '
    [0x29] = 0x35, // `
    [0x2A] = 0xE1, // Left Shift
    [0x2B] = 0x31, // backslash
    [0x2C] = 0x1D, [0x2D] = 0x1B, [0x2E] = 0x06, [0x2F] = 0x19, // Z X C V
    [0x30] = 0x05, [0x31] = 0x11, [0x32] = 0x10,                // B N M
    [0x33] = 0x36, // ,
    [0x34] = 0x37, // .
    [0x35] = 0x38, // /
    [0x36] = 0xE5, // Right Shift
    [0x37] = 0x55, // Keypad *
    [0x38] = 0xE2, // Left Alt
    [0x39] = 0x2C, // Space
    [0x3A] = 0x39, // Caps Lock
    [0x3B] = 0x3A, [0x3C] = 0x3B, [0x3D] = 0x3C, [0x3E] = 0x3D, // F1-F4
    [0x3F] = 0x3E, [0x40] = 0x3F, [0x41] = 0x40, [0x42] = 0x41, // F5-F8
    [0x43] = 0x42, [0x44] = 0x43,                               // F9 F10
    [0x45] = 0x53, // Num Lock (see the vk override for Pause)
    [0x46] = 0x47, // Scroll Lock
    [0x47] = 0x5F, [0x48] = 0x60, [0x49] = 0x61, // Keypad 7 8 9
    [0x4A] = 0x56, // Keypad -
    [0x4B] = 0x5C, [0x4C] = 0x5D, [0x4D] = 0x5E, // Keypad 4 5 6
    [0x4E] = 0x57, // Keypad +
    [0x4F] = 0x59, [0x50] = 0x5A, [0x51] = 0x5B, // Keypad 1 2 3
    [0x52] = 0x62, // Keypad 0
    [0x53] = 0x63, // Keypad .
    [0x56] = 0x64, // ISO key between Left Shift and Z (Non-US \ and |)
    [0x57] = 0x44, // F11
    [0x58] = 0x45, // F12
    [0x70] = 0x88, // International2 (Katakana/Hiragana)
    [0x73] = 0x87, // International1 (ABNT2 "/ ?", JIS Ro)
    [0x79] = 0x8A, // International4 (Convert)
    [0x7B] = 0x8B, // International5 (Non-convert)
    [0x7D] = 0x89, // International3 (Yen)
    [0x7E] = 0x85, // Keypad Comma (ABNT2 numpad ".")
};

// Windows scancodes with the 0xE0 prefix to HID usages
static const struct {
    uint8_t scancode;
    uint8_t hid;
} EXTENDED_TO_HID[] = {
    {0x1C, 0x58}, // Keypad Enter
    {0x1D, 0xE4}, // Right Ctrl
    {0x35, 0x54}, // Keypad /
    {0x37, 0x46}, // Print Screen
    {0x38, 0xE6}, // Right Alt (AltGr)
    {0x46, 0x48}, // Ctrl+Pause ("Break")
    {0x47, 0x4A}, // Home
    {0x48, 0x52}, // Up
    {0x49, 0x4B}, // Page Up
    {0x4B, 0x50}, // Left
    {0x4D, 0x4F}, // Right
    {0x4F, 0x4D}, // End
    {0x50, 0x51}, // Down
    {0x51, 0x4E}, // Page Down
    {0x52, 0x49}, // Insert
    {0x53, 0x4C}, // Delete
    {0x5B, 0xE3}, // Left Win
    {0x5C, 0xE7}, // Right Win
    {0x5D, 0x65}, // Menu
};

// Windows virtual-key codes used for disambiguation
#define VK_CANCEL_ 0x03  // Ctrl+Pause
#define VK_PAUSE_ 0x13
#define VK_NUMLOCK_ 0x90

uint8_t
pe_keymap_from_windows(uint16_t scancode, bool extended, uint16_t vk) {
    // Pause/Break and NumLock share the scancode 0x45, and the extended flag
    // is not reliable for them: trust the virtual-key code.
    if (vk == VK_PAUSE_ || vk == VK_CANCEL_) {
        return PE_SC_PAUSE;
    }
    if (vk == VK_NUMLOCK_) {
        return 0x53;
    }

    if (extended) {
        for (unsigned i = 0;
                i < sizeof(EXTENDED_TO_HID) / sizeof(EXTENDED_TO_HID[0]); ++i) {
            if (EXTENDED_TO_HID[i].scancode == scancode) {
                return EXTENDED_TO_HID[i].hid;
            }
        }
        return 0;
    }

    if (scancode >= sizeof(SET1_TO_HID)) {
        return 0;
    }
    return SET1_TO_HID[scancode];
}

uint8_t
pe_keymap_hid_mod_bit(uint8_t hid) {
    if (hid < 0xE0 || hid > 0xE7) {
        return 0;
    }
    return 1 << (hid - 0xE0);
}

uint8_t
pe_keymap_generic_mods(uint8_t hid_mods) {
    uint8_t mods = 0;
    if (hid_mods & (PE_HID_LCTRL | PE_HID_RCTRL)) {
        mods |= PE_MOD_CTRL;
    }
    if (hid_mods & (PE_HID_LALT | PE_HID_RALT)) {
        mods |= PE_MOD_ALT;
    }
    if (hid_mods & (PE_HID_LSHIFT | PE_HID_RSHIFT)) {
        mods |= PE_MOD_SHIFT;
    }
    if (hid_mods & (PE_HID_LGUI | PE_HID_RGUI)) {
        mods |= PE_MOD_GUI;
    }
    return mods;
}

static const struct {
    const char *name;
    uint8_t hid;
} KEY_NAMES[] = {
    {"pause", PE_SC_PAUSE}, {"break", PE_SC_PAUSE},
    {"scrolllock", 0x47}, {"printscreen", 0x46}, {"prtsc", 0x46},
    {"numlock", 0x53},
    {"insert", 0x49}, {"home", 0x4A}, {"pageup", 0x4B}, {"delete", 0x4C},
    {"end", 0x4D}, {"pagedown", 0x4E},
    {"right", 0x4F}, {"left", 0x50}, {"down", 0x51}, {"up", 0x52},
    {"esc", 0x29}, {"escape", 0x29}, {"enter", 0x28}, {"return", 0x28},
    {"tab", 0x2B}, {"backspace", 0x2A}, {"space", 0x2C},
};

uint8_t
pe_key_from_name(const char *name) {
    char buf[16];
    size_t len = strlen(name);
    if (!len || len >= sizeof(buf)) {
        return 0;
    }
    for (size_t i = 0; i < len; ++i) {
        buf[i] = (char) tolower((unsigned char) name[i]);
    }
    buf[len] = '\0';

    if (len == 1) {
        if (buf[0] >= 'a' && buf[0] <= 'z') {
            return 0x04 + (buf[0] - 'a');
        }
        if (buf[0] >= '1' && buf[0] <= '9') {
            return 0x1E + (buf[0] - '1');
        }
        if (buf[0] == '0') {
            return 0x27;
        }
    }

    if (buf[0] == 'f' && len >= 2 && len <= 3) {
        int n = 0;
        for (size_t i = 1; i < len; ++i) {
            if (buf[i] < '0' || buf[i] > '9') {
                n = -1;
                break;
            }
            n = n * 10 + (buf[i] - '0');
        }
        if (n >= 1 && n <= 12) {
            return 0x3A + (n - 1);
        }
        if (n != -1) {
            return 0;
        }
    }

    for (unsigned i = 0; i < sizeof(KEY_NAMES) / sizeof(KEY_NAMES[0]); ++i) {
        if (!strcmp(KEY_NAMES[i].name, buf)) {
            return KEY_NAMES[i].hid;
        }
    }
    return 0;
}

static void
set_err(char *err, unsigned errlen, const char *msg, const char *detail) {
    if (err && errlen) {
        snprintf(err, errlen, "%s%s%s", msg, detail ? ": " : "",
                 detail ? detail : "");
    }
}

static bool
key_allowed_without_modifier(uint8_t key) {
    // Pause, Scroll Lock, Print Screen and F1-F12 are unlikely to be typed
    return key == PE_SC_PAUSE || key == 0x47 || key == 0x46
        || (key >= 0x3A && key <= 0x45);
}

bool
pe_hotkey_parse(const char *s, struct pe_hotkey *out, char *err,
                unsigned errlen) {
    struct pe_hotkey hk = {0, 0};

    char token[32];
    const char *p = s;
    for (;;) {
        // skip spaces
        while (*p == ' ' || *p == '\t') {
            ++p;
        }
        size_t n = 0;
        while (*p && *p != '+') {
            if (n + 1 >= sizeof(token)) {
                set_err(err, errlen, "hotkey token too long", s);
                return false;
            }
            token[n++] = *p++;
        }
        while (n && (token[n - 1] == ' ' || token[n - 1] == '\t')) {
            --n;
        }
        token[n] = '\0';

        if (!n) {
            set_err(err, errlen, "empty element in hotkey", s);
            return false;
        }

        char lower[32];
        for (size_t i = 0; i <= n; ++i) {
            lower[i] = (char) tolower((unsigned char) token[i]);
        }

        if (!strcmp(lower, "ctrl") || !strcmp(lower, "control")) {
            hk.mods |= PE_MOD_CTRL;
        } else if (!strcmp(lower, "alt")) {
            hk.mods |= PE_MOD_ALT;
        } else if (!strcmp(lower, "shift")) {
            hk.mods |= PE_MOD_SHIFT;
        } else if (!strcmp(lower, "win") || !strcmp(lower, "gui")
                || !strcmp(lower, "super")) {
            hk.mods |= PE_MOD_GUI;
        } else {
            uint8_t key = pe_key_from_name(lower);
            if (!key) {
                set_err(err, errlen, "unknown key in hotkey", token);
                return false;
            }
            if (hk.key) {
                set_err(err, errlen, "hotkey must have a single main key", s);
                return false;
            }
            hk.key = key;
        }

        if (!*p) {
            break;
        }
        ++p; // skip '+'
    }

    if (!hk.key) {
        set_err(err, errlen, "hotkey has no main key", s);
        return false;
    }
    if (!hk.mods && !key_allowed_without_modifier(hk.key)) {
        set_err(err, errlen, "hotkey needs at least one modifier", s);
        return false;
    }

    *out = hk;
    return true;
}

void
pe_hotkey_format(const struct pe_hotkey *hk, char *out, unsigned len) {
    char buf[64] = "";
    if (hk->mods & PE_MOD_CTRL) {
        strcat(buf, "Ctrl+");
    }
    if (hk->mods & PE_MOD_ALT) {
        strcat(buf, "Alt+");
    }
    if (hk->mods & PE_MOD_SHIFT) {
        strcat(buf, "Shift+");
    }
    if (hk->mods & PE_MOD_GUI) {
        strcat(buf, "Win+");
    }
    char key[16];
    if (hk->key == PE_SC_PAUSE) {
        snprintf(key, sizeof(key), "Pause");
    } else if (hk->key >= 0x3A && hk->key <= 0x45) {
        snprintf(key, sizeof(key), "F%d", hk->key - 0x3A + 1);
    } else if (hk->key >= 0x04 && hk->key <= 0x1D) {
        snprintf(key, sizeof(key), "%c", 'A' + (hk->key - 0x04));
    } else {
        snprintf(key, sizeof(key), "0x%02X", hk->key);
    }
    snprintf(out, len, "%s%s", buf, key);
}
