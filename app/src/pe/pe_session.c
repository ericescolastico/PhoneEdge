#include "pe_session.h"

#include <string.h>

#include "util/log.h"

static const char *
leave_reason_name(enum pe_leave_reason r) {
    switch (r) {
        case PE_LEAVE_EDGE: return "edge";
        case PE_LEAVE_HOTKEY: return "emergency hotkey";
        case PE_LEAVE_DISCONNECT: return "phone disconnected";
        case PE_LEAVE_RELEASE: return "release";
    }
    return "?";
}

static void
dispatch(struct pe_session *s) {
    const struct pe_result *res = &s->res;
    for (unsigned i = 0; i < res->count; ++i) {
        const struct pe_action *a = &res->actions[i];
        switch (a->type) {
            case PE_ACT_ENTER_PHONE:
                ++s->entries;
                LOGI("Control moved to the phone (ratio %.3f)",
                     a->enter.ratio);
                break;
            case PE_ACT_PHONE_MOVE:
                LOGV("phone move dx=%d dy=%d%s", a->move.dx, a->move.dy,
                     a->move.placement ? " (placement)" : "");
                s->phone.move(s->phone_ud, a->move.dx, a->move.dy,
                              a->move.placement, a->move.delay_ms);
                break;
            case PE_ACT_PHONE_BUTTONS:
                LOGD("phone buttons=0x%02x", a->buttons.buttons);
                s->phone.buttons(s->phone_ud, a->buttons.buttons);
                break;
            case PE_ACT_PHONE_WHEEL:
                LOGV("phone wheel h=%.2f v=%.2f", a->wheel.horizontal,
                     a->wheel.vertical);
                s->phone.wheel(s->phone_ud, a->wheel.horizontal,
                               a->wheel.vertical);
                break;
            case PE_ACT_PHONE_KEY:
                LOGV("phone key 0x%02x %s mods=0x%02x", a->key.scancode,
                     a->key.down ? "down" : "up", a->key.mods);
                s->phone.key(s->phone_ud, a->key.scancode, a->key.down,
                             a->key.mods);
                break;
            case PE_ACT_LEAVE_PHONE: {
                ++s->leaves;
                float vx, vy;
                pe_edge_get_vpos(&s->edge, &vx, &vy);
                LOGI("Control back to the PC: %s (estimated pointer %.0f,%.0f,"
                     " ratio %.3f)", leave_reason_name(a->leave.reason),
                     vx, vy, a->leave.ratio);
                if (a->leave.set_cursor) {
                    LOGD("PC cursor -> %d,%d", (int) a->leave.pc_x,
                         (int) a->leave.pc_y);
                    s->pc.set_cursor(s->pc_ud, a->leave.pc_x, a->leave.pc_y);
                }
                break;
            }
        }
    }
}

static bool
finish(struct pe_session *s) {
    atomic_store(&s->phone_active, pe_edge_is_phone_active(&s->edge));
    atomic_store(&s->disabled, pe_edge_state(&s->edge) == PE_STATE_DISABLED);
    return s->res.swallow;
}

bool
pe_session_init(struct pe_session *s, const struct pe_config *cfg,
                const struct pe_phone_ops *phone, void *phone_ud,
                const struct pe_pc_ops *pc, void *pc_ud) {
    memset(s, 0, sizeof(*s));
    if (!sc_mutex_init(&s->mutex)) {
        return false;
    }
    pe_edge_init(&s->edge, cfg);
    s->phone = *phone;
    s->phone_ud = phone_ud;
    s->pc = *pc;
    s->pc_ud = pc_ud;
    atomic_init(&s->phone_active, false);
    atomic_init(&s->disabled, false);
    return true;
}

void
pe_session_destroy(struct pe_session *s) {
    sc_mutex_destroy(&s->mutex);
}

void
pe_session_set_monitors(struct pe_session *s, const struct pe_rect *rects,
                        int count, int selected) {
    sc_mutex_lock(&s->mutex);
    pe_edge_set_monitors(&s->edge, rects, count, selected);
    sc_mutex_unlock(&s->mutex);
}

void
pe_session_set_phone_size(struct pe_session *s, int width, int height) {
    sc_mutex_lock(&s->mutex);
    pe_edge_set_phone_size(&s->edge, width, height);
    sc_mutex_unlock(&s->mutex);
}

bool
pe_session_on_mouse_move(struct pe_session *s, uint64_t now, int dx, int dy,
                         int cursor_x, int cursor_y) {
    sc_mutex_lock(&s->mutex);
    pe_edge_on_mouse_move(&s->edge, now, dx, dy, cursor_x, cursor_y, &s->res);
    dispatch(s);
    bool swallow = finish(s);
    sc_mutex_unlock(&s->mutex);
    return swallow;
}

bool
pe_session_on_button(struct pe_session *s, uint64_t now, uint8_t button,
                     bool down) {
    sc_mutex_lock(&s->mutex);
    pe_edge_on_button(&s->edge, now, button, down, &s->res);
    dispatch(s);
    bool swallow = finish(s);
    sc_mutex_unlock(&s->mutex);
    return swallow;
}

bool
pe_session_on_wheel(struct pe_session *s, uint64_t now, bool horizontal,
                    int delta) {
    sc_mutex_lock(&s->mutex);
    pe_edge_on_wheel(&s->edge, now, horizontal, delta, &s->res);
    dispatch(s);
    bool swallow = finish(s);
    sc_mutex_unlock(&s->mutex);
    return swallow;
}

bool
pe_session_on_key(struct pe_session *s, uint64_t now, uint8_t scancode,
                  bool down) {
    sc_mutex_lock(&s->mutex);
    pe_edge_on_key(&s->edge, now, scancode, down, &s->res);
    dispatch(s);
    bool swallow = finish(s);
    sc_mutex_unlock(&s->mutex);
    return swallow;
}

void
pe_session_on_tick(struct pe_session *s, uint64_t now) {
    sc_mutex_lock(&s->mutex);
    pe_edge_on_tick(&s->edge, now, &s->res);
    dispatch(s);
    finish(s);
    sc_mutex_unlock(&s->mutex);
}

void
pe_session_disconnect(struct pe_session *s, uint64_t now) {
    sc_mutex_lock(&s->mutex);
    pe_edge_on_disconnect(&s->edge, now, &s->res);
    dispatch(s);
    finish(s);
    sc_mutex_unlock(&s->mutex);
}

void
pe_session_release(struct pe_session *s, uint64_t now) {
    sc_mutex_lock(&s->mutex);
    pe_edge_release(&s->edge, now, &s->res);
    dispatch(s);
    finish(s);
    sc_mutex_unlock(&s->mutex);
}
