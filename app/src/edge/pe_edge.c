#include "pe_edge.h"

#include <math.h>
#include <string.h>

// The cursor is considered "on the edge" when it is at most this many pixels
// from the border (Windows clamps it to the last pixel)
#define PE_EDGE_ZONE 1

// Largest motion (HID units) flushed at once: bounds the number of reports
#define PE_MAX_PENDING (127 * 32)

// Largest chunk of a HID motion report
#define PE_HID_MAX 127

static void
res_init(struct pe_result *res) {
    res->swallow = false;
    res->count = 0;
}

static struct pe_action *
res_push(struct pe_result *res, enum pe_action_type type) {
    if (res->count >= PE_MAX_ACTIONS) {
        // Cannot happen with the bounds above; drop rather than overflow
        return NULL;
    }
    struct pe_action *a = &res->actions[res->count++];
    memset(a, 0, sizeof(*a));
    a->type = type;
    return a;
}

// Milliseconds elapsed since `then`, 0 if the clock went backwards: a
// timestamp older than a previous one must never wrap around
static uint64_t
elapsed_ms(uint64_t now, uint64_t then) {
    return now >= then ? now - then : 0;
}

static float
clampf(float v, float lo, float hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

#define MIN_INT(a, b) ((a) < (b) ? (a) : (b))
#define MAX_INT(a, b) ((a) > (b) ? (a) : (b))

static int
clampi(int v, int lo, int hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

void
pe_edge_init(struct pe_edge *e, const struct pe_config *cfg) {
    memset(e, 0, sizeof(*e));
    e->cfg = *cfg;
    e->state = PE_STATE_PC_ACTIVE;
    e->selected = -1;
    int w, h;
    pe_config_oriented_size(cfg, &w, &h);
    pe_vpos_init(&e->vpos, w, h);
}

void
pe_edge_set_monitors(struct pe_edge *e, const struct pe_rect *rects, int count,
                     int selected) {
    if (count > PE_MAX_MONITORS) {
        count = PE_MAX_MONITORS;
    }
    if (count < 0) {
        count = 0;
    }
    memcpy(e->monitors, rects, count * sizeof(*rects));
    e->monitor_count = count;
    e->selected = (selected >= 0 && selected < count) ? selected : -1;
}

void
pe_edge_set_phone_size(struct pe_edge *e, int width, int height) {
    pe_vpos_init(&e->vpos, width, height);
}

enum pe_state
pe_edge_state(const struct pe_edge *e) {
    return e->state;
}

void
pe_edge_get_vpos(const struct pe_edge *e, float *x, float *y) {
    *x = e->vpos.x;
    *y = e->vpos.y;
}

static bool
rect_contains(const struct pe_rect *r, int x, int y) {
    return x >= r->left && x < r->right && y >= r->top && y < r->bottom;
}

static bool
point_in_other_monitor(const struct pe_edge *e, int x, int y) {
    for (int i = 0; i < e->monitor_count; ++i) {
        if (i != e->selected && rect_contains(&e->monitors[i], x, y)) {
            return true;
        }
    }
    return false;
}

// Is (x, y) on the exposed edge of the selected monitor (no other monitor
// right behind it)? If so, set *ratio to the position along the edge (0..1).
static bool
on_exposed_edge(const struct pe_edge *e, int x, int y, float *ratio) {
    if (e->selected < 0) {
        return false;
    }
    const struct pe_rect *r = &e->monitors[e->selected];
    if (!rect_contains(r, x, y)) {
        return false;
    }

    bool on_edge;
    int out_x = x;
    int out_y = y;
    switch (e->cfg.edge) {
        case PE_SIDE_RIGHT:
            on_edge = x >= r->right - 1 - PE_EDGE_ZONE;
            out_x = r->right;
            break;
        case PE_SIDE_LEFT:
            on_edge = x <= r->left + PE_EDGE_ZONE;
            out_x = r->left - 1;
            break;
        case PE_SIDE_TOP:
            on_edge = y <= r->top + PE_EDGE_ZONE;
            out_y = r->top - 1;
            break;
        case PE_SIDE_BOTTOM:
        default:
            on_edge = y >= r->bottom - 1 - PE_EDGE_ZONE;
            out_y = r->bottom;
            break;
    }
    if (!on_edge || point_in_other_monitor(e, out_x, out_y)) {
        return false;
    }

    float len;
    float pos;
    if (e->cfg.edge == PE_SIDE_LEFT || e->cfg.edge == PE_SIDE_RIGHT) {
        len = (float) (r->bottom - 1 - r->top);
        pos = (float) (y - r->top);
    } else {
        len = (float) (r->right - 1 - r->left);
        pos = (float) (x - r->left);
    }
    *ratio = len > 0 ? clampf(pos / len, 0, 1) : 0.5f;
    return true;
}

// Component of the motion that pushes toward the configured PC edge
static int
outward(const struct pe_edge *e, int dx, int dy) {
    switch (e->cfg.edge) {
        case PE_SIDE_RIGHT: return dx;
        case PE_SIDE_LEFT: return -dx;
        case PE_SIDE_TOP: return -dy;
        case PE_SIDE_BOTTOM:
        default: return dy;
    }
}

static uint8_t
phone_mods(const struct pe_edge *e) {
    return e->phone_hid_mods;
}

// Send HID motion (|dx|, |dy| unbounded) as reports of at most 127 per axis,
// and update the estimated position. If `watch_leave`, accumulate the push
// against the phone edge facing the PC.
static void
emit_motion(struct pe_edge *e, struct pe_result *res, int dx, int dy,
            bool placement, uint16_t delay_ms, bool watch_leave) {
    int ax = dx < 0 ? -dx : dx;
    int ay = dy < 0 ? -dy : dy;
    int n = MAX_INT((ax + PE_HID_MAX - 1) / PE_HID_MAX,
                    (ay + PE_HID_MAX - 1) / PE_HID_MAX);
    enum pe_side phone_side = pe_side_opposite(e->cfg.edge);

    for (int i = 0; i < n; ++i) {
        int cx = clampi(dx, -PE_HID_MAX, PE_HID_MAX);
        int cy = clampi(dy, -PE_HID_MAX, PE_HID_MAX);
        dx -= cx;
        dy -= cy;

        struct pe_action *a = res_push(res, PE_ACT_PHONE_MOVE);
        if (!a) {
            return;
        }
        a->move.dx = (int8_t) cx;
        a->move.dy = (int8_t) cy;
        a->move.placement = placement;
        a->move.delay_ms = delay_ms;

        float cutx, cuty;
        pe_vpos_move(&e->vpos, cx * e->cfg.pointer_scale,
                     cy * e->cfg.pointer_scale, &cutx, &cuty);

        if (!watch_leave) {
            continue;
        }

        // Push against the edge facing the PC (the edge opposite to the PC
        // edge that was crossed)
        float toward; // motion toward the leaving edge, > 0
        float cut;    // part of it that was cut by the screen limit
        switch (phone_side) {
            case PE_SIDE_LEFT:
                toward = (float) -cx; cut = cutx < 0 ? -cutx : 0; break;
            case PE_SIDE_RIGHT:
                toward = (float) cx; cut = cutx > 0 ? cutx : 0; break;
            case PE_SIDE_TOP:
                toward = (float) -cy; cut = cuty < 0 ? -cuty : 0; break;
            case PE_SIDE_BOTTOM:
            default:
                toward = (float) cy; cut = cuty > 0 ? cuty : 0; break;
        }
        if (toward < 0) {
            e->leave_push = 0; // moving away from the edge
        } else if (cut > 0) {
            e->leave_push += cut;
        }
    }
}

static void
flush_pending(struct pe_edge *e, uint64_t now, struct pe_result *res,
              bool watch_leave) {
    if (!e->pending_dx && !e->pending_dy) {
        return;
    }
    int dx = e->pending_dx;
    int dy = e->pending_dy;
    e->pending_dx = 0;
    e->pending_dy = 0;
    e->last_flush = now;
    emit_motion(e, res, dx, dy, false, 0, watch_leave);
}

static void
compute_pc_point(const struct pe_edge *e, float ratio, int32_t *x,
                 int32_t *y) {
    const struct pe_rect *r = &e->monitors[e->selected];
    int w = r->right - 1 - r->left;
    int h = r->bottom - 1 - r->top;
    // Reappear clearly away from the edge zone, or the cursor would cross
    // again at once, and always inside the monitor
    int inset = MAX_INT(e->cfg.return_inset, PE_EDGE_ZONE + 1);
    inset = MIN_INT(inset, MIN_INT(w, h) / 2);
    int along_x = r->left + (int) lroundf(ratio * (float) w);
    int along_y = r->top + (int) lroundf(ratio * (float) h);
    switch (e->cfg.edge) {
        case PE_SIDE_RIGHT:
            *x = r->right - 1 - inset;
            *y = along_y;
            break;
        case PE_SIDE_LEFT:
            *x = r->left + inset;
            *y = along_y;
            break;
        case PE_SIDE_TOP:
            *x = along_x;
            *y = r->top + inset;
            break;
        case PE_SIDE_BOTTOM:
        default:
            *x = along_x;
            *y = r->bottom - 1 - inset;
            break;
    }
}

// Release everything the phone considers pressed
static void
release_phone_inputs(struct pe_edge *e, struct pe_result *res) {
    if (e->phone_buttons) {
        e->phone_buttons = 0;
        struct pe_action *a = res_push(res, PE_ACT_PHONE_BUTTONS);
        if (a) {
            a->buttons.buttons = 0;
        }
    }
    for (int k = 255; k >= 0; --k) {
        // Modifiers (0xE0..0xE7) last, so that the mods byte stays coherent
        if (!e->phone_keys[k]) {
            continue;
        }
        e->phone_keys[k] = false;
        uint8_t bit = pe_keymap_hid_mod_bit((uint8_t) k);
        if (bit) {
            e->phone_hid_mods &= (uint8_t) ~bit;
        }
        struct pe_action *a = res_push(res, PE_ACT_PHONE_KEY);
        if (a) {
            a->key.scancode = (uint8_t) k;
            a->key.down = false;
            a->key.mods = phone_mods(e);
        }
    }
}

static void
leave_phone(struct pe_edge *e, uint64_t now, enum pe_leave_reason reason,
            struct pe_result *res) {
    bool can_send = reason != PE_LEAVE_DISCONNECT;
    if (can_send) {
        flush_pending(e, now, res, false);
        release_phone_inputs(e, res);
    } else {
        // Nothing can reach the phone anymore
        e->pending_dx = e->pending_dy = 0;
        e->phone_buttons = 0;
        memset(e->phone_keys, 0, sizeof(e->phone_keys));
        e->phone_hid_mods = 0;
    }

    // Ratio along the edge, from the estimated position
    float ratio;
    enum pe_side edge = e->cfg.edge;
    if (edge == PE_SIDE_LEFT || edge == PE_SIDE_RIGHT) {
        float len = (float) (e->vpos.height - 1);
        ratio = len > 0 ? e->vpos.y / len : 0.5f;
    } else {
        float len = (float) (e->vpos.width - 1);
        ratio = len > 0 ? e->vpos.x / len : 0.5f;
    }
    ratio = clampf(ratio, 0, 1);

    struct pe_action *a = res_push(res, PE_ACT_LEAVE_PHONE);
    if (a) {
        a->leave.reason = reason;
        a->leave.ratio = ratio;
        a->leave.set_cursor = e->selected >= 0;
        if (a->leave.set_cursor) {
            compute_pc_point(e, ratio, &a->leave.pc_x, &a->leave.pc_y);
        }
    }

    e->state = PE_STATE_PC_ACTIVE;
    e->leave_time = now;
    e->has_left = true;
    e->edge_push_acc = 0;
    e->leave_push = 0;
    e->residual_x = e->residual_y = 0;
    res->swallow = true;
}

static void
enter_phone(struct pe_edge *e, uint64_t now, float ratio,
            struct pe_result *res) {
    const struct pe_config *cfg = &e->cfg;

    e->state = PE_STATE_PHONE_ACTIVE;
    e->enter_time = now;
    e->leave_push = 0;
    e->edge_push_acc = 0;
    e->residual_x = e->residual_y = 0;
    e->pending_dx = e->pending_dy = 0;
    e->last_flush = now;
    e->phone_buttons = 0;
    memset(e->phone_keys, 0, sizeof(e->phone_keys));
    e->phone_hid_mods = 0;
    e->hotkey_key_down = false;
    // (the keys Windows saw going down are tracked in `win_down`)

    res->swallow = true;
    struct pe_action *a = res_push(res, PE_ACT_ENTER_PHONE);
    if (a) {
        a->enter.ratio = ratio;
    }

    // Anchor the pointer to the corner of the phone edge closest to the entry
    // point, then move it to the entry point.
    int w = e->vpos.width;
    int h = e->vpos.height;
    enum pe_side phone_side = pe_side_opposite(cfg->edge);
    int inset_x = MIN_INT(cfg->entry_inset, (w - 1) / 2);
    int inset_y = MIN_INT(cfg->entry_inset, (h - 1) / 2);

    float tx, ty; // target
    float cx, cy; // corner
    switch (phone_side) {
        case PE_SIDE_LEFT:
            tx = (float) inset_x;
            ty = ratio * (float) (h - 1);
            cx = 0;
            cy = ratio < 0.5f ? 0 : (float) (h - 1);
            break;
        case PE_SIDE_RIGHT:
            tx = (float) (w - 1 - inset_x);
            ty = ratio * (float) (h - 1);
            cx = (float) (w - 1);
            cy = ratio < 0.5f ? 0 : (float) (h - 1);
            break;
        case PE_SIDE_TOP:
            tx = ratio * (float) (w - 1);
            ty = (float) inset_y;
            cx = ratio < 0.5f ? 0 : (float) (w - 1);
            cy = 0;
            break;
        case PE_SIDE_BOTTOM:
        default:
            tx = ratio * (float) (w - 1);
            ty = (float) (h - 1 - inset_y);
            cx = ratio < 0.5f ? 0 : (float) (w - 1);
            cy = (float) (h - 1);
            break;
    }

    // Anchoring: enough full-scale reports to cross the whole screen even
    // if Android moves the pointer less than expected; the pointer stops at
    // the corner.
    int longest = MAX_INT(w, h);
    int reports = (int) ceilf((float) longest
                              / ((float) PE_HID_MAX * cfg->pointer_scale))
                + cfg->anchor_extra;
    reports = MIN_INT(reports, PE_MAX_ANCHOR_REPORTS);
    int sx = cx == 0 ? -PE_HID_MAX : PE_HID_MAX;
    int sy = cy == 0 ? -PE_HID_MAX : PE_HID_MAX;
    for (int i = 0; i < reports; ++i) {
        struct pe_action *m = res_push(res, PE_ACT_PHONE_MOVE);
        if (!m) {
            break;
        }
        m->move.dx = (int8_t) sx;
        m->move.dy = (int8_t) sy;
        m->move.placement = true;
        m->move.delay_ms = (uint16_t) cfg->place_delay_ms;
    }
    pe_vpos_set(&e->vpos, cx, cy);

    // Positioning, in HID units
    int ux = (int) lroundf((tx - cx) / cfg->pointer_scale);
    int uy = (int) lroundf((ty - cy) / cfg->pointer_scale);
    emit_motion(e, res, ux, uy, true, (uint16_t) cfg->place_delay_ms, false);
}

static bool
may_enter(const struct pe_edge *e, uint64_t now) {
    if (e->state != PE_STATE_PC_ACTIVE || e->selected < 0) {
        return false;
    }
    if (e->vpos.width < 1 || e->vpos.height < 1) {
        return false;
    }
    if (e->pc_buttons) {
        return false;
    }
    if (e->has_left
            && elapsed_ms(now, e->leave_time) < (uint64_t) e->cfg.reenter_ms) {
        return false;
    }
    return true;
}

static void
tick_phone(struct pe_edge *e, uint64_t now, struct pe_result *res);

void
pe_edge_on_mouse_move(struct pe_edge *e, uint64_t now, int dx, int dy,
                      int cursor_x, int cursor_y, struct pe_result *res) {
    res_init(res);

    if (e->state == PE_STATE_DISABLED) {
        return;
    }

    if (e->state == PE_STATE_PC_ACTIVE) {
        float ratio;
        if (!on_exposed_edge(e, cursor_x, cursor_y, &ratio)) {
            e->edge_push_acc = 0;
            return;
        }
        int out = outward(e, dx, dy);
        if (out < 0) {
            e->edge_push_acc = 0; // coming back from the edge
            return;
        }
        if (out == 0) {
            return;
        }
        if (!may_enter(e, now)) {
            // Entry is blocked (button held, cooldown...): the push must be
            // rebuilt after that, or a tiny nudge would cross later
            e->edge_push_acc = 0;
            return;
        }
        e->edge_push_acc += (float) out;
        if (e->edge_push_acc >= (float) e->cfg.edge_push) {
            enter_phone(e, now, ratio, res);
        }
        return;
    }

    // PE_STATE_PHONE_ACTIVE
    res->swallow = true;

    float fx = (float) dx * e->cfg.sensitivity + e->residual_x;
    float fy = (float) dy * e->cfg.sensitivity + e->residual_y;
    int ix = (int) truncf(fx);
    int iy = (int) truncf(fy);
    e->residual_x = fx - (float) ix;
    e->residual_y = fy - (float) iy;
    e->pending_dx = clampi(e->pending_dx + ix, -PE_MAX_PENDING, PE_MAX_PENDING);
    e->pending_dy = clampi(e->pending_dy + iy, -PE_MAX_PENDING, PE_MAX_PENDING);

    tick_phone(e, now, res);
}

// Flush the pending motion if it is time, and come back to the PC if the
// pointer pushed enough against the edge facing it. Does not reset `res`.
static void
tick_phone(struct pe_edge *e, uint64_t now, struct pe_result *res) {
    if (elapsed_ms(now, e->last_flush) < (uint64_t) e->cfg.flush_ms) {
        return;
    }
    flush_pending(e, now, res, true);

    bool guard_over =
        elapsed_ms(now, e->enter_time) >= (uint64_t) e->cfg.enter_guard_ms;
    if (guard_over && e->leave_push > 0
            && e->leave_push >= (float) e->cfg.leave_margin) {
        leave_phone(e, now, PE_LEAVE_EDGE, res);
    }
}

void
pe_edge_on_tick(struct pe_edge *e, uint64_t now, struct pe_result *res) {
    res_init(res);

    if (e->state != PE_STATE_PHONE_ACTIVE) {
        return;
    }
    res->swallow = true;
    tick_phone(e, now, res);
}

void
pe_edge_on_button(struct pe_edge *e, uint64_t now, uint8_t button, bool down,
                  struct pe_result *res) {
    res_init(res);

    if (e->state == PE_STATE_DISABLED) {
        return;
    }
    if (e->state == PE_STATE_PC_ACTIVE) {
        if (down) {
            e->pc_buttons |= button;
        } else {
            e->pc_buttons &= (uint8_t) ~button;
        }
        return;
    }

    res->swallow = true;
    uint8_t before = e->phone_buttons;
    if (down) {
        e->phone_buttons |= button;
    } else {
        e->phone_buttons &= (uint8_t) ~button;
    }
    if (e->phone_buttons == before) {
        return;
    }

    // Keep the order: the pointer must be at the right place for the click
    flush_pending(e, now, res, true);
    struct pe_action *a = res_push(res, PE_ACT_PHONE_BUTTONS);
    if (a) {
        a->buttons.buttons = e->phone_buttons;
    }
}

void
pe_edge_on_wheel(struct pe_edge *e, uint64_t now, bool horizontal, int delta,
                 struct pe_result *res) {
    res_init(res);

    if (e->state != PE_STATE_PHONE_ACTIVE) {
        return;
    }

    res->swallow = true;
    // Keep the order: scroll where the pointer is
    flush_pending(e, now, res, true);
    float notches = (float) delta / 120.0f * e->cfg.wheel_scale;
    struct pe_action *a = res_push(res, PE_ACT_PHONE_WHEEL);
    if (a) {
        a->wheel.horizontal = horizontal ? notches : 0;
        a->wheel.vertical = horizontal ? 0 : notches;
    }
}

static uint8_t
phys_hid_mods(const struct pe_edge *e) {
    uint8_t mods = 0;
    for (int k = 0xE0; k <= 0xE7; ++k) {
        if (e->pc_keys[k]) {
            mods |= pe_keymap_hid_mod_bit((uint8_t) k);
        }
    }
    return mods;
}

// The core of pe_edge_on_key(): decide about one key event. The caller records
// what Windows will see.
static void
handle_key(struct pe_edge *e, uint64_t now, uint8_t sc, bool down,
           struct pe_result *res) {
    // `pc_keys` is the physical state of the keyboard, whatever the state
    bool repeat = down && e->pc_keys[sc];
    e->pc_keys[sc] = down;

    const struct pe_hotkey *hk = &e->cfg.hotkey;

    // Emergency shortcut: always handled, whatever the state. It fires on every
    // press (even auto-repeats) because some keys (Pause) may never report
    // their release, and it must not depend on stale bookkeeping.
    if (sc == hk->key) {
        if (down) {
            uint8_t mods = pe_keymap_generic_mods(phys_hid_mods(e));
            if ((mods & hk->mods) == hk->mods) {
                e->hotkey_key_down = true;
                // Do not keep a "held" state that a missing release would
                // make permanent
                e->pc_keys[sc] = false;
                res->swallow = true;
                if (e->state == PE_STATE_PHONE_ACTIVE) {
                    leave_phone(e, now, PE_LEAVE_HOTKEY, res);
                }
                return;
            }
            e->hotkey_key_down = false; // pressed without the modifiers
        } else if (e->hotkey_key_down) {
            e->hotkey_key_down = false;
            // The shortcut press was swallowed, unless Windows already saw an
            // earlier press of this key
            res->swallow = !e->win_down[sc];
            return;
        }
    }

    // Windows saw this key going down (before the phone had the control, or
    // without the modifiers of a shortcut): it must see it going up, and the
    // repeats are of no concern
    if (e->win_down[sc]) {
        if (!down) {
            res->swallow = false;
        } else {
            // auto-repeat: the PC keeps it while it has the control
            res->swallow = e->state == PE_STATE_PHONE_ACTIVE;
        }
        return;
    }

    // A key swallowed during a phone session and still held: Windows never saw
    // it going down, so it must not see its auto-repeats nor its release
    if (e->swallowed_until_up[sc]) {
        res->swallow = true;
        if (!down) {
            e->swallowed_until_up[sc] = false;
        }
        if (e->state == PE_STATE_PC_ACTIVE) {
            return;
        }
    } else if (e->state == PE_STATE_PC_ACTIVE) {
        return;
    }

    // PE_STATE_PHONE_ACTIVE, a key Windows does not know
    res->swallow = true;
    if (!down) {
        e->swallowed_until_up[sc] = false;
        if (!e->phone_keys[sc]) {
            return;
        }
    } else {
        e->swallowed_until_up[sc] = true;
        if (repeat || e->phone_keys[sc]) {
            return;
        }
    }

    e->phone_keys[sc] = down;
    uint8_t bit = pe_keymap_hid_mod_bit(sc);
    if (bit) {
        if (down) {
            e->phone_hid_mods |= bit;
        } else {
            e->phone_hid_mods &= (uint8_t) ~bit;
        }
    }

    flush_pending(e, now, res, true);
    struct pe_action *a = res_push(res, PE_ACT_PHONE_KEY);
    if (a) {
        a->key.scancode = sc;
        a->key.down = down;
        a->key.mods = phone_mods(e);
    }
}

void
pe_edge_on_key(struct pe_edge *e, uint64_t now, uint8_t sc, bool down,
               struct pe_result *res) {
    res_init(res);

    // Keys without HID usage are always left to the PC (see pe_edge.h)
    if (e->state == PE_STATE_DISABLED || sc == 0) {
        return;
    }

    handle_key(e, now, sc, down, res);

    // Track what Windows has seen
    if (!res->swallow) {
        e->win_down[sc] = down;
    }
}

void
pe_edge_on_disconnect(struct pe_edge *e, uint64_t now, struct pe_result *res) {
    res_init(res);

    if (e->state == PE_STATE_PHONE_ACTIVE) {
        leave_phone(e, now, PE_LEAVE_DISCONNECT, res);
    }
    e->state = PE_STATE_DISABLED;
}

void
pe_edge_release(struct pe_edge *e, uint64_t now, struct pe_result *res) {
    res_init(res);

    if (e->state == PE_STATE_PHONE_ACTIVE) {
        leave_phone(e, now, PE_LEAVE_RELEASE, res);
    }
    e->state = PE_STATE_DISABLED;
}
