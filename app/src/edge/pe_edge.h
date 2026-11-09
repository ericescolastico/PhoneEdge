#ifndef PE_EDGE_H
#define PE_EDGE_H

/**
 * Edge handoff state machine, portable C (no Win32, no SDL, no I/O).
 *
 * The platform layer feeds it with input events (raw mouse deltas, cursor
 * position, buttons, wheel, keys, disconnection) and a monotonic clock in
 * milliseconds, and it answers with:
 *  - `swallow`: whether the platform must hide the original event from the PC;
 *  - a list of actions to execute (inject input on the phone, move the PC
 *    cursor back...).
 *
 * States:
 *  - PE_STATE_PC_ACTIVE: the PC has the control. Mouse motion is watched to
 *    detect the push against the configured edge of the selected monitor.
 *  - PE_STATE_PHONE_ACTIVE: all mouse/keyboard events are redirected to the
 *    phone (swallowed on the PC) until the pointer comes back by the opposite
 *    edge of the phone, or the emergency hotkey is pressed.
 *  - PE_STATE_DISABLED: the phone is gone (disconnection). The PC keeps the
 *    control for good.
 *
 * Not thread-safe: the platform must serialize the calls.
 */

#include <stdbool.h>
#include <stdint.h>

#include "edge/pe_config.h"
#include "edge/pe_keymap.h"
#include "edge/pe_vpos.h"

#define PE_MAX_MONITORS 16
// Worst case of an entry, with the limits accepted by pe_config (phone up to
// 10000 px, pointer_scale >= 0.25): ENTER + at most PE_MAX_ANCHOR_REPORTS
// anchoring reports + ceil(10000 / 0.25 / 127) = 315 positioning reports
#define PE_MAX_ANCHOR_REPORTS 300
#define PE_MAX_ACTIONS 640

// Mouse buttons bit mask (same order as the HID report)
#define PE_BTN_LEFT   (1 << 0)
#define PE_BTN_RIGHT  (1 << 1)
#define PE_BTN_MIDDLE (1 << 2)
#define PE_BTN_X1     (1 << 3)
#define PE_BTN_X2     (1 << 4)

struct pe_rect {
    // Windows RECT convention: right and bottom are exclusive
    int32_t left;
    int32_t top;
    int32_t right;
    int32_t bottom;
};

enum pe_state {
    PE_STATE_PC_ACTIVE,
    PE_STATE_PHONE_ACTIVE,
    PE_STATE_DISABLED,
};

enum pe_leave_reason {
    PE_LEAVE_EDGE,       // the pointer came back through the phone edge
    PE_LEAVE_HOTKEY,     // emergency shortcut
    PE_LEAVE_DISCONNECT, // the phone disconnected (nothing can be sent to it)
    PE_LEAVE_RELEASE,    // forced by the application (exit, signal, error)
};

enum pe_action_type {
    // Start redirecting. `enter.ratio` is the position along the edge (0..1).
    // Followed by the anchoring and positioning moves (flagged `placement`).
    PE_ACT_ENTER_PHONE,
    // Send a relative motion to the phone (|dx|, |dy| <= 127)
    PE_ACT_PHONE_MOVE,
    // Send the new state of the buttons of the phone mouse
    PE_ACT_PHONE_BUTTONS,
    // Send a scroll (in wheel notches, positive is up/right)
    PE_ACT_PHONE_WHEEL,
    // Send a key press/release
    PE_ACT_PHONE_KEY,
    // Stop redirecting; for the PC, place the cursor at (pc_x, pc_y) if
    // `set_cursor`
    PE_ACT_LEAVE_PHONE,
};

struct pe_action {
    enum pe_action_type type;
    union {
        struct {
            float ratio;
        } enter;
        struct {
            int8_t dx;
            int8_t dy;
            // part of the anchoring/positioning sequence after an entry
            bool placement;
            // advised delay before the next action of the sequence
            uint16_t delay_ms;
        } move;
        struct {
            uint8_t buttons;
        } buttons;
        struct {
            float horizontal;
            float vertical;
        } wheel;
        struct {
            uint8_t scancode;
            bool down;
            uint8_t mods; // HID modifier byte, after this event
        } key;
        struct {
            enum pe_leave_reason reason;
            bool set_cursor;
            float ratio;
            int32_t pc_x;
            int32_t pc_y;
        } leave;
    };
};

struct pe_result {
    bool swallow;
    unsigned count;
    struct pe_action actions[PE_MAX_ACTIONS];
};

struct pe_edge {
    struct pe_config cfg;
    enum pe_state state;

    struct pe_rect monitors[PE_MAX_MONITORS];
    int monitor_count;
    int selected;

    // Phone
    struct pe_vpos vpos;
    uint64_t enter_time;
    uint64_t leave_time;
    bool has_left; // leave_time is valid
    float leave_push; // consecutive px pushed against the opposite edge
    bool anchored;    // vpos was anchored to a corner at anchor_time
    uint64_t anchor_time;
    int entries_since_anchor;

    // PC side
    float edge_push_acc;
    uint8_t pc_buttons;
    bool pc_keys[256]; // physical state of the keyboard
    // Keys whose press reached Windows and whose release did not yet: their
    // release must always reach it too, or Windows would see a stuck key
    bool win_down[256];
    // Keys whose press was swallowed (never seen by Windows) and that are
    // still held: their auto-repeats and release must not reach Windows
    bool swallowed_until_up[256];
    uint8_t pc_hid_mods;

    // Phone side
    uint8_t phone_buttons;
    bool phone_keys[256];
    uint8_t phone_hid_mods;
    // Ctrl+Alt translated to AltGr (right Alt) on the phone: the physical
    // Ctrl/Alt keys held meanwhile are not sent
    bool altgr_active;
    bool altgr_hidden[256];
    bool hotkey_key_down; // the key of the hotkey is down (swallow its up)

    // Pending motion, not yet sent (HID units), flushed every flush_ms
    float residual_x;
    float residual_y;
    int pending_dx;
    int pending_dy;
    uint64_t last_flush;
};

void
pe_edge_init(struct pe_edge *e, const struct pe_config *cfg);

// Declare the monitors (virtual desktop coordinates) and the selected one.
// To call at startup, and when the display configuration changes.
void
pe_edge_set_monitors(struct pe_edge *e, const struct pe_rect *rects, int count,
                     int selected);

// Set the phone screen size, after rotation
void
pe_edge_set_phone_size(struct pe_edge *e, int width, int height);

enum pe_state
pe_edge_state(const struct pe_edge *e);

static inline bool
pe_edge_is_phone_active(const struct pe_edge *e) {
    return e->state == PE_STATE_PHONE_ACTIVE;
}

// Estimated position of the phone pointer
void
pe_edge_get_vpos(const struct pe_edge *e, float *x, float *y);

// Relative mouse motion: raw deltas (counts) + the cursor position on the
// virtual desktop
void
pe_edge_on_mouse_move(struct pe_edge *e, uint64_t now_ms, int dx, int dy,
                      int cursor_x, int cursor_y, struct pe_result *res);

void
pe_edge_on_button(struct pe_edge *e, uint64_t now_ms, uint8_t button,
                  bool down, struct pe_result *res);

// `delta` in units of 120 per notch (WHEEL_DELTA)
void
pe_edge_on_wheel(struct pe_edge *e, uint64_t now_ms, bool horizontal,
                 int delta, struct pe_result *res);

// `scancode` is a HID usage. Key repeats are ignored here. A key without HID
// usage (scancode 0: media keys...) is never handled: it is left to the PC,
// whatever the state, so that its release can never be lost.
void
pe_edge_on_key(struct pe_edge *e, uint64_t now_ms, uint8_t scancode, bool down,
               struct pe_result *res);

// To call regularly (every few ms) while the phone is active, to flush the
// pending motion even if the mouse stops.
void
pe_edge_on_tick(struct pe_edge *e, uint64_t now_ms, struct pe_result *res);

// The phone is no longer reachable: the PC gets the control back and keeps it
void
pe_edge_on_disconnect(struct pe_edge *e, uint64_t now_ms,
                      struct pe_result *res);

// Force the control back to the PC (exit, signal, error). Unlike a disconnect,
// the phone is still reachable, so held keys/buttons are released.
void
pe_edge_release(struct pe_edge *e, uint64_t now_ms, struct pe_result *res);

#endif
