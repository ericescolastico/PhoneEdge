#include "common.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "edge/pe_edge.h"

#define W 1080
#define H 2340

static const struct pe_rect MON_A = {0, 0, 1920, 1080};

static void
setup(struct pe_edge *e, enum pe_side edge) {
    struct pe_config cfg;
    pe_config_defaults(&cfg);
    cfg.edge = edge;
    cfg.phone_size_auto = false;
    cfg.phone_width = W;
    cfg.phone_height = H;
    pe_edge_init(e, &cfg);
    pe_edge_set_monitors(e, &MON_A, 1, 0);
}

static unsigned
count_type(const struct pe_result *res, enum pe_action_type type) {
    unsigned n = 0;
    for (unsigned i = 0; i < res->count; ++i) {
        n += res->actions[i].type == type;
    }
    return n;
}

static const struct pe_action *
find_type(const struct pe_result *res, enum pe_action_type type) {
    for (unsigned i = 0; i < res->count; ++i) {
        if (res->actions[i].type == type) {
            return &res->actions[i];
        }
    }
    return NULL;
}

static void
sum_moves(const struct pe_result *res, bool placement, int *sx, int *sy) {
    *sx = *sy = 0;
    for (unsigned i = 0; i < res->count; ++i) {
        const struct pe_action *a = &res->actions[i];
        if (a->type == PE_ACT_PHONE_MOVE && a->move.placement == placement) {
            *sx += a->move.dx;
            *sy += a->move.dy;
        }
    }
}

// Cursor position on the edge for each PC edge, with the given ratio
static void
edge_point(enum pe_side edge, float ratio, int *x, int *y) {
    switch (edge) {
        case PE_SIDE_RIGHT:
            *x = 1919; *y = (int) (ratio * 1079); break;
        case PE_SIDE_LEFT:
            *x = 0; *y = (int) (ratio * 1079); break;
        case PE_SIDE_TOP:
            *x = (int) (ratio * 1919); *y = 0; break;
        default:
            *x = (int) (ratio * 1919); *y = 1079; break;
    }
}

// Raw delta pushing toward the PC edge
static void
push_delta(enum pe_side edge, int amount, int *dx, int *dy) {
    *dx = *dy = 0;
    switch (edge) {
        case PE_SIDE_RIGHT: *dx = amount; break;
        case PE_SIDE_LEFT: *dx = -amount; break;
        case PE_SIDE_TOP: *dy = -amount; break;
        default: *dy = amount; break;
    }
}

static uint64_t T0 = 1000;

static void
enter(struct pe_edge *e, enum pe_side edge, float ratio, struct pe_result *res) {
    int x, y, dx, dy;
    edge_point(edge, ratio, &x, &y);
    push_delta(edge, 20, &dx, &dy);
    pe_edge_on_mouse_move(e, T0, dx, dy, x, y, res);
    assert(pe_edge_state(e) == PE_STATE_PHONE_ACTIVE);
}

static uint64_t g_leave_time;

// Push toward the PC (phone opposite edge) for `ms`, one event every 10 ms
static const struct pe_action *
push_back(struct pe_edge *e, enum pe_side edge, uint64_t from, uint64_t ms,
          struct pe_result *res) {
    // the phone pointer must go toward the PC: opposite of the push to enter
    int dx, dy;
    push_delta(edge, -20, &dx, &dy);
    for (uint64_t t = from; t <= from + ms; t += 10) {
        pe_edge_on_mouse_move(e, t, dx, dy, 0, 0, res);
        const struct pe_action *l = find_type(res, PE_ACT_LEAVE_PHONE);
        if (l) {
            g_leave_time = t;
            return l;
        }
    }
    return NULL;
}

static void test_enter_right_anchoring(void) {
    struct pe_edge e;
    setup(&e, PE_SIDE_RIGHT);
    struct pe_result res;

    enter(&e, PE_SIDE_RIGHT, 0.25f, &res);
    assert(res.swallow);
    assert(res.actions[0].type == PE_ACT_ENTER_PHONE);
    assert(fabsf(res.actions[0].enter.ratio - 0.25f) < 0.01f);

    // 1 + anchoring reports + positioning, all flagged as placement
    int ax, ay;
    sum_moves(&res, true, &ax, &ay);
    unsigned moves = count_type(&res, PE_ACT_PHONE_MOVE);
    assert(moves >= 20);
    for (unsigned i = 1; i < res.count; ++i) {
        assert(res.actions[i].type == PE_ACT_PHONE_MOVE);
        assert(res.actions[i].move.placement);
        assert(res.actions[i].move.dx >= -127 && res.actions[i].move.dx <= 127);
    }
    // ratio 0.25 -> anchored to the top-left corner: first reports go
    // up-left
    assert(res.actions[1].move.dx == -127 && res.actions[1].move.dy == -127);

    // estimated position: entry inset on the left edge, 25% of the height
    float x, y;
    pe_edge_get_vpos(&e, &x, &y);
    assert(fabsf(x - 24) < 1.5f);
    assert(fabsf(y - res.actions[0].enter.ratio * (H - 1)) < 1.5f);
}

static void test_enter_anchors_to_nearest_corner(void) {
    struct pe_edge e;
    setup(&e, PE_SIDE_RIGHT);
    struct pe_result res;
    enter(&e, PE_SIDE_RIGHT, 0.9f, &res);
    // bottom-left corner
    assert(res.actions[1].move.dx == -127 && res.actions[1].move.dy == 127);
    float x, y;
    pe_edge_get_vpos(&e, &x, &y);
    assert(fabsf(y - res.actions[0].enter.ratio * (H - 1)) < 1.5f);
}

static void test_edge_push_threshold(void) {
    struct pe_edge e;
    setup(&e, PE_SIDE_RIGHT);
    struct pe_result res;

    // default edge_push = 12
    pe_edge_on_mouse_move(&e, T0, 5, 0, 1919, 500, &res);
    assert(!res.swallow && res.count == 0);
    pe_edge_on_mouse_move(&e, T0 + 1, 5, 0, 1919, 500, &res);
    assert(pe_edge_state(&e) == PE_STATE_PC_ACTIVE);
    pe_edge_on_mouse_move(&e, T0 + 2, 5, 0, 1919, 500, &res);
    assert(pe_edge_state(&e) == PE_STATE_PHONE_ACTIVE);
}

static void test_no_entry_away_from_edge_or_inward(void) {
    struct pe_edge e;
    setup(&e, PE_SIDE_RIGHT);
    struct pe_result res;

    pe_edge_on_mouse_move(&e, T0, 50, 0, 1000, 500, &res);
    assert(pe_edge_state(&e) == PE_STATE_PC_ACTIVE);
    assert(!res.swallow);

    // pushing, then moving inward resets the accumulator
    pe_edge_on_mouse_move(&e, T0 + 1, 8, 0, 1919, 500, &res);
    pe_edge_on_mouse_move(&e, T0 + 2, -1, 0, 1919, 500, &res);
    pe_edge_on_mouse_move(&e, T0 + 3, 8, 0, 1919, 500, &res);
    assert(pe_edge_state(&e) == PE_STATE_PC_ACTIVE);

    // pushing along another edge does nothing
    pe_edge_on_mouse_move(&e, T0 + 4, 0, 50, 500, 1079, &res);
    assert(pe_edge_state(&e) == PE_STATE_PC_ACTIVE);
    // pushing the wrong way at the edge does nothing
    pe_edge_on_mouse_move(&e, T0 + 5, -50, 0, 1919, 500, &res);
    assert(pe_edge_state(&e) == PE_STATE_PC_ACTIVE);
}

static void test_all_edges_roundtrip(void) {
    const enum pe_side edges[] = {
        PE_SIDE_LEFT, PE_SIDE_RIGHT, PE_SIDE_TOP, PE_SIDE_BOTTOM,
    };
    for (unsigned i = 0; i < 4; ++i) {
        enum pe_side edge = edges[i];
        struct pe_edge e;
        setup(&e, edge);
        struct pe_result res;

        enter(&e, edge, 0.5f, &res);
        float ex, ey;
        pe_edge_get_vpos(&e, &ex, &ey);
        // the pointer appears on the phone edge facing the PC
        switch (edge) {
            case PE_SIDE_RIGHT: assert(ex < 30); break;          // left
            case PE_SIDE_LEFT: assert(ex > W - 1 - 30); break;   // right
            case PE_SIDE_BOTTOM: assert(ey < 30); break;         // top
            default: assert(ey > H - 1 - 30); break;             // bottom
        }

        // Come back through the same phone edge
        const struct pe_action *l = push_back(&e, edge, T0 + 500, 2000, &res);
        assert(l);
        assert(l->leave.reason == PE_LEAVE_EDGE);
        assert(l->leave.set_cursor);
        assert(pe_edge_state(&e) == PE_STATE_PC_ACTIVE);
        assert(res.swallow);

        // The PC cursor reappears just inside the crossed edge, at the same
        // proportion along it
        int32_t px = l->leave.pc_x;
        int32_t py = l->leave.pc_y;
        switch (edge) {
            case PE_SIDE_RIGHT:
                assert(px == 1919 - 3);
                assert(abs(py - 540) <= 3);
                break;
            case PE_SIDE_LEFT:
                assert(px == 3);
                assert(abs(py - 540) <= 3);
                break;
            case PE_SIDE_TOP:
                assert(py == 3);
                assert(abs(px - 960) <= 3);
                break;
            default:
                assert(py == 1079 - 3);
                assert(abs(px - 960) <= 3);
                break;
        }
    }
}

static void test_leave_ratio_follows_pointer(void) {
    struct pe_edge e;
    setup(&e, PE_SIDE_RIGHT);
    struct pe_result res;
    enter(&e, PE_SIDE_RIGHT, 0.25f, &res);
    float entry_ratio = res.actions[0].enter.ratio;

    // Move down by 1170 px (half of the phone height), 100 units per 10 ms
    for (int i = 0; i < 12; ++i) {
        pe_edge_on_mouse_move(&e, T0 + 20 + 10 * i, 0, 100, 0, 0, &res);
    }
    float x, y;
    pe_edge_get_vpos(&e, &x, &y);
    float entry_y = entry_ratio * (H - 1);
    assert(fabsf(y - (entry_y + 1200)) < 2.0f);

    const struct pe_action *l = push_back(&e, PE_SIDE_RIGHT, T0 + 800, 2000,
                                          &res);
    assert(l);
    float expected = (entry_y + 1200) / (H - 1);
    assert(fabsf(l->leave.ratio - expected) < 0.01f);
    assert(abs(l->leave.pc_y - (int) lroundf(expected * 1079)) <= 2);
}

static void test_clamp_and_chunking(void) {
    struct pe_edge e;
    setup(&e, PE_SIDE_RIGHT);
    struct pe_result res;
    enter(&e, PE_SIDE_RIGHT, 0.5f, &res);

    // 300 units in a single flush: 127 + 127 + 46
    pe_edge_on_mouse_move(&e, T0 + 100, 300, 0, 0, 0, &res);
    assert(count_type(&res, PE_ACT_PHONE_MOVE) == 3);
    assert(res.actions[0].move.dx == 127);
    assert(res.actions[1].move.dx == 127);
    assert(res.actions[2].move.dx == 46);
    for (unsigned i = 0; i < 3; ++i) {
        assert(!res.actions[i].move.placement);
    }

    // The estimate is clamped to the phone screen
    for (int i = 0; i < 100; ++i) {
        pe_edge_on_mouse_move(&e, T0 + 200 + 10 * i, 4000, 4000, 0, 0, &res);
    }
    float x, y;
    pe_edge_get_vpos(&e, &x, &y);
    assert(x == (float) (W - 1));
    assert(y == (float) (H - 1));
}

static void test_flush_rate_and_tick(void) {
    struct pe_edge e;
    setup(&e, PE_SIDE_RIGHT);
    struct pe_result res;
    enter(&e, PE_SIDE_RIGHT, 0.5f, &res);

    // The first motion after a quiet period goes out right away (latency)
    pe_edge_on_mouse_move(&e, T0 + 100, 3, 1, 0, 0, &res);
    assert(res.swallow && res.count == 1);
    assert(res.actions[0].move.dx == 3 && res.actions[0].move.dy == 1);

    // flush_ms = 8: the next ones, 1 ms apart, are aggregated
    pe_edge_on_mouse_move(&e, T0 + 101, 3, 1, 0, 0, &res);
    assert(res.swallow && res.count == 0);
    pe_edge_on_mouse_move(&e, T0 + 102, 3, 1, 0, 0, &res);
    assert(res.count == 0);
    pe_edge_on_tick(&e, T0 + 105, &res);
    assert(res.swallow && res.count == 0);
    pe_edge_on_tick(&e, T0 + 108, &res);
    assert(res.count == 1);
    assert(res.actions[0].move.dx == 6 && res.actions[0].move.dy == 2);
    assert(!res.actions[0].move.placement);
    // nothing left to send
    pe_edge_on_tick(&e, T0 + 130, &res);
    assert(res.count == 0);
}

static void test_sensitivity_residual(void) {
    struct pe_config cfg;
    pe_config_defaults(&cfg);
    cfg.sensitivity = 0.5f;
    cfg.phone_size_auto = false;
    struct pe_edge e;
    pe_edge_init(&e, &cfg);
    pe_edge_set_monitors(&e, &MON_A, 1, 0);
    struct pe_result res;
    enter(&e, PE_SIDE_RIGHT, 0.5f, &res);

    int total = 0;
    for (int i = 0; i < 10; ++i) {
        pe_edge_on_mouse_move(&e, T0 + 100 + 10 * i, 1, 0, 0, 0, &res);
        for (unsigned j = 0; j < res.count; ++j) {
            total += res.actions[j].move.dx;
        }
    }
    // 10 counts * 0.5, nothing lost to rounding
    assert(total == 5);
}

static void test_pointer_scale(void) {
    struct pe_config cfg;
    pe_config_defaults(&cfg);
    cfg.pointer_scale = 2.0f;
    cfg.phone_size_auto = false;
    struct pe_edge e;
    pe_edge_init(&e, &cfg);
    pe_edge_set_monitors(&e, &MON_A, 1, 0);
    struct pe_result res;
    enter(&e, PE_SIDE_RIGHT, 0.25f, &res);

    // one unit moves the pointer by 2 px: fewer anchoring reports, and the
    // estimate still lands on the entry point
    assert(count_type(&res, PE_ACT_PHONE_MOVE) < 20);
    float x, y;
    pe_edge_get_vpos(&e, &x, &y);
    assert(fabsf(x - 24) < 2.5f);
    assert(fabsf(y - res.actions[0].enter.ratio * (H - 1)) < 2.5f);

    pe_edge_on_mouse_move(&e, T0 + 100, 0, 10, 0, 0, &res);
    float y2;
    pe_edge_get_vpos(&e, &x, &y2);
    assert(fabsf((y2 - y) - 20) < 0.01f);
}

static void test_enter_guard_and_margin(void) {
    struct pe_edge e;
    setup(&e, PE_SIDE_RIGHT);
    struct pe_result res;
    enter(&e, PE_SIDE_RIGHT, 0.5f, &res);

    // Push left right after entering: the guard (300 ms) forbids coming back,
    // even though the pointer reaches the edge
    for (int i = 0; i < 25; ++i) {
        pe_edge_on_mouse_move(&e, T0 + 10 * (i + 1), -20, 0, 0, 0, &res);
        assert(!find_type(&res, PE_ACT_LEAVE_PHONE) || i >= 20);
    }
    // ... but it does after the guard, once pushing past the margin (24 px)
    assert(pe_edge_state(&e) == PE_STATE_PC_ACTIVE
        || push_back(&e, PE_SIDE_RIGHT, T0 + 300, 1000, &res));
}

static void test_leave_margin_resets_when_moving_away(void) {
    struct pe_config cfg;
    pe_config_defaults(&cfg);
    cfg.phone_size_auto = false;
    cfg.enter_guard_ms = 0;
    cfg.leave_margin = 30;
    cfg.entry_inset = 10;
    struct pe_edge e;
    pe_edge_init(&e, &cfg);
    pe_edge_set_monitors(&e, &MON_A, 1, 0);
    struct pe_result res;
    enter(&e, PE_SIDE_RIGHT, 0.5f, &res);

    // 10 px to reach the edge, then 20 px beyond: below the margin
    pe_edge_on_mouse_move(&e, T0 + 100, -30, 0, 0, 0, &res);
    assert(pe_edge_state(&e) == PE_STATE_PHONE_ACTIVE);
    // moving away resets
    pe_edge_on_mouse_move(&e, T0 + 110, 5, 0, 0, 0, &res);
    pe_edge_on_mouse_move(&e, T0 + 120, -20, 0, 0, 0, &res);
    assert(pe_edge_state(&e) == PE_STATE_PHONE_ACTIVE);
    // 15 more beyond the edge: crosses the margin
    pe_edge_on_mouse_move(&e, T0 + 130, -20, 0, 0, 0, &res);
    assert(pe_edge_state(&e) == PE_STATE_PC_ACTIVE);
}

static void test_reenter_cooldown(void) {
    struct pe_edge e;
    setup(&e, PE_SIDE_RIGHT);
    struct pe_result res;
    enter(&e, PE_SIDE_RIGHT, 0.5f, &res);
    const struct pe_action *l = push_back(&e, PE_SIDE_RIGHT, T0 + 400, 2000,
                                          &res);
    assert(l);
    uint64_t t = g_leave_time;

    // reenter_ms = 300: pushing at the edge right away does not re-enter
    pe_edge_on_mouse_move(&e, t + 50, 30, 0, 1919, 500, &res);
    assert(pe_edge_state(&e) == PE_STATE_PC_ACTIVE);
    pe_edge_on_mouse_move(&e, t + 400, 30, 0, 1919, 500, &res);
    assert(pe_edge_state(&e) == PE_STATE_PHONE_ACTIVE);
}

static void test_multi_monitor_adjacent(void) {
    struct pe_edge e;
    setup(&e, PE_SIDE_RIGHT);
    struct pe_rect mons[2] = {
        {0, 0, 1920, 1080},
        {1920, 0, 3840, 1080},
    };
    struct pe_result res;

    // A neighbour monitor on the right: the right edge is not exposed
    pe_edge_set_monitors(&e, mons, 2, 0);
    pe_edge_on_mouse_move(&e, T0, 50, 0, 1919, 500, &res);
    assert(pe_edge_state(&e) == PE_STATE_PC_ACTIVE);
    assert(!res.swallow);

    // Selecting the right-hand monitor: its right edge is exposed
    pe_edge_set_monitors(&e, mons, 2, 1);
    pe_edge_on_mouse_move(&e, T0, 50, 0, 3839, 500, &res);
    assert(pe_edge_state(&e) == PE_STATE_PHONE_ACTIVE);
}

static void test_multi_monitor_partial_overlap(void) {
    struct pe_edge e;
    setup(&e, PE_SIDE_RIGHT);
    // B is shifted down: the top part of A's right edge is exposed
    struct pe_rect mons[2] = {
        {0, 0, 1920, 1080},
        {1920, 600, 3840, 1680},
    };
    pe_edge_set_monitors(&e, mons, 2, 0);
    struct pe_result res;

    pe_edge_on_mouse_move(&e, T0, 50, 0, 1919, 800, &res); // facing B
    assert(pe_edge_state(&e) == PE_STATE_PC_ACTIVE);
    pe_edge_on_mouse_move(&e, T0 + 1, 50, 0, 1919, 300, &res); // exposed
    assert(pe_edge_state(&e) == PE_STATE_PHONE_ACTIVE);
}

static void test_multi_monitor_left_edge_and_negative_coords(void) {
    struct pe_edge e;
    setup(&e, PE_SIDE_LEFT);
    // The selected monitor is the one on the left of the primary: negative
    // coordinates, and its left edge is exposed
    struct pe_rect mons[2] = {
        {-1920, 0, 0, 1080},
        {0, 0, 1920, 1080},
    };
    pe_edge_set_monitors(&e, mons, 2, 0);
    struct pe_result res;
    pe_edge_on_mouse_move(&e, T0, -50, 0, -1920, 540, &res);
    assert(pe_edge_state(&e) == PE_STATE_PHONE_ACTIVE);

    // The selected monitor is the primary: its left edge touches the other
    struct pe_edge e2;
    setup(&e2, PE_SIDE_LEFT);
    pe_edge_set_monitors(&e2, mons, 2, 1);
    pe_edge_on_mouse_move(&e2, T0, -50, 0, 0, 540, &res);
    assert(pe_edge_state(&e2) == PE_STATE_PC_ACTIVE);
}

static void test_monitor_not_selected(void) {
    struct pe_edge e;
    setup(&e, PE_SIDE_RIGHT);
    struct pe_result res;
    pe_edge_set_monitors(&e, &MON_A, 1, 5); // invalid index
    pe_edge_on_mouse_move(&e, T0, 50, 0, 1919, 500, &res);
    assert(pe_edge_state(&e) == PE_STATE_PC_ACTIVE);
}

static void test_buttons_and_wheel(void) {
    struct pe_edge e;
    setup(&e, PE_SIDE_RIGHT);
    struct pe_result res;

    // PC mode: not swallowed
    pe_edge_on_button(&e, T0, PE_BTN_LEFT, true, &res);
    assert(!res.swallow && res.count == 0);
    // cannot cross while a button is held
    pe_edge_on_mouse_move(&e, T0 + 1, 50, 0, 1919, 500, &res);
    assert(pe_edge_state(&e) == PE_STATE_PC_ACTIVE);
    pe_edge_on_button(&e, T0 + 2, PE_BTN_LEFT, false, &res);

    // the wheel is ignored on the PC side
    pe_edge_on_wheel(&e, T0 + 3, false, 120, &res);
    assert(!res.swallow && res.count == 0);

    pe_edge_on_mouse_move(&e, T0 + 4, 50, 0, 1919, 500, &res);
    assert(pe_edge_state(&e) == PE_STATE_PHONE_ACTIVE);

    pe_edge_on_button(&e, T0 + 100, PE_BTN_LEFT, true, &res);
    assert(res.swallow);
    const struct pe_action *b = find_type(&res, PE_ACT_PHONE_BUTTONS);
    assert(b && b->buttons.buttons == PE_BTN_LEFT);
    pe_edge_on_button(&e, T0 + 101, PE_BTN_RIGHT, true, &res);
    b = find_type(&res, PE_ACT_PHONE_BUTTONS);
    assert(b && b->buttons.buttons == (PE_BTN_LEFT | PE_BTN_RIGHT));
    pe_edge_on_button(&e, T0 + 102, PE_BTN_LEFT, false, &res);
    b = find_type(&res, PE_ACT_PHONE_BUTTONS);
    assert(b && b->buttons.buttons == PE_BTN_RIGHT);

    pe_edge_on_wheel(&e, T0 + 103, false, -240, &res);
    assert(res.swallow);
    const struct pe_action *w = find_type(&res, PE_ACT_PHONE_WHEEL);
    assert(w && w->wheel.vertical == -2.0f && w->wheel.horizontal == 0);
    pe_edge_on_wheel(&e, T0 + 104, true, 60, &res);
    w = find_type(&res, PE_ACT_PHONE_WHEEL);
    assert(w && w->wheel.horizontal == 0.5f && w->wheel.vertical == 0);

    // A click is preceded by the pending motion
    pe_edge_on_mouse_move(&e, T0 + 105, 4, 0, 0, 0, &res); // sent right away
    pe_edge_on_mouse_move(&e, T0 + 106, 4, 0, 0, 0, &res); // pending
    assert(res.count == 0);
    pe_edge_on_button(&e, T0 + 107, PE_BTN_LEFT, true, &res);
    assert(res.actions[0].type == PE_ACT_PHONE_MOVE);
    assert(res.actions[1].type == PE_ACT_PHONE_BUTTONS);
}

static void test_keys(void) {
    struct pe_edge e;
    setup(&e, PE_SIDE_RIGHT);
    struct pe_result res;

    // PC mode: keys pass through
    pe_edge_on_key(&e, T0, 0x04, true, &res);
    assert(!res.swallow && res.count == 0);
    pe_edge_on_key(&e, T0, 0x04, false, &res);

    // Shift is held when entering
    pe_edge_on_key(&e, T0 + 1, 0xE1, true, &res);
    pe_edge_on_mouse_move(&e, T0 + 2, 50, 0, 1919, 500, &res);
    assert(pe_edge_state(&e) == PE_STATE_PHONE_ACTIVE);

    // 'a' goes to the phone, with the held modifiers of the phone only
    pe_edge_on_key(&e, T0 + 100, 0x04, true, &res);
    assert(res.swallow);
    const struct pe_action *k = find_type(&res, PE_ACT_PHONE_KEY);
    assert(k && k->key.scancode == 0x04 && k->key.down && k->key.mods == 0);
    // auto-repeat is ignored
    pe_edge_on_key(&e, T0 + 101, 0x04, true, &res);
    assert(res.swallow && res.count == 0);
    pe_edge_on_key(&e, T0 + 102, 0x04, false, &res);
    k = find_type(&res, PE_ACT_PHONE_KEY);
    assert(k && !k->key.down);

    // Ctrl pressed in the phone shows in the mods of the next keys
    pe_edge_on_key(&e, T0 + 103, 0xE0, true, &res);
    k = find_type(&res, PE_ACT_PHONE_KEY);
    assert(k && k->key.mods == PE_HID_LCTRL);
    pe_edge_on_key(&e, T0 + 104, 0x06, true, &res); // ctrl+c
    k = find_type(&res, PE_ACT_PHONE_KEY);
    assert(k && k->key.mods == PE_HID_LCTRL);

    // Shift was pressed before entering: Windows must see its release, and
    // the phone must not receive it
    pe_edge_on_key(&e, T0 + 105, 0xE1, false, &res);
    assert(!res.swallow && res.count == 0);
    // ... and only once
    pe_edge_on_key(&e, T0 + 106, 0xE1, true, &res);
    assert(res.swallow && count_type(&res, PE_ACT_PHONE_KEY) == 1);
}

static void test_emergency_hotkey(void) {
    struct pe_edge e;
    setup(&e, PE_SIDE_RIGHT);
    struct pe_result res;
    enter(&e, PE_SIDE_RIGHT, 0.5f, &res);

    pe_edge_on_mouse_move(&e, T0 + 100, 5, 0, 0, 0, &res); // pending motion
    pe_edge_on_button(&e, T0 + 101, PE_BTN_LEFT, true, &res);
    pe_edge_on_key(&e, T0 + 102, 0xE0, true, &res); // Ctrl (left)
    pe_edge_on_key(&e, T0 + 103, 0xE6, true, &res); // Alt (right: any works)
    pe_edge_on_key(&e, T0 + 104, 0x04, true, &res);

    pe_edge_on_key(&e, T0 + 105, PE_SC_PAUSE, true, &res);
    assert(res.swallow);
    assert(pe_edge_state(&e) == PE_STATE_PC_ACTIVE);

    // The phone gets everything released: button, 'a', ctrl, alt
    const struct pe_action *l = find_type(&res, PE_ACT_LEAVE_PHONE);
    assert(l && l->leave.reason == PE_LEAVE_HOTKEY && l->leave.set_cursor);
    const struct pe_action *b = find_type(&res, PE_ACT_PHONE_BUTTONS);
    assert(b && b->buttons.buttons == 0);
    unsigned up_keys = 0;
    uint8_t last_mods = 0xFF;
    for (unsigned i = 0; i < res.count; ++i) {
        if (res.actions[i].type == PE_ACT_PHONE_KEY) {
            assert(!res.actions[i].key.down);
            ++up_keys;
            last_mods = res.actions[i].key.mods;
        }
    }
    assert(up_keys == 3);
    assert(last_mods == 0);
    // the leave comes last
    assert(res.actions[res.count - 1].type == PE_ACT_LEAVE_PHONE);

    // The release of the shortcut key is swallowed too
    pe_edge_on_key(&e, T0 + 106, PE_SC_PAUSE, false, &res);
    assert(res.swallow);
    // ... but the PC keys behave normally again
    pe_edge_on_key(&e, T0 + 107, 0x05, true, &res);
    assert(!res.swallow && res.count == 0);
}

static void test_hotkey_on_pc_and_other_chords(void) {
    struct pe_edge e;
    setup(&e, PE_SIDE_RIGHT);
    struct pe_result res;

    // On the PC the chord is swallowed (it would otherwise be Ctrl+Break) but
    // does nothing
    pe_edge_on_key(&e, T0, 0xE0, true, &res);
    pe_edge_on_key(&e, T0, 0xE2, true, &res);
    pe_edge_on_key(&e, T0, PE_SC_PAUSE, true, &res);
    assert(res.swallow && res.count == 0);
    pe_edge_on_key(&e, T0, PE_SC_PAUSE, false, &res);
    assert(res.swallow);
    pe_edge_on_key(&e, T0, 0xE2, false, &res);
    pe_edge_on_key(&e, T0, 0xE0, false, &res);

    // Pause alone, or with a single modifier, is not the shortcut
    pe_edge_on_key(&e, T0 + 1, PE_SC_PAUSE, true, &res);
    assert(!res.swallow);
    pe_edge_on_key(&e, T0 + 1, PE_SC_PAUSE, false, &res);
    pe_edge_on_key(&e, T0 + 2, 0xE0, true, &res);
    pe_edge_on_key(&e, T0 + 2, PE_SC_PAUSE, true, &res);
    assert(!res.swallow);
}

static void test_custom_hotkey(void) {
    struct pe_config cfg;
    pe_config_defaults(&cfg);
    char err[64];
    assert(pe_hotkey_parse("ctrl+shift+f12", &cfg.hotkey, err, sizeof(err)));
    struct pe_edge e;
    pe_edge_init(&e, &cfg);
    pe_edge_set_monitors(&e, &MON_A, 1, 0);
    struct pe_result res;
    enter(&e, PE_SIDE_RIGHT, 0.5f, &res);

    pe_edge_on_key(&e, T0 + 100, 0xE0, true, &res);
    pe_edge_on_key(&e, T0 + 101, PE_SC_PAUSE, true, &res); // old shortcut
    assert(pe_edge_state(&e) == PE_STATE_PHONE_ACTIVE);
    pe_edge_on_key(&e, T0 + 102, PE_SC_PAUSE, false, &res);
    pe_edge_on_key(&e, T0 + 103, 0xE1, true, &res);
    pe_edge_on_key(&e, T0 + 104, 0x45, true, &res); // F12
    assert(pe_edge_state(&e) == PE_STATE_PC_ACTIVE);
}

static void test_disconnect_while_on_phone(void) {
    struct pe_edge e;
    setup(&e, PE_SIDE_RIGHT);
    struct pe_result res;
    enter(&e, PE_SIDE_RIGHT, 0.5f, &res);
    pe_edge_on_key(&e, T0 + 100, 0x04, true, &res);
    pe_edge_on_button(&e, T0 + 101, PE_BTN_LEFT, true, &res);
    pe_edge_on_mouse_move(&e, T0 + 102, 5, 5, 0, 0, &res);

    pe_edge_on_disconnect(&e, T0 + 200, &res);
    assert(pe_edge_state(&e) == PE_STATE_DISABLED);
    assert(res.swallow);
    // Nothing is sent to a phone that is gone: only the PC cursor is restored
    assert(res.count == 1);
    assert(res.actions[0].type == PE_ACT_LEAVE_PHONE);
    assert(res.actions[0].leave.reason == PE_LEAVE_DISCONNECT);
    assert(res.actions[0].leave.set_cursor);

    // From now on, the PC keeps everything, for good
    pe_edge_on_mouse_move(&e, T0 + 5000, 50, 0, 1919, 500, &res);
    assert(!res.swallow && res.count == 0);
    assert(pe_edge_state(&e) == PE_STATE_DISABLED);
    pe_edge_on_key(&e, T0 + 5001, 0x04, true, &res);
    assert(!res.swallow && res.count == 0);
    pe_edge_on_button(&e, T0 + 5002, PE_BTN_LEFT, true, &res);
    assert(!res.swallow && res.count == 0);
    pe_edge_on_wheel(&e, T0 + 5003, false, 120, &res);
    assert(!res.swallow && res.count == 0);
}

static void test_disconnect_on_pc_side(void) {
    struct pe_edge e;
    setup(&e, PE_SIDE_RIGHT);
    struct pe_result res;
    pe_edge_on_disconnect(&e, T0, &res);
    assert(pe_edge_state(&e) == PE_STATE_DISABLED);
    assert(!res.swallow && res.count == 0);
    pe_edge_on_mouse_move(&e, T0 + 1000, 50, 0, 1919, 500, &res);
    assert(pe_edge_state(&e) == PE_STATE_DISABLED);
}

static void test_release_on_exit(void) {
    struct pe_edge e;
    setup(&e, PE_SIDE_RIGHT);
    struct pe_result res;
    enter(&e, PE_SIDE_RIGHT, 0.5f, &res);
    pe_edge_on_button(&e, T0 + 100, PE_BTN_LEFT, true, &res);

    pe_edge_release(&e, T0 + 200, &res);
    assert(pe_edge_state(&e) == PE_STATE_DISABLED);
    const struct pe_action *b = find_type(&res, PE_ACT_PHONE_BUTTONS);
    assert(b && b->buttons.buttons == 0);
    const struct pe_action *l = find_type(&res, PE_ACT_LEAVE_PHONE);
    assert(l && l->leave.reason == PE_LEAVE_RELEASE);

    // releasing twice is harmless
    pe_edge_release(&e, T0 + 201, &res);
    assert(res.count == 0);
}

static void test_no_entry_without_phone_size(void) {
    struct pe_edge e;
    setup(&e, PE_SIDE_RIGHT);
    pe_edge_set_phone_size(&e, 0, 0);
    struct pe_result res;
    pe_edge_on_mouse_move(&e, T0, 50, 0, 1919, 500, &res);
    assert(pe_edge_state(&e) == PE_STATE_PC_ACTIVE);
    pe_edge_set_phone_size(&e, W, H);
    pe_edge_on_mouse_move(&e, T0 + 1, 50, 0, 1919, 500, &res);
    assert(pe_edge_state(&e) == PE_STATE_PHONE_ACTIVE);
}

static void test_rotation_swaps_size(void) {
    struct pe_config cfg;
    pe_config_defaults(&cfg);
    cfg.phone_size_auto = false;
    cfg.phone_rotation = 90;
    struct pe_edge e;
    pe_edge_init(&e, &cfg);
    assert(e.vpos.width == H && e.vpos.height == W);
}

int main(int argc, char *argv[]) {
    (void) argc;
    (void) argv;

    test_enter_right_anchoring();
    test_enter_anchors_to_nearest_corner();
    test_edge_push_threshold();
    test_no_entry_away_from_edge_or_inward();
    test_all_edges_roundtrip();
    test_leave_ratio_follows_pointer();
    test_clamp_and_chunking();
    test_flush_rate_and_tick();
    test_sensitivity_residual();
    test_pointer_scale();
    test_enter_guard_and_margin();
    test_leave_margin_resets_when_moving_away();
    test_reenter_cooldown();
    test_multi_monitor_adjacent();
    test_multi_monitor_partial_overlap();
    test_multi_monitor_left_edge_and_negative_coords();
    test_monitor_not_selected();
    test_buttons_and_wheel();
    test_keys();
    test_emergency_hotkey();
    test_hotkey_on_pc_and_other_chords();
    test_custom_hotkey();
    test_disconnect_while_on_phone();
    test_disconnect_on_pc_side();
    test_release_on_exit();
    test_no_entry_without_phone_size();
    test_rotation_swaps_size();
    return 0;
}
