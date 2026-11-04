#ifndef PE_CONFIG_H
#define PE_CONFIG_H

/**
 * PhoneEdge settings (edge handoff), portable. Shared by the command line and
 * the .ini file.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "edge/pe_keymap.h"

// The PC monitor edge (or the phone edge, see pe_edge.h) a pointer crosses
enum pe_side {
    PE_SIDE_LEFT,
    PE_SIDE_RIGHT,
    PE_SIDE_TOP,
    PE_SIDE_BOTTOM,
};

struct pe_config {
    // The edge of the PC monitor on which the phone is located
    enum pe_side edge;
    // Index of the monitor (0-based, in the order of --list-monitors)
    int monitor_index;

    // Phone screen size, in pixels, in its natural (rotation 0) orientation
    bool phone_size_auto;      // read with "adb shell wm size"
    int phone_width;           // used if !phone_size_auto, and as fallback
    int phone_height;
    int phone_rotation;        // 0, 90, 180 or 270 (clockwise)

    // Estimated phone pixels moved by the pointer for 1 unit of the HID
    // report. Used to estimate the pointer position (drift: calibrate it).
    float pointer_scale;
    // HID units sent per raw mouse count of the PC
    float sensitivity;
    // Multiplier applied to the wheel
    float wheel_scale;

    // Emergency shortcut, always brings the control back to the PC
    struct pe_hotkey hotkey;

    // Raw mouse counts to push against the edge to cross it
    int edge_push;
    // Pixels of the phone to push against the opposite edge to come back
    int leave_margin;
    // Distance (phone pixels) from the phone edge where the pointer appears
    int entry_inset;
    // Distance (PC pixels) from the monitor edge where the PC cursor reappears
    int return_inset;
    // Minimum delay between 2 HID motion reports
    int flush_ms;
    // Time after entering during which the pointer cannot come back
    int enter_guard_ms;
    // Time after coming back during which the pointer cannot cross again
    int reenter_ms;
    // Extra reports sent when anchoring the pointer to a corner
    int anchor_extra;
    // Delay between the reports of the anchoring and positioning sequence
    int place_delay_ms;
    // Also confine the PC cursor to a pixel while the phone has the control
    // (plan B if the hooks do not freeze the cursor on a given system)
    bool clip_cursor;
};

void
pe_config_defaults(struct pe_config *cfg);

enum pe_set_result {
    PE_SET_OK,
    PE_SET_UNKNOWN_KEY, // not an edge setting (let the caller handle it)
    PE_SET_INVALID_VALUE,
};

/**
 * Set a setting from its name and textual value (the same names are used in
 * the .ini file and, prefixed by "--" with '-' instead of '_', on the command
 * line). On INVALID_VALUE, `err` receives a message.
 */
enum pe_set_result
pe_config_set(struct pe_config *cfg, const char *key, const char *value,
              char *err, unsigned errlen);

/**
 * Parse a .ini file content: "key = value" lines, comments starting with
 * ';' or '#', optional "[section]" lines (ignored). Keys are case
 * insensitive and '-' is the same as '_'.
 *
 * For each entry, `cb` is called; it returns false to abort (with a message in
 * `err`). Return false on a syntax error or when aborted, `err` receives a
 * message prefixed with "line N: ".
 */
typedef bool (*pe_ini_cb)(void *userdata, const char *key, const char *value,
                          char *err, unsigned errlen);

bool
pe_ini_parse(const char *text, pe_ini_cb cb, void *userdata, char *err,
             unsigned errlen);

// Parse yes/no, true/false, on/off, 1/0 (case insensitive)
bool
pe_parse_bool(const char *s, bool *out);

// Size of the phone after applying the rotation
void
pe_config_oriented_size(const struct pe_config *cfg, int *width, int *height);

// Return true if the whole config is consistent
bool
pe_config_validate(const struct pe_config *cfg, char *err, unsigned errlen);

const char *
pe_side_name(enum pe_side side);

enum pe_side
pe_side_opposite(enum pe_side side);

#endif
