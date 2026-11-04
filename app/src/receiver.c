#include "receiver.h"

#include <assert.h>
#include <inttypes.h>
#include <stdlib.h>

#include "device_msg.h"
#include "util/log.h"
#include "util/thread.h"

bool
sc_receiver_init(struct sc_receiver *receiver, sc_socket control_socket,
                 const struct sc_receiver_callbacks *cbs, void *cbs_userdata) {
    bool ok = sc_mutex_init(&receiver->mutex);
    if (!ok) {
        return false;
    }

    receiver->control_socket = control_socket;

    assert(cbs && cbs->on_ended);
    receiver->cbs = cbs;
    receiver->cbs_userdata = cbs_userdata;

    return true;
}

void
sc_receiver_destroy(struct sc_receiver *receiver) {
    sc_mutex_destroy(&receiver->mutex);
}

static void
process_msg(struct sc_receiver *receiver, struct sc_device_msg *msg) {
    (void) receiver;

    // PhoneEdge only sends input to the device: the clipboard is not
    // synchronized (clipboard_autosync=false) and HID outputs (keyboard LEDs)
    // are ignored, so just log and release the message.
    switch (msg->type) {
        case DEVICE_MSG_TYPE_CLIPBOARD:
        case DEVICE_MSG_TYPE_ACK_CLIPBOARD:
            LOGD("Ignoring clipboard message from the device");
            break;
        case DEVICE_MSG_TYPE_UHID_OUTPUT:
            LOGV("Ignoring UHID output [%" PRIu16 "] size=%" PRIu16,
                 msg->uhid_output.id, msg->uhid_output.size);
            break;
    }

    sc_device_msg_destroy(msg);
}

static ssize_t
process_msgs(struct sc_receiver *receiver, const uint8_t *buf, size_t len) {
    size_t head = 0;
    for (;;) {
        struct sc_device_msg msg;
        ssize_t r = sc_device_msg_deserialize(&buf[head], len - head, &msg);
        if (r == -1) {
            return -1;
        }
        if (r == 0) {
            return head;
        }

        process_msg(receiver, &msg);
        // the device msg must be destroyed by process_msg()

        head += r;
        assert(head <= len);
        if (head == len) {
            return head;
        }
    }
}

static int
run_receiver(void *data) {
    struct sc_receiver *receiver = data;

    static uint8_t buf[DEVICE_MSG_MAX_SIZE];
    size_t head = 0;

    bool error = false;

    for (;;) {
        assert(head < DEVICE_MSG_MAX_SIZE);
        ssize_t r = net_recv(receiver->control_socket, buf + head,
                             DEVICE_MSG_MAX_SIZE - head);
        if (r <= 0) {
            LOGD("Receiver stopped");
            // device disconnected: keep error=false
            break;
        }

        head += r;
        ssize_t consumed = process_msgs(receiver, buf, head);
        if (consumed == -1) {
            // an error occurred
            error = true;
            break;
        }

        if (consumed) {
            head -= consumed;
            // shift the remaining data in the buffer
            memmove(buf, &buf[consumed], head);
        }
    }

    receiver->cbs->on_ended(receiver, error, receiver->cbs_userdata);

    return 0;
}

bool
sc_receiver_start(struct sc_receiver *receiver) {
    LOGD("Starting receiver thread");

    bool ok = sc_thread_create(&receiver->thread, run_receiver,
                               "scrcpy-receiver", receiver);
    if (!ok) {
        LOGE("Could not start receiver thread");
        return false;
    }

    return true;
}

void
sc_receiver_join(struct sc_receiver *receiver) {
    sc_thread_join(&receiver->thread, NULL);
}
