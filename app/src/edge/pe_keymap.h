#ifndef PE_KEYMAP_H
#define PE_KEYMAP_H

/**
 * Keyboard helpers, portable (no Win32, no SDL).
 *
 * Scancodes are USB HID usage ids (keyboard page), the same values as
 * SDL_Scancode and sc_scancode.
 */

#include <stdbool.h>
#include <stdint.h>

// Generic modifier mask (left or right variants are not distinguished)
#define PE_MOD_CTRL  (1 << 0)
#define PE_MOD_ALT   (1 << 1)
#define PE_MOD_SHIFT (1 << 2)
#define PE_MOD_GUI   (1 << 3)

// HID modifier byte bits (scancodes 0xE0..0xE7 map to bits 0..7)
#define PE_HID_LCTRL  (1 << 0)
#define PE_HID_LSHIFT (1 << 1)
#define PE_HID_LALT   (1 << 2)
#define PE_HID_LGUI   (1 << 3)
#define PE_HID_RCTRL  (1 << 4)
#define PE_HID_RSHIFT (1 << 5)
#define PE_HID_RALT   (1 << 6)
#define PE_HID_RGUI   (1 << 7)

#define PE_SC_PAUSE 0x48

struct pe_hotkey {
    uint8_t mods; // PE_MOD_* mask, all of them must be held
    uint8_t key;  // HID scancode of the main key (never a modifier)
};

/**
 * Convert a Windows keyboard event to an USB HID scancode.
 *
 * - `scancode` is the hardware scancode (set 1) of the event, without the
 *   0xE0 prefix;
 * - `extended` is true if the key has the 0xE0 prefix (LLKHF_EXTENDED);
 * - `vk` is the Windows virtual-key code (0 if unknown). It is only used to
 *   disambiguate Pause/Break and NumLock, which share the scancode 0x45.
 *
 * Return 0 if the key is not supported.
 */
uint8_t
pe_keymap_from_windows(uint16_t scancode, bool extended, uint16_t vk);

// Return the bit of the HID modifier byte for a modifier scancode, 0 otherwise
uint8_t
pe_keymap_hid_mod_bit(uint8_t hid);

// Convert a HID modifier byte to a generic PE_MOD_* mask
uint8_t
pe_keymap_generic_mods(uint8_t hid_mods);

// Parse a key name ("pause", "f12", "a", "5", ...), case insensitive.
// Return the HID scancode, or 0 if unknown.
uint8_t
pe_key_from_name(const char *name);

// Parse a hotkey like "ctrl+alt+pause". On error, `err` receives a message.
bool
pe_hotkey_parse(const char *s, struct pe_hotkey *out, char *err,
                unsigned errlen);

// Format a hotkey ("Ctrl+Alt+Pause"), for logs
void
pe_hotkey_format(const struct pe_hotkey *hk, char *out, unsigned len);

#endif
