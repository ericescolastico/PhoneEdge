#include "scrcpy.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <SDL3/SDL.h>

#ifdef _WIN32
# include <windows.h>
#endif

#include "controller.h"
#include "events.h"
#include "server.h"
#include "uhid/keyboard_uhid.h"
#include "uhid/mouse_uhid.h"
#include "util/log.h"
#include "util/rand.h"

struct scrcpy {
    struct sc_server server;
    struct sc_controller controller;
    struct sc_keyboard_uhid keyboard_uhid;
    struct sc_mouse_uhid mouse_uhid;
};

#ifdef _WIN32
static BOOL WINAPI
windows_ctrl_handler(DWORD ctrl_type) {
    if (ctrl_type == CTRL_C_EVENT || ctrl_type == CTRL_BREAK_EVENT) {
        sc_push_event(SDL_EVENT_QUIT);
        return TRUE;
    }
    return FALSE;
}

static void
sdl_configure_ctrl_c_windows(void) {
    // Clean up properly on Ctrl+C on Windows
    bool ok = SetConsoleCtrlHandler(windows_ctrl_handler, TRUE);
    if (!ok) {
        LOGW("Could not set Ctrl+C handler");
    }
}
#endif // _WIN32

static enum scrcpy_exit_code
event_loop(void) {
    SDL_Event event;
    while (SDL_WaitEvent(&event)) {
        switch (event.type) {
            case SC_EVENT_DEVICE_DISCONNECTED:
                LOGW("Device disconnected");
                return SCRCPY_EXIT_DISCONNECTED;
            case SC_EVENT_CONTROLLER_ERROR:
                LOGE("Controller error");
                return SCRCPY_EXIT_FAILURE;
            case SDL_EVENT_QUIT:
                LOGD("User requested to quit");
                return SCRCPY_EXIT_SUCCESS;
            default:
                break;
        }
    }
    return SCRCPY_EXIT_FAILURE;
}

// Return true on success, false on error
static bool
await_for_server(bool *connected) {
    SDL_Event event;
    while (SDL_WaitEvent(&event)) {
        switch (event.type) {
            case SDL_EVENT_QUIT:
                *connected = false;
                return true;
            case SC_EVENT_SERVER_CONNECTION_FAILED:
                return false;
            case SC_EVENT_SERVER_CONNECTED:
                *connected = true;
                return true;
            default:
                break;
        }
    }

    LOGE("SDL_WaitEvent() error: %s", SDL_GetError());
    return false;
}

static void
sc_controller_on_ended(struct sc_controller *controller, bool error,
                       void *userdata) {
    // Note: this function may be called twice, once from the controller thread
    // and once from the receiver thread
    (void) controller;
    (void) userdata;

    if (error) {
        sc_push_event(SC_EVENT_CONTROLLER_ERROR);
    } else {
        sc_push_event(SC_EVENT_DEVICE_DISCONNECTED);
    }
}

static void
sc_server_on_connection_failed(struct sc_server *server, void *userdata) {
    (void) server;
    (void) userdata;

    sc_push_event(SC_EVENT_SERVER_CONNECTION_FAILED);
}

static void
sc_server_on_connected(struct sc_server *server, void *userdata) {
    (void) server;
    (void) userdata;

    sc_push_event(SC_EVENT_SERVER_CONNECTED);
}

static void
sc_server_on_disconnected(struct sc_server *server, void *userdata) {
    (void) server;
    (void) userdata;

    LOGD("Server disconnected");
    // Do nothing, the disconnection is handled by the controller
}

// Generate a scrcpy id to differentiate multiple running instances
static uint32_t
scrcpy_generate_scid(void) {
    struct sc_rand rand;
    sc_rand_init(&rand);
    // Only use 31 bits to avoid issues with signed values on the Java-side
    return sc_rand_u32(&rand) & 0x7FFFFFFF;
}

enum scrcpy_exit_code
scrcpy(struct scrcpy_options *options) {
    static struct scrcpy scrcpy;
#ifndef NDEBUG
    // Detect missing initializations
    memset(&scrcpy, 42, sizeof(scrcpy));
#endif
    struct scrcpy *s = &scrcpy;

    // Minimal SDL initialization (events only: there is no window)
    if (!SDL_Init(SDL_INIT_EVENTS)) {
        LOGE("Could not initialize SDL: %s", SDL_GetError());
        return SCRCPY_EXIT_FAILURE;
    }

    atexit(SDL_Quit);

    enum scrcpy_exit_code ret = SCRCPY_EXIT_FAILURE;

    bool server_started = false;
    bool controller_initialized = false;
    bool controller_started = false;

    struct sc_server_params params = {
        .scid = scrcpy_generate_scid(),
        .req_serial = options->serial,
        .log_level = options->log_level,
        .port_range = options->port_range,
        .tunnel_host = options->tunnel_host,
        .tunnel_port = options->tunnel_port,
        .force_adb_forward = options->force_adb_forward,
        .stay_awake = options->stay_awake,
        .kill_adb_on_close = options->kill_adb_on_close,
    };

    static const struct sc_server_callbacks cbs = {
        .on_connection_failed = sc_server_on_connection_failed,
        .on_connected = sc_server_on_connected,
        .on_disconnected = sc_server_on_disconnected,
    };
    if (!sc_server_init(&s->server, &params, &cbs, NULL)) {
        return SCRCPY_EXIT_FAILURE;
    }

#ifdef _WIN32
    sdl_configure_ctrl_c_windows();
#endif

    if (!sc_server_start(&s->server)) {
        goto end;
    }

    server_started = true;

    bool connected;
    if (!await_for_server(&connected)) {
        goto end;
    }
    if (!connected) {
        // The user requested to quit while connecting
        ret = SCRCPY_EXIT_SUCCESS;
        goto end;
    }

    static const struct sc_controller_callbacks controller_cbs = {
        .on_ended = sc_controller_on_ended,
    };

    if (!sc_controller_init(&s->controller, s->server.control_socket,
                            &controller_cbs, NULL)) {
        goto end;
    }
    controller_initialized = true;

    if (!sc_keyboard_uhid_init(&s->keyboard_uhid, &s->controller)) {
        goto end;
    }
    if (!sc_mouse_uhid_init(&s->mouse_uhid, &s->controller)) {
        goto end;
    }

    if (!sc_controller_start(&s->controller)) {
        goto end;
    }
    controller_started = true;

    LOGI("Connected to \"%s\"", s->server.info.device_name);

    ret = event_loop();

end:
    if (controller_started) {
        sc_controller_stop(&s->controller);
    }

    if (server_started) {
        // shutdown the sockets and kill the server
        sc_server_stop(&s->server);
    }

    if (controller_started) {
        sc_controller_join(&s->controller);
    }
    if (controller_initialized) {
        sc_controller_destroy(&s->controller);
    }

    if (server_started) {
        sc_server_join(&s->server);
    }

    sc_server_destroy(&s->server);

    return ret;
}
