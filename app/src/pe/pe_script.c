#include "pe_script.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_timer.h>

#include "util/log.h"
#include "util/tick.h"

static bool
parse_int(const char *s, long *out) {
    char *end;
    errno = 0;
    long v = strtol(s, &end, 0);
    if (errno || end == s || *end) {
        return false;
    }
    *out = v;
    return true;
}

static uint8_t
parse_button(const char *s) {
    if (!strcmp(s, "left")) return PE_BTN_LEFT;
    if (!strcmp(s, "right")) return PE_BTN_RIGHT;
    if (!strcmp(s, "middle")) return PE_BTN_MIDDLE;
    if (!strcmp(s, "x1")) return PE_BTN_X1;
    if (!strcmp(s, "x2")) return PE_BTN_X2;
    return 0;
}

bool
pe_script_run_line(struct pe_session *session, uint64_t *clock, char *line) {
    char *argv[8];
    int argc = 0;

    char *hash = strchr(line, '#');
    if (hash) {
        *hash = '\0';
    }
    char *save = NULL;
    for (char *tok = strtok_r(line, " \t\r\n", &save); tok;
            tok = strtok_r(NULL, " \t\r\n", &save)) {
        if (argc == 8) {
            return false;
        }
        argv[argc++] = tok;
    }
    if (!argc) {
        return true;
    }

    const char *cmd = argv[0];
    long v[4];

    if (!strcmp(cmd, "monitor") && argc == 5) {
        for (int i = 0; i < 4; ++i) {
            if (!parse_int(argv[1 + i], &v[i])) {
                return false;
            }
        }
        struct pe_rect r = {(int32_t) v[0], (int32_t) v[1], (int32_t) v[2],
                            (int32_t) v[3]};
        pe_session_set_monitors(session, &r, 1, 0);
        return true;
    }
    if (!strcmp(cmd, "move") && argc == 5) {
        for (int i = 0; i < 4; ++i) {
            if (!parse_int(argv[1 + i], &v[i])) {
                return false;
            }
        }
        bool s = pe_session_on_mouse_move(session, *clock, (int) v[0],
                                          (int) v[1], (int) v[2], (int) v[3]);
        LOGD("script: move %ld %ld -> swallow=%d", v[0], v[1], s);
        return true;
    }
    if ((!strcmp(cmd, "down") || !strcmp(cmd, "up")) && argc == 2) {
        uint8_t b = parse_button(argv[1]);
        if (!b) {
            return false;
        }
        bool s = pe_session_on_button(session, *clock, b, cmd[0] == 'd');
        LOGD("script: %s %s -> swallow=%d", cmd, argv[1], s);
        return true;
    }
    if ((!strcmp(cmd, "wheel") || !strcmp(cmd, "hwheel")) && argc == 2) {
        if (!parse_int(argv[1], &v[0])) {
            return false;
        }
        bool s = pe_session_on_wheel(session, *clock, cmd[0] == 'h',
                                     (int) v[0]);
        LOGD("script: %s %ld -> swallow=%d", cmd, v[0], s);
        return true;
    }
    if (!strcmp(cmd, "key") && argc == 3) {
        bool down = !strcmp(argv[1], "down");
        if ((!down && strcmp(argv[1], "up")) || !parse_int(argv[2], &v[0])
                || v[0] <= 0 || v[0] > 255) {
            return false;
        }
        bool s = pe_session_on_key(session, *clock, (uint8_t) v[0], down);
        LOGD("script: key %s 0x%02lx -> swallow=%d", argv[1], v[0], s);
        return true;
    }
    if (!strcmp(cmd, "sleep") && argc == 2) {
        if (!parse_int(argv[1], &v[0]) || v[0] < 0) {
            return false;
        }
        *clock += (uint64_t) v[0];
        // Really wait, so that the pacing of the sender is exercised too
        SDL_Delay((Uint32) v[0]);
        return true;
    }
    if (!strcmp(cmd, "tick") && argc == 1) {
        pe_session_on_tick(session, *clock);
        return true;
    }
    if (!strcmp(cmd, "disconnect") && argc == 1) {
        pe_session_disconnect(session, *clock);
        return true;
    }
    if (!strcmp(cmd, "release") && argc == 1) {
        pe_session_release(session, *clock);
        return true;
    }
    if (!strcmp(cmd, "quit") && argc == 1) {
        // Ask the application to exit, like Ctrl+C
        SDL_Event event = {.type = SDL_EVENT_QUIT};
        SDL_PushEvent(&event);
        return true;
    }
    if (!strcmp(cmd, "state") && argc == 1) {
        LOGI("script: phone_active=%d",
             pe_session_is_phone_active(session) ? 1 : 0);
        return true;
    }
    return false;
}

static int
run_script(void *data) {
    struct pe_script *sc = data;

    FILE *f = fopen(sc->path, "r");
    if (!f) {
        LOGE("Could not open the script %s: %s", sc->path, strerror(errno));
        return 1;
    }

    // Default monitor, if the script does not declare one
    struct pe_rect mon = {0, 0, 1920, 1080};
    pe_session_set_monitors(sc->session, &mon, 1, 0);

    uint64_t clock = 1000;
    char line[256];
    unsigned lineno = 0;
    while (!atomic_load(&sc->stop) && fgets(line, sizeof(line), f)) {
        ++lineno;
        if (!pe_script_run_line(sc->session, &clock, line)) {
            LOGE("%s:%u: invalid command", sc->path, lineno);
            break;
        }
    }
    fclose(f);
    LOGI("Script finished");
    return 0;
}

bool
pe_script_init(struct pe_script *sc, struct pe_session *session,
               const char *path) {
    sc->session = session;
    sc->path = strdup(path);
    sc->started = false;
    atomic_init(&sc->stop, false);
    return sc->path != NULL;
}

bool
pe_script_start(struct pe_script *sc) {
    if (!sc_thread_create(&sc->thread, run_script, "pe-script", sc)) {
        return false;
    }
    sc->started = true;
    return true;
}

void
pe_script_stop(struct pe_script *sc) {
    atomic_store(&sc->stop, true);
}

void
pe_script_join(struct pe_script *sc) {
    if (sc->started) {
        sc_thread_join(&sc->thread, NULL);
        sc->started = false;
    }
}

void
pe_script_destroy(struct pe_script *sc) {
    free(sc->path);
}
