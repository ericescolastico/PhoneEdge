#include "pe_dryrun.h"

#include "util/log.h"

static void
mouse_motion(struct sc_mouse_processor *mp,
             const struct sc_mouse_motion_event *event) {
    (void) mp;
    LOGV("(dry-run) mouse motion %d,%d", (int) event->xrel, (int) event->yrel);
}

static void
mouse_click(struct sc_mouse_processor *mp,
            const struct sc_mouse_click_event *event) {
    (void) mp;
    LOGD("(dry-run) mouse buttons 0x%02x", event->buttons_state);
}

static void
mouse_scroll(struct sc_mouse_processor *mp,
             const struct sc_mouse_scroll_event *event) {
    (void) mp;
    LOGV("(dry-run) wheel %.2f,%.2f", event->hscroll, event->vscroll);
}

static void
process_key(struct sc_key_processor *kp, const struct sc_key_event *event,
            uint64_t ack_to_wait) {
    (void) kp;
    (void) ack_to_wait;
    LOGV("(dry-run) key 0x%02x %s", (unsigned) event->scancode,
         event->action == SC_ACTION_DOWN ? "down" : "up");
}

void
pe_dryrun_init(struct pe_dryrun *d) {
    static const struct sc_mouse_processor_ops mouse_ops = {
        .process_mouse_motion = mouse_motion,
        .process_mouse_click = mouse_click,
        .process_mouse_scroll = mouse_scroll,
        .process_touch = NULL,
    };
    static const struct sc_key_processor_ops key_ops = {
        .process_key = process_key,
        .process_text = NULL,
    };
    d->mp.ops = &mouse_ops;
    d->mp.relative_mode = true;
    d->kp.ops = &key_ops;
    d->kp.async_paste = false;
    d->kp.hid = true;
}
