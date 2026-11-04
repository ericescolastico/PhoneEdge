#include "server.h"

#include <assert.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

#include "adb/adb.h"
#include "util/env.h"
#include "util/file.h"
#include "util/log.h"
#include "util/net_intr.h"
#include "util/process.h"
#include "util/str.h"

#define SC_SERVER_FILENAME "scrcpy-server"

#define SC_SERVER_PATH_DEFAULT PREFIX "/share/phoneedge/" SC_SERVER_FILENAME
#define SC_DEVICE_SERVER_PATH "/data/local/tmp/scrcpy-server.jar"

#define SC_ADB_PORT_DEFAULT 5555
#define SC_SOCKET_NAME_PREFIX "scrcpy_"

static char *
get_server_path(void) {
    char *server_path = sc_get_env("SCRCPY_SERVER_PATH");
    if (server_path) {
        // if the envvar is set, use it
        LOGD("Using SCRCPY_SERVER_PATH: %s", server_path);
        return server_path;
    }

#ifndef PORTABLE
    LOGD("Using server: " SC_SERVER_PATH_DEFAULT);
    server_path = strdup(SC_SERVER_PATH_DEFAULT);
    if (!server_path) {
        LOG_OOM();
        return NULL;
    }
#else
    server_path = sc_file_get_local_path(SC_SERVER_FILENAME);
    if (!server_path) {
        LOGE("Could not get local file path, "
             "using " SC_SERVER_FILENAME " from current directory");
        return strdup(SC_SERVER_FILENAME);
    }

    LOGD("Using server (portable): %s", server_path);
#endif

    return server_path;
}

static bool
push_server(struct sc_intr *intr, const char *serial) {
    char *server_path = get_server_path();
    if (!server_path) {
        return false;
    }
    if (!sc_file_is_regular(server_path)) {
        LOGE("'%s' does not exist or is not a regular file\n", server_path);
        free(server_path);
        return false;
    }
    bool ok = sc_adb_push(intr, serial, server_path, SC_DEVICE_SERVER_PATH, 0);
    free(server_path);
    return ok;
}

static const char *
log_level_to_server_string(enum sc_log_level level) {
    switch (level) {
        case SC_LOG_LEVEL_VERBOSE:
            return "verbose";
        case SC_LOG_LEVEL_DEBUG:
            return "debug";
        case SC_LOG_LEVEL_INFO:
            return "info";
        case SC_LOG_LEVEL_WARN:
            return "warn";
        case SC_LOG_LEVEL_ERROR:
            return "error";
        default:
            assert(!"unexpected log level");
            return NULL;
    }
}

static bool
sc_server_sleep(struct sc_server *server, sc_tick deadline) {
    sc_mutex_lock(&server->mutex);
    bool timed_out = false;
    while (!server->stopped && !timed_out) {
        timed_out = !sc_cond_timedwait(&server->cond_stopped,
                                       &server->mutex, deadline);
    }
    bool stopped = server->stopped;
    sc_mutex_unlock(&server->mutex);

    return !stopped;
}

static sc_pid
execute_server(struct sc_server *server,
               const struct sc_server_params *params) {
    sc_pid pid = SC_PROCESS_NONE;

    const char *serial = server->serial;
    assert(serial);

    const char *cmd[128];
    unsigned count = 0;
    cmd[count++] = sc_adb_get_executable();
    cmd[count++] = "-s";
    cmd[count++] = serial;
    cmd[count++] = "shell";
    cmd[count++] = "CLASSPATH=" SC_DEVICE_SERVER_PATH;
    cmd[count++] = "app_process";

#ifdef SERVER_DEBUGGER
    uint16_t sdk_version = sc_adb_get_device_sdk_version(&server->intr, serial);
    if (!sdk_version) {
        LOGE("Could not determine SDK version");
        return 0;
    }

# define SERVER_DEBUGGER_PORT "5005"
    const char *dbg;
    if (sdk_version < 28) {
        // Android < 9
        dbg = "-agentlib:jdwp=transport=dt_socket,suspend=y,server=y,address="
              SERVER_DEBUGGER_PORT;
    } else if (sdk_version < 30) {
        // Android >= 9 && Android < 11
        dbg = "-XjdwpProvider:internal -XjdwpOptions:transport=dt_socket,"
              "suspend=y,server=y,address=" SERVER_DEBUGGER_PORT;
    } else {
        // Android >= 11
        // Contrary to the other methods, this does not suspend on start.
        // <https://github.com/Genymobile/scrcpy/pull/5466>
        dbg = "-XjdwpProvider:adbconnection";
    }
    cmd[count++] = dbg;
#endif

    cmd[count++] = "/"; // unused
    cmd[count++] = "com.genymobile.scrcpy.Server";
    // The prebuilt server rejects a client of a different version, so this is
    // the version of the scrcpy-server, not the one of PhoneEdge
    cmd[count++] = SCRCPY_SERVER_VERSION;

    unsigned dyn_idx = count; // from there, the strings are allocated
#define ADD_PARAM(fmt, ...) do { \
        char *p; \
        if (asprintf(&p, fmt, ## __VA_ARGS__) == -1) { \
            goto end; \
        } \
        cmd[count++] = p; \
    } while(0)

    ADD_PARAM("scid=%08x", params->scid);
    ADD_PARAM("log_level=%s", log_level_to_server_string(params->log_level));

    // Control only: no video, no audio
    ADD_PARAM("video=false");
    ADD_PARAM("audio=false");
    ADD_PARAM("clipboard_autosync=false");
    if (server->tunnel.forward) {
        ADD_PARAM("tunnel_forward=true");
    }
    if (params->stay_awake) {
        ADD_PARAM("stay_awake=true");
    }

#undef ADD_PARAM

    cmd[count++] = NULL;

#ifdef SERVER_DEBUGGER
    LOGI("Server debugger listening%s...",
         sdk_version < 30 ? " on port " SERVER_DEBUGGER_PORT : "");
    // For Android < 11, from the computer:
    //     - run `adb forward tcp:5005 tcp:5005`
    // For Android >= 11:
    //     - execute `adb jdwp` to get the jdwp port
    //     - run `adb forward tcp:5005 jdwp:XXXX` (replace XXXX)
    //
    // Then, from Android Studio: Run > Debug > Edit configurations...
    // On the left, click on '+', "Remote", with:
    //     Host: localhost
    //     Port: 5005
    // Then click on "Debug"
#endif
    // Inherit both stdout and stderr (all server logs are printed to stdout)
    pid = sc_adb_execute(cmd, 0);

end:
    for (unsigned i = dyn_idx; i < count; ++i) {
        free((char *) cmd[i]);
    }

    return pid;
}

static bool
connect_and_read_byte(struct sc_intr *intr, sc_socket socket,
                      uint32_t tunnel_host, uint16_t tunnel_port) {
    bool ok = net_connect_intr(intr, socket, tunnel_host, tunnel_port);
    if (!ok) {
        return false;
    }

    char byte;
    // the connection may succeed even if the server behind the "adb tunnel"
    // is not listening, so read one byte to detect a working connection
    if (net_recv_intr(intr, socket, &byte, 1) != 1) {
        // the server is not listening yet behind the adb tunnel
        return false;
    }

    return true;
}

static sc_socket
connect_to_server(struct sc_server *server, unsigned attempts, sc_tick delay,
                  uint32_t host, uint16_t port) {
    do {
        LOGD("Remaining connection attempts: %u", attempts);
        sc_socket socket = net_socket();
        if (socket != SC_SOCKET_NONE) {
            bool ok = connect_and_read_byte(&server->intr, socket, host, port);
            if (ok) {
                // it worked!
                return socket;
            }

            net_close(socket);
        }

        if (sc_intr_is_interrupted(&server->intr)) {
            // Stop immediately
            break;
        }

        if (attempts) {
            sc_tick deadline = sc_tick_now() + delay;
            bool ok = sc_server_sleep(server, deadline);
            if (!ok) {
                LOGI("Connection attempt stopped");
                break;
            }
        }
    } while (--attempts);
    return SC_SOCKET_NONE;
}

bool
sc_server_init(struct sc_server *server, const struct sc_server_params *params,
              const struct sc_server_callbacks *cbs, void *cbs_userdata) {
    // The allocated data in params (const char *) must remain valid until the
    // end of the program
    server->params = *params;

    bool ok = sc_adb_init();
    if (!ok) {
        return false;
    }

    ok = sc_mutex_init(&server->mutex);
    if (!ok) {
        sc_adb_destroy();
        return false;
    }

    ok = sc_cond_init(&server->cond_stopped);
    if (!ok) {
        sc_mutex_destroy(&server->mutex);
        sc_adb_destroy();
        return false;
    }

    ok = sc_intr_init(&server->intr);
    if (!ok) {
        sc_cond_destroy(&server->cond_stopped);
        sc_mutex_destroy(&server->mutex);
        sc_adb_destroy();
        return false;
    }

    server->serial = NULL;
    server->device_socket_name = NULL;
    server->stopped = false;

    server->control_socket = SC_SOCKET_NONE;

    sc_adb_tunnel_init(&server->tunnel);

    assert(cbs);
    assert(cbs->on_connection_failed);
    assert(cbs->on_connected);
    assert(cbs->on_disconnected);

    server->cbs = cbs;
    server->cbs_userdata = cbs_userdata;

    return true;
}

static bool
device_read_info(struct sc_intr *intr, sc_socket device_socket,
                 struct sc_server_info *info) {
    uint8_t buf[SC_DEVICE_NAME_FIELD_LENGTH];
    ssize_t r = net_recv_all_intr(intr, device_socket, buf, sizeof(buf));
    if (r < SC_DEVICE_NAME_FIELD_LENGTH) {
        LOGE("Could not retrieve device information");
        return false;
    }
    // in case the client sends garbage
    buf[SC_DEVICE_NAME_FIELD_LENGTH - 1] = '\0';
    static_assert(sizeof(info->device_name) == SC_DEVICE_NAME_FIELD_LENGTH);
    memcpy(info->device_name, buf, SC_DEVICE_NAME_FIELD_LENGTH);

    return true;
}

static bool
sc_server_connect_to(struct sc_server *server, struct sc_server_info *info) {
    struct sc_adb_tunnel *tunnel = &server->tunnel;

    assert(tunnel->enabled);

    const char *serial = server->serial;
    assert(serial);

    // Only the control socket is used (no video, no audio)
    sc_socket control_socket = SC_SOCKET_NONE;
    if (!tunnel->forward) {
        control_socket = net_accept_intr(&server->intr, tunnel->server_socket);
        if (control_socket == SC_SOCKET_NONE) {
            goto fail;
        }
    } else {
        uint32_t tunnel_host = server->params.tunnel_host;
        if (!tunnel_host) {
            tunnel_host = IPV4_LOCALHOST;
        }

        uint16_t tunnel_port = server->params.tunnel_port;
        if (!tunnel_port) {
            tunnel_port = tunnel->local_port;
        }

        unsigned attempts = 100;
        sc_tick delay = SC_TICK_FROM_MS(100);
        control_socket = connect_to_server(server, attempts, delay,
                                           tunnel_host, tunnel_port);
        if (control_socket == SC_SOCKET_NONE) {
            goto fail;
        }
    }

    // Disable Nagle's algorithm for the control socket
    bool ok = net_set_tcp_nodelay(control_socket, true);
    (void) ok; // error already logged

    // we don't need the adb tunnel anymore
    sc_adb_tunnel_close(tunnel, &server->intr, serial,
                        server->device_socket_name);

    // The socket will be closed on stop if device_read_info() fails
    server->control_socket = control_socket;
    ok = device_read_info(&server->intr, control_socket, info);
    if (!ok) {
        goto fail;
    }

    return true;

fail:
    if (control_socket != SC_SOCKET_NONE) {
        if (!net_close(control_socket)) {
            LOGW("Could not close control socket");
        }
        server->control_socket = SC_SOCKET_NONE;
    }

    if (tunnel->enabled) {
        // Always leave this function with tunnel disabled
        sc_adb_tunnel_close(tunnel, &server->intr, serial,
                            server->device_socket_name);
    }

    return false;
}

static void
sc_server_on_terminated(void *userdata) {
    struct sc_server *server = userdata;

    // If the server process dies before connecting to the server socket,
    // then the client will be stuck forever on accept(). To avoid the problem,
    // wake up the accept() call (or any other) when the server dies, like on
    // stop() (it is safe to call interrupt() twice).
    sc_intr_interrupt(&server->intr);

    server->cbs->on_disconnected(server, server->cbs_userdata);

    LOGD("Server terminated");
}

static void
sc_server_kill_adb_if_requested(struct sc_server *server) {
    if (server->params.kill_adb_on_close) {
        LOGI("Killing adb server...");
        unsigned flags = SC_ADB_NO_STDOUT | SC_ADB_NO_STDERR | SC_ADB_NO_LOGERR;
        sc_adb_kill_server(&server->intr, flags);
    }
}

static int
run_server(void *data) {
    struct sc_server *server = data;

    const struct sc_server_params *params = &server->params;

    // Execute "adb start-server" before "adb devices" so that daemon starting
    // output/errors is correctly printed in the console ("adb devices" output
    // is parsed, so it is not output)
    bool ok = sc_adb_start_server(&server->intr, 0);
    if (!ok) {
        LOGE("Could not start adb server");
        goto error_connection_failed;
    }

    struct sc_adb_device_selector selector;
    if (params->req_serial) {
        selector.type = SC_ADB_DEVICE_SELECT_SERIAL;
        selector.serial = params->req_serial;
    } else {
        // No explicit selection, check $ANDROID_SERIAL
        const char *env_serial = getenv("ANDROID_SERIAL");
        if (env_serial) {
            LOGI("Using ANDROID_SERIAL: %s", env_serial);
            selector.type = SC_ADB_DEVICE_SELECT_SERIAL;
            selector.serial = env_serial;
        } else {
            selector.type = SC_ADB_DEVICE_SELECT_ALL;
        }
    }
    struct sc_adb_device device;
    ok = sc_adb_select_device(&server->intr, &selector, 0, &device);
    if (!ok) {
        goto error_connection_failed;
    }

    // "move" the device.serial without copy
    server->serial = device.serial;
    // the serial must not be freed by the destructor
    device.serial = NULL;
    sc_adb_device_destroy(&device);

    const char *serial = server->serial;
    assert(serial);
    LOGD("Device serial: %s", serial);

    ok = push_server(&server->intr, serial);
    if (!ok) {
        goto error_connection_failed;
    }

    int r = asprintf(&server->device_socket_name, SC_SOCKET_NAME_PREFIX "%08x",
                     params->scid);
    if (r == -1) {
        LOG_OOM();
        goto error_connection_failed;
    }
    assert(r == sizeof(SC_SOCKET_NAME_PREFIX) - 1 + 8);
    assert(server->device_socket_name);

    ok = sc_adb_tunnel_open(&server->tunnel, &server->intr, serial,
                            server->device_socket_name, params->port_range,
                            params->force_adb_forward);
    if (!ok) {
        goto error_connection_failed;
    }

    // server will connect to our server socket
    sc_pid pid = execute_server(server, params);
    if (pid == SC_PROCESS_NONE) {
        sc_adb_tunnel_close(&server->tunnel, &server->intr, serial,
                            server->device_socket_name);
        goto error_connection_failed;
    }

    static const struct sc_process_listener listener = {
        .on_terminated = sc_server_on_terminated,
    };
    struct sc_process_observer observer;
    ok = sc_process_observer_init(&observer, pid, &listener, server);
    if (!ok) {
        sc_process_terminate(pid);
        sc_process_wait(pid, true); // ignore exit code
        sc_adb_tunnel_close(&server->tunnel, &server->intr, serial,
                            server->device_socket_name);
        goto error_connection_failed;
    }

    ok = sc_server_connect_to(server, &server->info);
    // The tunnel is always closed by server_connect_to()
    if (!ok) {
        sc_process_terminate(pid);
        sc_process_wait(pid, true); // ignore exit code
        sc_process_observer_join(&observer);
        sc_process_observer_destroy(&observer);
        goto error_connection_failed;
    }

    // Now connected
    server->cbs->on_connected(server, server->cbs_userdata);

    // Wait for server_stop()
    sc_mutex_lock(&server->mutex);
    while (!server->stopped) {
        sc_cond_wait(&server->cond_stopped, &server->mutex);
    }
    sc_mutex_unlock(&server->mutex);

    // Interrupt sockets to wake up socket blocking calls on the server

    if (server->control_socket != SC_SOCKET_NONE) {
        net_interrupt(server->control_socket);
    }

    // Give some delay for the server to terminate properly
#define WATCHDOG_DELAY SC_TICK_FROM_SEC(1)
    sc_tick deadline = sc_tick_now() + WATCHDOG_DELAY;
    bool terminated = sc_process_observer_timedwait(&observer, deadline);

    // After this delay, kill the server if it's not dead already.
    // On some devices, closing the sockets is not sufficient to wake up the
    // blocking calls while the device is asleep.
    if (!terminated) {
        // The process may have terminated since the check, but it is not
        // reaped (closed) yet, so its PID is still valid, and it is ok to call
        // sc_process_terminate() even in that case.
        LOGW("Killing the server...");
        sc_process_terminate(pid);
    }

    sc_process_observer_join(&observer);
    sc_process_observer_destroy(&observer);

    sc_process_close(pid);

    sc_server_kill_adb_if_requested(server);

    return 0;

error_connection_failed:
    sc_server_kill_adb_if_requested(server);
    server->cbs->on_connection_failed(server, server->cbs_userdata);
    return -1;
}

bool
sc_server_start(struct sc_server *server) {
    bool ok =
        sc_thread_create(&server->thread, run_server, "scrcpy-server", server);
    if (!ok) {
        LOGE("Could not create server thread");
        return false;
    }

    return true;
}

void
sc_server_stop(struct sc_server *server) {
    sc_mutex_lock(&server->mutex);
    server->stopped = true;
    sc_cond_signal(&server->cond_stopped);
    sc_intr_interrupt(&server->intr);
    sc_mutex_unlock(&server->mutex);
}

void
sc_server_join(struct sc_server *server) {
    sc_thread_join(&server->thread, NULL);
}

void
sc_server_destroy(struct sc_server *server) {
    if (server->control_socket != SC_SOCKET_NONE) {
        net_close(server->control_socket);
    }

    free(server->serial);
    free(server->device_socket_name);
    sc_intr_destroy(&server->intr);
    sc_cond_destroy(&server->cond_stopped);
    sc_mutex_destroy(&server->mutex);

    sc_adb_destroy();
}
