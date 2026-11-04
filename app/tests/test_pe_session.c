#include "common.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "pe/pe_monitors.h"
#include "pe/pe_script.h"
#include "pe/pe_session.h"

struct rec {
    int moves;
    int placement_moves;
    int sum_dx;
    int sum_dy;
    int buttons_calls;
    uint8_t buttons;
    int keys_down;
    int keys_up;
    uint8_t last_mods;
    float wheel_v;
    int cursor_calls;
    int cursor_x;
    int cursor_y;
};

static void
r_move(void *ud, int dx, int dy, bool placement, unsigned delay) {
    (void) delay;
    struct rec *r = ud;
    ++r->moves;
    r->placement_moves += placement;
    r->sum_dx += dx;
    r->sum_dy += dy;
}

static void
r_buttons(void *ud, uint8_t b) {
    struct rec *r = ud;
    ++r->buttons_calls;
    r->buttons = b;
}

static void
r_wheel(void *ud, float h, float v) {
    (void) h;
    struct rec *r = ud;
    r->wheel_v += v;
}

static void
r_key(void *ud, uint8_t sc, bool down, uint8_t mods) {
    (void) sc;
    struct rec *r = ud;
    if (down) {
        ++r->keys_down;
    } else {
        ++r->keys_up;
    }
    r->last_mods = mods;
}

static void
r_cursor(void *ud, int x, int y) {
    struct rec *r = ud;
    ++r->cursor_calls;
    r->cursor_x = x;
    r->cursor_y = y;
}

static struct pe_session *
make_session(struct rec *rec) {
    static struct pe_session s;
    static const struct pe_phone_ops phone = {r_move, r_buttons, r_wheel,
                                              r_key};
    static const struct pe_pc_ops pc = {r_cursor};
    struct pe_config cfg;
    pe_config_defaults(&cfg);
    cfg.phone_size_auto = false;
    memset(rec, 0, sizeof(*rec));
    bool ok = pe_session_init(&s, &cfg, &phone, rec, &pc, rec);
    assert(ok);
    struct pe_rect mon = {0, 0, 1920, 1080};
    pe_session_set_monitors(&s, &mon, 1, 0);
    return &s;
}

static void
test_flow(void) {
    struct rec rec;
    struct pe_session *s = make_session(&rec);

    assert(!pe_session_is_phone_active(s));
    assert(!pe_session_on_mouse_move(s, 1000, 5, 0, 1919, 540));
    // second push: enters, the flag is updated and the event swallowed
    assert(pe_session_on_mouse_move(s, 1001, 20, 0, 1919, 540));
    assert(pe_session_is_phone_active(s));
    assert(rec.placement_moves > 15);
    assert(rec.moves == rec.placement_moves);

    // input goes to the phone ops
    assert(pe_session_on_button(s, 1500, PE_BTN_LEFT, true));
    assert(rec.buttons == PE_BTN_LEFT);
    assert(pe_session_on_wheel(s, 1501, false, -120));
    assert(rec.wheel_v == -1.0f);
    assert(pe_session_on_key(s, 1502, 0xE0, true));
    assert(pe_session_on_key(s, 1503, 0x06, true));
    assert(rec.keys_down == 2 && rec.last_mods == PE_HID_LCTRL);

    // emergency shortcut: swallow, releases, cursor restored
    assert(pe_session_on_key(s, 1504, 0xE2, true));
    assert(pe_session_on_key(s, 1505, PE_SC_PAUSE, true));
    assert(!pe_session_is_phone_active(s));
    assert(rec.buttons == 0);
    assert(rec.keys_up == 3);
    assert(rec.cursor_calls == 1);
    assert(rec.cursor_x == 1916);
    assert(abs(rec.cursor_y - 540) < 3);
    assert(s->entries == 1 && s->leaves == 1);

    pe_session_destroy(s);
}

static void
test_disconnect_and_release(void) {
    struct rec rec;
    struct pe_session *s = make_session(&rec);
    pe_session_on_mouse_move(s, 1000, 20, 0, 1919, 540);
    pe_session_on_mouse_move(s, 1001, 20, 0, 1919, 540);
    assert(pe_session_is_phone_active(s));
    pe_session_on_button(s, 1500, PE_BTN_LEFT, true);
    int buttons_calls = rec.buttons_calls;

    pe_session_disconnect(s, 1600);
    assert(!pe_session_is_phone_active(s));
    assert(atomic_load(&s->disabled));
    // nothing was sent to the dead phone, but the PC cursor is back
    assert(rec.buttons_calls == buttons_calls);
    assert(rec.cursor_calls == 1);

    // the PC keeps everything
    assert(!pe_session_on_mouse_move(s, 9000, 50, 0, 1919, 540));
    assert(!pe_session_is_phone_active(s));
    pe_session_destroy(s);

    s = make_session(&rec);
    pe_session_on_mouse_move(s, 1000, 20, 0, 1919, 540);
    pe_session_on_mouse_move(s, 1001, 20, 0, 1919, 540);
    pe_session_on_button(s, 1500, PE_BTN_LEFT, true);
    pe_session_release(s, 1600);
    assert(rec.buttons == 0);
    assert(!pe_session_is_phone_active(s));
    pe_session_destroy(s);
}

static void
test_monitors_sort(void) {
    struct pe_monitor_info in[4] = {
        {{1920, 0, 3840, 1080}, false},
        {{-1920, 0, 0, 1080}, false},
        {{0, 0, 1920, 1080}, true},
        {{1920, 1080, 3840, 2160}, false},
    };
    struct pe_monitor_info out[4];
    pe_monitors_sort(in, out, 4);
    // primary first, then left to right, top to bottom
    assert(out[0].primary && out[0].rect.left == 0);
    assert(out[1].rect.left == -1920);
    assert(out[2].rect.left == 1920 && out[2].rect.top == 0);
    assert(out[3].rect.left == 1920 && out[3].rect.top == 1080);

    pe_monitors_sort(in, out, 0);
    pe_monitors_sort(in, out, 1);
    assert(out[0].rect.left == 1920);
}

static void
test_script_lines(void) {
    struct rec rec;
    struct pe_session *s = make_session(&rec);
    uint64_t clock = 1000;
    char l1[] = "move 20 0 1919 540  # comment";
    char l2[] = "move 20 0 1919 540";
    char l3[] = "down left";
    char l4[] = "key down 0x04";
    char l5[] = "wheel -240";
    char l6[] = "state";
    char l7[] = "";
    char l8[] = "   # only a comment";
    assert(pe_script_run_line(s, &clock, l1));
    assert(pe_script_run_line(s, &clock, l2));
    assert(pe_session_is_phone_active(s));
    assert(pe_script_run_line(s, &clock, l3));
    assert(rec.buttons == PE_BTN_LEFT);
    assert(pe_script_run_line(s, &clock, l4));
    assert(rec.keys_down == 1);
    assert(pe_script_run_line(s, &clock, l5));
    assert(rec.wheel_v == -2.0f);
    assert(pe_script_run_line(s, &clock, l6));
    assert(pe_script_run_line(s, &clock, l7));
    assert(pe_script_run_line(s, &clock, l8));

    // errors
    char b1[] = "move 1 2 3";
    char b2[] = "down purple";
    char b3[] = "key down 0";
    char b4[] = "key sideways 0x04";
    char b5[] = "bogus";
    char b6[] = "move a b c d";
    char b7[] = "sleep -5";
    assert(!pe_script_run_line(s, &clock, b1));
    assert(!pe_script_run_line(s, &clock, b2));
    assert(!pe_script_run_line(s, &clock, b3));
    assert(!pe_script_run_line(s, &clock, b4));
    assert(!pe_script_run_line(s, &clock, b5));
    assert(!pe_script_run_line(s, &clock, b6));
    assert(!pe_script_run_line(s, &clock, b7));

    char sl[] = "sleep 5";
    assert(pe_script_run_line(s, &clock, sl));
    assert(clock == 1005);
    pe_session_destroy(s);
}

int main(int argc, char *argv[]) {
    (void) argc;
    (void) argv;

    test_flow();
    test_disconnect_and_release();
    test_monitors_sort();
    test_script_lines();
    return 0;
}
