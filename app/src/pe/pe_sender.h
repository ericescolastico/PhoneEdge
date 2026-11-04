#ifndef PE_SENDER_H
#define PE_SENDER_H

/**
 * Asynchronous, ordered delivery of the actions for the phone.
 *
 * The input hooks of the PC must return in microseconds (Windows disables
 * slow hooks), while injecting input means taking a lock and pushing messages
 * to the control socket. The session therefore only enqueues commands here,
 * and a dedicated thread executes them in order, with the pacing requested by
 * the anchoring sequence.
 *
 * It implements `pe_phone_ops` itself, and forwards to the real one.
 */

#include <stdbool.h>
#include <stdint.h>

#include "pe/pe_session.h"
#include "util/thread.h"

#define PE_SENDER_QUEUE_SIZE 2048

enum pe_cmd_type {
    PE_CMD_MOVE,
    PE_CMD_BUTTONS,
    PE_CMD_WHEEL,
    PE_CMD_KEY,
};

struct pe_cmd {
    enum pe_cmd_type type;
    union {
        struct {
            int dx;
            int dy;
            unsigned delay_ms;
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
            uint8_t mods;
        } key;
    };
};

struct pe_sender {
    struct pe_phone_ops real;
    void *real_ud;

    sc_thread thread;
    sc_mutex mutex;
    sc_cond cond;
    bool stopped;
    bool drain; // when stopping, still execute what is queued
    bool started;

    struct pe_cmd queue[PE_SENDER_QUEUE_SIZE];
    unsigned head; // next to pop
    unsigned count;

    unsigned long dropped;
};

bool
pe_sender_init(struct pe_sender *s, const struct pe_phone_ops *real,
               void *real_ud);

bool
pe_sender_start(struct pe_sender *s);

// Stop the thread. If `drain`, the queued commands are still executed first
// (without pacing): needed to release the keys and buttons held on the phone
// when exiting. Otherwise (the phone is gone), they are dropped.
void
pe_sender_stop(struct pe_sender *s, bool drain);

void
pe_sender_join(struct pe_sender *s);

void
pe_sender_destroy(struct pe_sender *s);

// The `pe_phone_ops` to give to the session (with the sender as `ud`)
extern const struct pe_phone_ops pe_sender_ops;

#endif
