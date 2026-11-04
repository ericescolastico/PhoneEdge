#include "scrcpy.h"

#include <assert.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <SDL3/SDL.h>

#ifdef _WIN32
# include <winsock2.h>
# include <windows.h>
#endif

#include "controller.h"
#include "events.h"
#include "pe/pe_app.h"
#include "pe/pe_dryrun.h"
#include "server.h"
#include "uhid/keyboard_uhid.h"
#include "uhid/mouse_uhid.h"
#include "util/log.h"
#include "util/rand.h"
#include "util/tick.h"

struct scrcpy {
    struct sc_server server;
    struct sc_controller controller;
    struct sc_keyboard_uhid keyboard_uhid;
    struct sc_mouse_uhid mouse_uhid;
    struct pe_app pe;
    struct pe_dryrun dryrun;
    atomic_bool pe_running;
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
    struct scrcpy *s = userdata;

    // The phone is gone: give the PC its mouse and keyboard back right now,
    // without waiting for the main loop
    if (atomic_load(&s->pe_running)) {
        pe_app_notify_disconnect(&s->pe);
    }

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

// --dry-run: no phone, nothing sent; only the PC side is exercised
static enum scrcpy_exit_code
run_dry(struct scrcpy_options *options, struct scrcpy *s) {
    pe_dryrun_init(&s->dryrun);

#ifdef _WIN32
    sdl_configure_ctrl_c_windows();
#endif

    struct pe_app_params pe_params = {
        .cfg = &options->edge,
        .script_path = options->script_path,
        .mp = &s->dryrun.mp,
        .kp = &s->dryrun.kp,
        .intr = NULL, // no adb: the phone size comes from the configuration
        .serial = NULL,
    };
    if (!pe_app_start(&s->pe, &pe_params)) {
        return SCRCPY_EXIT_FAILURE;
    }
    LOGI("Modo de teste (--dry-run): nenhum celular é usado. Ctrl+C para sair.");

    enum scrcpy_exit_code ret = event_loop();
    pe_app_stop(&s->pe, true);
    return ret;
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
    bool pe_started = false;
    atomic_init(&scrcpy.pe_running, false);

    if (options->dry_run) {
        return run_dry(options, s);
    }

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
                            &controller_cbs, s)) {
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

    struct pe_app_params pe_params = {
        .cfg = &options->edge,
        .script_path = options->script_path,
        .mp = &s->mouse_uhid.mouse_processor,
        .kp = &s->keyboard_uhid.key_processor,
        .intr = &s->server.intr,
        .serial = s->server.serial,
    };
    if (!pe_app_start(&s->pe, &pe_params)) {
        goto end;
    }
    pe_started = true;
    atomic_store(&s->pe_running, true);

    ret = event_loop();

end:
    if (pe_started) {
        // Give the control back to the PC before anything else. If the phone is
        // still there, release what is held on it and let the messages go out
        // before closing the controller.
        bool phone_alive = ret == SCRCPY_EXIT_SUCCESS;
        atomic_store(&s->pe_running, false);
        pe_app_stop(&s->pe, phone_alive);
        if (phone_alive && controller_started) {
            sc_controller_wait_idle(&s->controller, SC_TICK_FROM_MS(1000));
        }
    }

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
