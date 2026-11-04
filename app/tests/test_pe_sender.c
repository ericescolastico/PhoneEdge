#include "common.h"

#include <assert.h>
#include <string.h>
#include <SDL3/SDL_timer.h>

#include "pe/pe_inject.h"
#include "pe/pe_sender.h"
#include "util/tick.h"

#define MAX_EVENTS 4096

struct log {
    sc_mutex mutex;
    int count;
    char kind[MAX_EVENTS]; // 'm', 'b', 'w', 'k'
    int value[MAX_EVENTS];
    sc_tick time[MAX_EVENTS];
    unsigned delay[MAX_EVENTS];
    sc_tick block; // simulated slow phone: delay of each call
};

static void
add(struct log *l, char kind, int value) {
    sc_mutex_lock(&l->mutex);
    if (l->count < MAX_EVENTS) {
        l->kind[l->count] = kind;
        l->value[l->count] = value;
        l->time[l->count] = sc_tick_now();
        ++l->count;
    }
    sc_mutex_unlock(&l->mutex);
    if (l->block) {
        SDL_Delay((Uint32) SC_TICK_TO_MS(l->block));
    }
}

static void
l_move(void *ud, int dx, int dy, bool placement, unsigned delay) {
    (void) dy;
    (void) placement;
    (void) delay;
    add(ud, 'm', dx);
}

static void
l_buttons(void *ud, uint8_t b) {
    add(ud, 'b', b);
}

static void
l_wheel(void *ud, float h, float v) {
    (void) h;
    add(ud, 'w', (int) v);
}

static void
l_key(void *ud, uint8_t sc, bool down, uint8_t mods) {
    (void) mods;
    add(ud, 'k', down ? sc : -sc);
}

static const struct pe_phone_ops LOG_OPS = {l_move, l_buttons, l_wheel, l_key};

static void
log_init(struct log *l) {
    memset(l, 0, sizeof(*l));
    bool ok = sc_mutex_init(&l->mutex);
    assert(ok);
}

static void
log_destroy(struct log *l) {
    sc_mutex_destroy(&l->mutex);
}

static int
log_count(struct log *l) {
    sc_mutex_lock(&l->mutex);
    int n = l->count;
    sc_mutex_unlock(&l->mutex);
    return n;
}

static void
test_order(void) {
    struct log l;
    log_init(&l);
    struct pe_sender s;
    assert(pe_sender_init(&s, &LOG_OPS, &l));
    assert(pe_sender_start(&s));

    for (int i = 1; i <= 500; ++i) {
        pe_sender_ops.move(&s, i, 0, false, 0);
        if (i % 50 == 0) {
            pe_sender_ops.buttons(&s, i / 50);
            pe_sender_ops.key(&s, 4, true, 0);
            pe_sender_ops.wheel(&s, 0, 1);
        }
    }
    pe_sender_stop(&s, true);
    pe_sender_join(&s);

    // everything delivered, in the same order
    assert(l.count == 500 + 30);
    int next_move = 1;
    for (int i = 0; i < l.count; ++i) {
        if (l.kind[i] == 'm') {
            assert(l.value[i] == next_move);
            ++next_move;
        }
    }
    assert(next_move == 501);
    pe_sender_destroy(&s);
    log_destroy(&l);
}

static void
test_drain_releases_on_stop(void) {
    struct log l;
    log_init(&l);
    l.block = SC_TICK_FROM_MS(5); // slow phone
    struct pe_sender s;
    assert(pe_sender_init(&s, &LOG_OPS, &l));
    assert(pe_sender_start(&s));

    for (int i = 0; i < 20; ++i) {
        pe_sender_ops.move(&s, 1, 0, false, 0);
    }
    // the release of the held key/button queued just before exiting
    pe_sender_ops.buttons(&s, 0);
    pe_sender_ops.key(&s, 4, false, 0);
    pe_sender_stop(&s, true);
    pe_sender_join(&s);

    assert(l.count == 22);
    assert(l.kind[20] == 'b' && l.value[20] == 0);
    assert(l.kind[21] == 'k' && l.value[21] == -4);
    pe_sender_destroy(&s);
    log_destroy(&l);
}

static void
test_stop_without_drain_drops(void) {
    struct log l;
    log_init(&l);
    l.block = SC_TICK_FROM_MS(20);
    struct pe_sender s;
    assert(pe_sender_init(&s, &LOG_OPS, &l));
    assert(pe_sender_start(&s));
    for (int i = 0; i < 200; ++i) {
        pe_sender_ops.move(&s, 1, 0, false, 0);
    }
    SDL_Delay(50);
    sc_tick t0 = sc_tick_now();
    pe_sender_stop(&s, false);
    pe_sender_join(&s);
    // the phone is gone: it stops quickly, without delivering the 200
    assert(sc_tick_now() - t0 < SC_TICK_FROM_MS(500));
    assert(l.count < 20);
    // events after the stop are ignored
    pe_sender_ops.move(&s, 1, 0, false, 0);
    pe_sender_destroy(&s);
    log_destroy(&l);
}

static void
test_pacing(void) {
    struct log l;
    log_init(&l);
    struct pe_sender s;
    assert(pe_sender_init(&s, &LOG_OPS, &l));
    assert(pe_sender_start(&s));
    for (int i = 0; i < 5; ++i) {
        pe_sender_ops.move(&s, i + 1, 0, true, 10); // 10 ms between each
    }
    SDL_Delay(200);
    assert(log_count(&l) == 5);
    // the 5 paced moves are at least 10 ms apart
    sc_mutex_lock(&l.mutex);
    for (int i = 1; i < 5; ++i) {
        assert(l.time[i] - l.time[i - 1] >= SC_TICK_FROM_MS(9));
    }
    sc_mutex_unlock(&l.mutex);
    pe_sender_stop(&s, true);
    pe_sender_join(&s);
    pe_sender_destroy(&s);
    log_destroy(&l);
}

static void
test_queue_full_does_not_block(void) {
    struct log l;
    log_init(&l);
    l.block = SC_TICK_FROM_MS(50); // the phone is stuck
    struct pe_sender s;
    assert(pe_sender_init(&s, &LOG_OPS, &l));
    assert(pe_sender_start(&s));

    sc_tick t0 = sc_tick_now();
    for (int i = 0; i < PE_SENDER_QUEUE_SIZE * 2 + 10; ++i) {
        pe_sender_ops.move(&s, 1, 0, false, 0);
    }
    // the input hook never waits for a stuck phone
    assert(sc_tick_now() - t0 < SC_TICK_FROM_MS(500));
    assert(s.dropped > 0);
    pe_sender_stop(&s, false);
    pe_sender_join(&s);
    pe_sender_destroy(&s);
    log_destroy(&l);
}

// ---------------------------------------------------------------------------
// Conversions of the injector

static void test_inject_conversions(void) {
    assert(pe_inject_buttons_to_sc(0) == 0);
    assert(pe_inject_buttons_to_sc(PE_BTN_LEFT) == SC_MOUSE_BUTTON_LEFT);
    assert(pe_inject_buttons_to_sc(PE_BTN_RIGHT) == SC_MOUSE_BUTTON_RIGHT);
    assert(pe_inject_buttons_to_sc(PE_BTN_MIDDLE) == SC_MOUSE_BUTTON_MIDDLE);
    assert(pe_inject_buttons_to_sc(PE_BTN_X1 | PE_BTN_X2)
           == (SC_MOUSE_BUTTON_X1 | SC_MOUSE_BUTTON_X2));
    assert(pe_inject_mods_to_sc(PE_HID_LCTRL | PE_HID_RALT)
           == (SC_MOD_LCTRL | SC_MOD_RALT));
    assert(pe_inject_mods_to_sc(0) == 0);
    assert(pe_inject_mods_to_sc(0xFF)
           == (SC_MOD_LCTRL | SC_MOD_LSHIFT | SC_MOD_LALT | SC_MOD_LGUI
             | SC_MOD_RCTRL | SC_MOD_RSHIFT | SC_MOD_RALT | SC_MOD_RGUI));
}

struct fake_mp {
    struct sc_mouse_processor mp;
    int motions;
    int clicks;
    int scrolls;
    int last_xrel;
    int last_yrel;
    uint8_t last_buttons;
    int last_action;
    float last_v;
};

static void
fm_motion(struct sc_mouse_processor *mp,
          const struct sc_mouse_motion_event *e) {
    struct fake_mp *f = container_of(mp, struct fake_mp, mp);
    ++f->motions;
    f->last_xrel = e->xrel;
    f->last_yrel = e->yrel;
    f->last_buttons = e->buttons_state;
}

static void
fm_click(struct sc_mouse_processor *mp, const struct sc_mouse_click_event *e) {
    struct fake_mp *f = container_of(mp, struct fake_mp, mp);
    ++f->clicks;
    f->last_buttons = e->buttons_state;
    f->last_action = e->action;
}

static void
fm_scroll(struct sc_mouse_processor *mp,
          const struct sc_mouse_scroll_event *e) {
    struct fake_mp *f = container_of(mp, struct fake_mp, mp);
    ++f->scrolls;
    f->last_v = e->vscroll;
}

struct fake_kp {
    struct sc_key_processor kp;
    int keys;
    int last_scancode;
    int last_action;
    uint16_t last_mods;
};

static void
fk_key(struct sc_key_processor *kp, const struct sc_key_event *e, uint64_t ack) {
    (void) ack;
    struct fake_kp *f = container_of(kp, struct fake_kp, kp);
    ++f->keys;
    f->last_scancode = e->scancode;
    f->last_action = e->action;
    f->last_mods = e->mods_state;
}

static void test_inject_to_processors(void) {
    static const struct sc_mouse_processor_ops mops = {
        .process_mouse_motion = fm_motion,
        .process_mouse_click = fm_click,
        .process_mouse_scroll = fm_scroll,
    };
    static const struct sc_key_processor_ops kops = {.process_key = fk_key};
    struct fake_mp mp = {.mp = {.ops = &mops, .relative_mode = true}};
    struct fake_kp kp = {.kp = {.ops = &kops}};
    struct pe_inject inj;
    pe_inject_init(&inj, &mp.mp, &kp.kp);

    pe_inject_ops.move(&inj, -7, 9, false, 0);
    assert(mp.motions == 1 && mp.last_xrel == -7 && mp.last_yrel == 9);

    pe_inject_ops.buttons(&inj, PE_BTN_LEFT);
    assert(mp.clicks == 1 && mp.last_buttons == SC_MOUSE_BUTTON_LEFT
           && mp.last_action == SC_ACTION_DOWN);
    // the pressed buttons are carried by the following motions
    pe_inject_ops.move(&inj, 1, 1, false, 0);
    assert(mp.last_buttons == SC_MOUSE_BUTTON_LEFT);
    // no change: no event
    pe_inject_ops.buttons(&inj, PE_BTN_LEFT);
    assert(mp.clicks == 1);
    pe_inject_ops.buttons(&inj, 0);
    assert(mp.clicks == 2 && mp.last_buttons == 0
           && mp.last_action == SC_ACTION_UP);

    pe_inject_ops.wheel(&inj, 0, -2.0f);
    assert(mp.scrolls == 1 && mp.last_v == -2.0f);

    pe_inject_ops.key(&inj, 0x04, true, PE_HID_LSHIFT);
    assert(kp.keys == 1 && kp.last_scancode == 0x04
           && kp.last_action == SC_ACTION_DOWN
           && kp.last_mods == SC_MOD_LSHIFT);
    pe_inject_ops.key(&inj, 0x04, false, 0);
    assert(kp.last_action == SC_ACTION_UP && kp.last_mods == 0);
}

int main(int argc, char *argv[]) {
    (void) argc;
    (void) argv;

    test_order();
    test_drain_releases_on_stop();
    test_stop_without_drain_drops();
    test_pacing();
    test_queue_full_does_not_block();
    test_inject_conversions();
    test_inject_to_processors();
    return 0;
}
