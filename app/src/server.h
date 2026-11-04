#ifndef SC_SERVER_H
#define SC_SERVER_H

#include "common.h"

#include <stdbool.h>
#include <stdint.h>

#include "adb/adb_tunnel.h"
#include "options.h"
#include "util/intr.h"
#include "util/net.h"
#include "util/thread.h"
#include "util/tick.h"

#define SC_DEVICE_NAME_FIELD_LENGTH 64
struct sc_server_info {
    char device_name[SC_DEVICE_NAME_FIELD_LENGTH];
};

struct sc_server_params {
    uint32_t scid;
    const char *req_serial;
    enum sc_log_level log_level;
    struct sc_port_range port_range;
    uint32_t tunnel_host;
    uint16_t tunnel_port;
    bool force_adb_forward;
    bool stay_awake;
    bool kill_adb_on_close;
};

struct sc_server {
    // The internal allocated strings are copies owned by the server
    struct sc_server_params params;
    char *serial;
    char *device_socket_name;

    sc_thread thread;
    struct sc_server_info info; // initialized once connected

    sc_mutex mutex;
    sc_cond cond_stopped;
    bool stopped;

    struct sc_intr intr;
    struct sc_adb_tunnel tunnel;

    sc_socket control_socket;

    const struct sc_server_callbacks *cbs;
    void *cbs_userdata;
};

struct sc_server_callbacks {
    /**
     * Called when the server failed to connect
     *
     * If it is called, then on_connected() and on_disconnected() will never be
     * called.
     */
    void (*on_connection_failed)(struct sc_server *server, void *userdata);

    /**
     * Called on server connection
     */
    void (*on_connected)(struct sc_server *server, void *userdata);

    /**
     * Called on server disconnection (after it has been connected)
     */
    void (*on_disconnected)(struct sc_server *server, void *userdata);
};

// init the server with the given params
bool
sc_server_init(struct sc_server *server, const struct sc_server_params *params,
               const struct sc_server_callbacks *cbs, void *cbs_userdata);

// start the server asynchronously
bool
sc_server_start(struct sc_server *server);

// disconnect and kill the server process
void
sc_server_stop(struct sc_server *server);

// join the server thread
void
sc_server_join(struct sc_server *server);

// close and release sockets
void
sc_server_destroy(struct sc_server *server);

#endif
