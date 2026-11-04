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

// Stop everything. `phone_alive`: whether the phone can still receive the
// release of the held keys and buttons.
void
pe_app_stop(struct pe_app *app, bool phone_alive);

#endif
