#include "pe_sender.h"

#include "util/log.h"
#include "util/tick.h"

bool
pe_sender_init(struct pe_sender *s, const struct pe_phone_ops *real,
               void *real_ud) {
    s->real = *real;
    s->real_ud = real_ud;
    s->stopped = false;
    s->drain = false;
    s->started = false;
    s->head = 0;
    s->count = 0;
    s->dropped = 0;

    if (!sc_mutex_init(&s->mutex)) {
        return false;
    }
    if (!sc_cond_init(&s->cond)) {
        sc_mutex_destroy(&s->mutex);
        return false;
    }
    return true;
}

void
pe_sender_destroy(struct pe_sender *s) {
    sc_cond_destroy(&s->cond);
    sc_mutex_destroy(&s->mutex);
}

static void
push(struct pe_sender *s, const struct pe_cmd *cmd) {
    sc_mutex_lock(&s->mutex);
    if (s->stopped && !s->drain) {
        sc_mutex_unlock(&s->mutex);
        return;
    }
    if (s->count == PE_SENDER_QUEUE_SIZE) {
        // The phone does not follow: do not block the PC input hooks
        ++s->dropped;
        if (s->dropped == 1 || s->dropped % 1000 == 0) {
            LOGW("Phone input queue full, dropping events (%lu so far)",
                 s->dropped);
        }
        sc_mutex_unlock(&s->mutex);
        return;
    }
    s->queue[(s->head + s->count) % PE_SENDER_QUEUE_SIZE] = *cmd;
    ++s->count;
    sc_cond_signal(&s->cond);
    sc_mutex_unlock(&s->mutex);
}

static void
op_move(void *ud, int dx, int dy, bool placement, unsigned delay_ms) {
    (void) placement;
    struct pe_cmd cmd = {
        .type = PE_CMD_MOVE,
        .move = {dx, dy, delay_ms},
    };
    push(ud, &cmd);
}

static void
op_buttons(void *ud, uint8_t buttons) {
    struct pe_cmd cmd = {
        .type = PE_CMD_BUTTONS,
        .buttons = {buttons},
    };
    push(ud, &cmd);
}

static void
op_wheel(void *ud, float h, float v) {
    struct pe_cmd cmd = {
        .type = PE_CMD_WHEEL,
        .wheel = {h, v},
    };
    push(ud, &cmd);
}

static void
op_key(void *ud, uint8_t scancode, bool down, uint8_t mods) {
    struct pe_cmd cmd = {
        .type = PE_CMD_KEY,
        .key = {scancode, down, mods},
    };
    push(ud, &cmd);
}

const struct pe_phone_ops pe_sender_ops = {
    .move = op_move,
    .buttons = op_buttons,
    .wheel = op_wheel,
    .key = op_key,
};

static void
execute(struct pe_sender *s, const struct pe_cmd *cmd) {
    switch (cmd->type) {
        case PE_CMD_MOVE:
            s->real.move(s->real_ud, cmd->move.dx, cmd->move.dy, false, 0);
            break;
        case PE_CMD_BUTTONS:
            s->real.buttons(s->real_ud, cmd->buttons.buttons);
            break;
        case PE_CMD_WHEEL:
            s->real.wheel(s->real_ud, cmd->wheel.horizontal,
                          cmd->wheel.vertical);
            break;
        case PE_CMD_KEY:
            s->real.key(s->real_ud, cmd->key.scancode, cmd->key.down,
                        cmd->key.mods);
            break;
    }
}

static int
run_sender(void *data) {
    struct pe_sender *s = data;

    sc_mutex_lock(&s->mutex);
    for (;;) {
        while (!s->stopped && !s->count) {
            sc_cond_wait(&s->cond, &s->mutex);
        }
        if (s->stopped && (!s->drain || !s->count)) {
            break;
        }

        struct pe_cmd cmd = s->queue[s->head];
        s->head = (s->head + 1) % PE_SENDER_QUEUE_SIZE;
        --s->count;

        // Do not hold the lock while injecting
        sc_mutex_unlock(&s->mutex);
        execute(s, &cmd);
        sc_mutex_lock(&s->mutex);

        if (cmd.type == PE_CMD_MOVE && cmd.move.delay_ms && !s->stopped) {
            // Pacing of the anchoring/positioning sequence: interruptible
            sc_tick deadline = sc_tick_now() + SC_TICK_FROM_MS(cmd.move.delay_ms);
            bool signaled = true;
            while (!s->stopped && signaled) {
                signaled = sc_cond_timedwait(&s->cond, &s->mutex, deadline);
            }
        }
    }
    sc_mutex_unlock(&s->mutex);
    return 0;
}

bool
pe_sender_start(struct pe_sender *s) {
    if (!sc_thread_create(&s->thread, run_sender, "pe-sender", s)) {
        LOGE("Could not start the phone input thread");
        return false;
    }
    s->started = true;
    return true;
}

void
pe_sender_stop(struct pe_sender *s, bool drain) {
    sc_mutex_lock(&s->mutex);
    s->stopped = true;
    s->drain = drain;
    if (!drain) {
        s->count = 0;
    }
    sc_cond_signal(&s->cond);
    sc_mutex_unlock(&s->mutex);
}

void
pe_sender_join(struct pe_sender *s) {
    if (s->started) {
        sc_thread_join(&s->thread, NULL);
        s->started = false;
    }
}
