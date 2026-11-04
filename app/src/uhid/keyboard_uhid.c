#include "keyboard_uhid.h"

#include <assert.h>
#include <string.h>

#include "util/log.h"
#include "util/thread.h"

/** Downcast key processor to keyboard_uhid */
#define DOWNCAST(KP) container_of(KP, struct sc_keyboard_uhid, key_processor)

static void
sc_keyboard_uhid_send_input(struct sc_keyboard_uhid *kb,
                            const struct sc_hid_input *hid_input) {
    struct sc_control_msg msg;
    msg.type = SC_CONTROL_MSG_TYPE_UHID_INPUT;
    msg.uhid_input.id = hid_input->hid_id;

    assert(hid_input->size <= SC_HID_MAX_SIZE);
    memcpy(msg.uhid_input.data, hid_input->data, hid_input->size);
    msg.uhid_input.size = hid_input->size;

    if (!sc_controller_push_msg(kb->controller, &msg)) {
        LOGE("Could not push UHID_INPUT message (key)");
    }
}

static void
sc_key_processor_process_key(struct sc_key_processor *kp,
                             const struct sc_key_event *event,
                             uint64_t ack_to_wait) {
    (void) ack_to_wait;

    // This may be called from any thread, but never concurrently

    if (event->repeat) {
        // In USB HID protocol, key repeat is handled by the host (Android), so
        // just ignore key repeat here.
        return;
    }

    struct sc_keyboard_uhid *kb = DOWNCAST(kp);

    struct sc_hid_input hid_input;

    // Not all keys are supported, just ignore unsupported keys
    if (sc_hid_keyboard_generate_input_from_key(&kb->hid, &hid_input, event)) {
        sc_keyboard_uhid_send_input(kb, &hid_input);
    }
}

bool
sc_keyboard_uhid_init(struct sc_keyboard_uhid *kb,
                      struct sc_controller *controller) {
    sc_hid_keyboard_init(&kb->hid);

    kb->controller = controller;

    static const struct sc_key_processor_ops ops = {
        .process_key = sc_key_processor_process_key,
        // Never forward text input via HID (all the keys are injected
        // separately)
        .process_text = NULL,
    };

    // Clipboard synchronization is requested over the same control socket, so
    // there is no need for a specific synchronization mechanism
    kb->key_processor.async_paste = false;
    kb->key_processor.hid = true;
    kb->key_processor.ops = &ops;

    struct sc_hid_open hid_open;
    sc_hid_keyboard_generate_open(&hid_open);
    assert(hid_open.hid_id == SC_HID_ID_KEYBOARD);

    struct sc_control_msg msg;
    msg.type = SC_CONTROL_MSG_TYPE_UHID_CREATE;
    msg.uhid_create.id = SC_HID_ID_KEYBOARD;
    msg.uhid_create.vendor_id = 0;
    msg.uhid_create.product_id = 0;
    msg.uhid_create.name = NULL;
    msg.uhid_create.report_desc = hid_open.report_desc;
    msg.uhid_create.report_desc_size = hid_open.report_desc_size;
    if (!sc_controller_push_msg(controller, &msg)) {
        LOGE("Could not send UHID_CREATE message (keyboard)");
        return false;
    }

    return true;
}
