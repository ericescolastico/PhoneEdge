#ifndef PE_APP_H
#define PE_APP_H

/**
 * Wires everything together: probes the phone screen, creates the edge
 * session, the asynchronous sender and the injector toward the UHID
 * processors, and starts the input driver (Windows hooks, or a script).
 */

#include <stdbool.h>

#include "edge/pe_config.h"
#include "pe/pe_inject.h"
#include "pe/pe_script.h"
#include "pe/pe_sender.h"
#include "pe/pe_session.h"
#include "trait/key_processor.h"
#include "trait/mouse_processor.h"
#include "util/intr.h"
#include "util/thread.h"

struct pe_app_params {
    const struct pe_config *cfg;
    // If set, the input comes from this script instead of the PC hooks
    const char *script_path;
    struct sc_mouse_processor *mp;
    struct sc_key_processor *kp;
    // To query the phone with adb
    struct sc_intr *intr;
    const char *serial;
};

struct pe_app {
    struct pe_config cfg;
    struct pe_inject inject;
    struct pe_sender sender;
    struct pe_session session;
    struct pe_script script;

    // Serializes pe_app_notify_disconnect() (receiver/controller thread) with
    // pe_app_stop(); destroyed by pe_app_destroy()
    sc_mutex lifecycle;
    bool lifecycle_initialized;
    bool running;

    // Android settings changed while running (restored on exit)
    struct sc_intr *adb_intr;
    char *adb_serial;
    struct {
        const char *key;
        char *original; // NULL: the setting did not exist
        bool changed;
    } android_settings[2];

    bool sender_initialized;
    bool sender_started;
    bool session_initialized;
    bool script_initialized;
    bool script_started;
    bool driver_started;
};

// Initialize and start. On failure, nothing is left running.
bool
pe_app_start(struct pe_app *app, const struct pe_app_params *params);

// The phone is gone (any thread): the PC takes the control back for good
void
pe_app_notify_disconnect(struct pe_app *app);

// Give the control back to the PC and stop the input (driver, script, sender).
// `phone_alive`: whether the phone can still receive the release of the held
// keys and buttons. After this, pe_app_notify_disconnect() does nothing.
void
pe_app_stop(struct pe_app *app, bool phone_alive);

// Free everything. Only once nothing can call pe_app_notify_disconnect()
// anymore (the controller threads are joined).
void
pe_app_destroy(struct pe_app *app);

#endif
