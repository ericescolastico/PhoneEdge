#ifndef PE_SESSION_H
#define PE_SESSION_H

/**
 * Glue between a platform input driver (Windows hooks, or a script) and the
 * portable edge state machine: it serializes the calls, executes the actions
 * through the `pe_phone_ops` (phone side) and `pe_pc_ops` (PC side), and
 * exposes a thread-safe "phone active" flag for the hooks.
 *
 * Every pe_session_on_*() function returns whether the original input event
 * must be swallowed (hidden from the PC).
 */

#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>

#include "edge/pe_edge.h"
#include "util/thread.h"

// Phone side: to inject input on the phone. Called from one thread at a time.
struct pe_phone_ops {
    void (*move)(void *ud, int dx, int dy, bool placement,
                 unsigned delay_ms);
    void (*buttons)(void *ud, uint8_t buttons);
    void (*wheel)(void *ud, float horizontal, float vertical);
    void (*key)(void *ud, uint8_t scancode, bool down, uint8_t hid_mods);
};

// PC side
struct pe_pc_ops {
    // Place the PC cursor (virtual desktop coordinates)
    void (*set_cursor)(void *ud, int x, int y);
};

struct pe_session {
    struct pe_edge edge;
    struct pe_result res; // scratch, protected by the mutex
    sc_mutex mutex;

    struct pe_phone_ops phone;
    void *phone_ud;
    struct pe_pc_ops pc;
    void *pc_ud;

    // Mirror of the state, readable from any thread without locking
    atomic_bool phone_active;
    atomic_bool disabled;

    // Statistics, for the debug log
    unsigned long entries;
    unsigned long leaves;
};

bool
pe_session_init(struct pe_session *s, const struct pe_config *cfg,
                const struct pe_phone_ops *phone, void *phone_ud,
                const struct pe_pc_ops *pc, void *pc_ud);

void
pe_session_destroy(struct pe_session *s);

void
pe_session_set_monitors(struct pe_session *s, const struct pe_rect *rects,
                        int count, int selected);

void
pe_session_set_phone_size(struct pe_session *s, int width, int height);

bool
pe_session_on_mouse_move(struct pe_session *s, uint64_t now_ms, int dx, int dy,
                         int cursor_x, int cursor_y);

bool
pe_session_on_button(struct pe_session *s, uint64_t now_ms, uint8_t button,
                     bool down);

bool
pe_session_on_wheel(struct pe_session *s, uint64_t now_ms, bool horizontal,
                    int delta);

bool
pe_session_on_key(struct pe_session *s, uint64_t now_ms, uint8_t scancode,
                  bool down);

void
pe_session_on_tick(struct pe_session *s, uint64_t now_ms);

// The phone is gone: the PC keeps the control for good
void
pe_session_disconnect(struct pe_session *s, uint64_t now_ms);

// The application is exiting: give the control back to the PC, releasing what
// is held on the phone
void
pe_session_release(struct pe_session *s, uint64_t now_ms);

// Thread-safe, lock-free. True while the input must be redirected to the phone.
static inline bool
pe_session_is_phone_active(struct pe_session *s) {
    return atomic_load(&s->phone_active);
}

#endif
