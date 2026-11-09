#include "pe_app.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "adb/adb.h"
#include "adb/adb_parser.h"
#include "util/log.h"
#include "util/tick.h"

#ifdef _WIN32
# include <winsock2.h>
# include <windows.h>
# include "win/win_driver.h"
#endif

#define DUMPSYS_MAX (256 * 1024)

static uint64_t
now_ms(void) {
    return (uint64_t) SC_TICK_TO_MS(sc_tick_now());
}

// Quarter turns -> size
static void
rotate_size(int *w, int *h, int quarter_turns) {
    if (quarter_turns % 2) {
        int t = *w;
        *w = *h;
        *h = t;
    }
}

// Determine the phone screen size (with the current rotation applied)
static void
probe_phone_size(const struct pe_app_params *p, int *width, int *height) {
    const struct pe_config *cfg = p->cfg;
    pe_config_oriented_size(cfg, width, height);

    if (!cfg->phone_size_auto) {
        LOGI("Phone screen: %dx%d (from the configuration)", *width, *height);
        return;
    }
    if (!p->intr || !p->serial) {
        LOGI("Phone screen: %dx%d (default, no adb)", *width, *height);
        return;
    }

    char *dump = NULL;
    {
        const char *const args[] = {"dumpsys", "input", NULL};
        dump = sc_adb_shell_output(p->intr, p->serial, args, DUMPSYS_MAX,
                                   SC_ADB_SILENT);
    }

    int w, h, orientation;
    if (dump && sc_adb_parse_input_viewport(dump, &w, &h, &orientation)) {
        free(dump);
        LOGI("Phone screen: %dx%d (display viewport, rotation %d)", w, h,
             orientation);
        *width = w;
        *height = h;
        return;
    }

    int sw, sh;
    char *wm = NULL;
    {
        const char *const args[] = {"wm", "size", NULL};
        wm = sc_adb_shell_output(p->intr, p->serial, args, 4096,
                                 SC_ADB_SILENT);
    }
    if (wm && sc_adb_parse_wm_size(wm, &sw, &sh)) {
        int turns = cfg->phone_rotation / 90;
        const char *how = "wm size, rotation from the configuration";
        int so;
        if (dump && sc_adb_parse_surface_orientation(dump, &so)) {
            turns = so;
            how = "wm size + SurfaceOrientation";
        }
        rotate_size(&sw, &sh, turns);
        LOGI("Phone screen: %dx%d (%s)", sw, sh, how);
        *width = sw;
        *height = sh;
    } else {
        LOGW("Could not read the phone screen size with adb; using %dx%d. "
             "Set \"phone_size\" in the configuration if it is wrong.",
             *width, *height);
    }
    free(wm);
    free(dump);
}

// Phone-side operations of the session: forward to the sender
static const struct pe_phone_ops *
session_phone_ops(void) {
    return &pe_sender_ops;
}

static void
set_pc_cursor(void *ud, int x, int y) {
    (void) ud;
#ifdef _WIN32
    pe_win_release_clip();
    if (!SetCursorPos(x, y)) {
        LOGW("SetCursorPos(%d, %d) failed (%lu)", x, y, GetLastError());
    }
#else
    LOGI("(simulated) PC cursor placed at %d,%d", x, y);
#endif
}

// ---------------------------------------------------------------------------
// Android pointer settings (applied while running, restored on exit)

static void
trim_nl(char *s) {
    size_t n = strlen(s);
    while (n && (s[n - 1] == '\n' || s[n - 1] == '\r' || s[n - 1] == ' ')) {
        s[--n] = '\0';
    }
}

static void
android_set(struct pe_app *app, unsigned slot, const char *key,
            const char *value) {
    if (!app->adb_intr || !app->adb_serial) {
        return;
    }
    const char *const get[] = {"settings", "get", "system", key, NULL};
    char *old = sc_adb_shell_output(app->adb_intr, app->adb_serial, get, 256,
                                    SC_ADB_SILENT);
    if (old) {
        trim_nl(old);
        if (!strcmp(old, "null") || !*old) {
            free(old);
            old = NULL; // did not exist
        }
    }
    const char *const put[] = {"settings", "put", "system", key, value, NULL};
    char *out = sc_adb_shell_output(app->adb_intr, app->adb_serial, put, 256,
                                    SC_ADB_SILENT);
    if (!out) {
        LOGW("Could not set the Android setting %s", key);
        free(old);
        return;
    }
    free(out);
    app->android_settings[slot].key = key;
    app->android_settings[slot].original = old;
    app->android_settings[slot].changed = true;
    LOGI("Android: %s = %s (was %s); restored on exit", key, value,
         old ? old : "unset");
}

static void
android_restore(struct pe_app *app) {
    for (unsigned i = 0; i < 2; ++i) {
        if (!app->android_settings[i].changed) {
            continue;
        }
        const char *key = app->android_settings[i].key;
        char *orig = app->android_settings[i].original;
        const char *const put[] =
            {"settings", "put", "system", key, orig, NULL};
        const char *const del[] = {"settings", "delete", "system", key, NULL};
        char *out = sc_adb_shell_output(app->adb_intr, app->adb_serial,
                                        orig ? put : del, 256, SC_ADB_SILENT);
        if (out) {
            LOGI("Android: %s restored", key);
        } else {
            LOGW("Could not restore the Android setting %s (to restore it: "
                 "adb shell settings %s system %s%s%s)", key,
                 orig ? "put" : "delete", key, orig ? " " : "",
                 orig ? orig : "");
        }
        free(out);
        free(orig);
        app->android_settings[i].changed = false;
        app->android_settings[i].original = NULL;
    }
}

bool
pe_app_start(struct pe_app *app, const struct pe_app_params *params) {
    memset(app, 0, sizeof(*app));
    app->cfg = *params->cfg;

    char err[128];
    if (!pe_config_validate(&app->cfg, err, sizeof(err))) {
        LOGE("Invalid configuration: %s", err);
        return false;
    }

    if (!sc_mutex_init(&app->lifecycle)) {
        return false;
    }
    app->lifecycle_initialized = true;

    pe_inject_init(&app->inject, params->mp, params->kp);

    if (!pe_sender_init(&app->sender, &pe_inject_ops, &app->inject)) {
        return false;
    }
    app->sender_initialized = true;

    static const struct pe_pc_ops pc_ops = {.set_cursor = set_pc_cursor};
    // The session sends the phone actions to the sender (the sender itself is
    // the `ud` of the sender ops)
    if (!pe_session_init(&app->session, &app->cfg, session_phone_ops(),
                         &app->sender, &pc_ops, NULL)) {
        goto fail;
    }
    app->session_initialized = true;
    // The log lines of the session are written by the sender thread
    pe_session_set_logger(&app->session, pe_sender_log, &app->sender);

    if (params->intr && params->serial) {
        app->adb_intr = params->intr;
        app->adb_serial = strdup(params->serial);
        if (app->cfg.android_accel_off) {
            // (the key exists on recent Android versions; harmless otherwise)
            android_set(app, 0, "mouse_pointer_acceleration_enabled", "0");
        }
        if (app->cfg.android_pointer_speed != PE_POINTER_SPEED_KEEP) {
            char speed[8];
            snprintf(speed, sizeof(speed), "%d", app->cfg.android_pointer_speed);
            android_set(app, 1, "pointer_speed", speed);
        }
    }

    int w, h;
    probe_phone_size(params, &w, &h);
    pe_session_set_phone_size(&app->session, w, h);

    if (!pe_sender_start(&app->sender)) {
        goto fail;
    }
    app->sender_started = true;

    char hotkey[64];
    pe_hotkey_format(&app->cfg.hotkey, hotkey, sizeof(hotkey));

    // Written to the debug log: everything needed to understand a report
    LOGI("Config: edge=%s monitor=%d phone=%dx%d pointer_scale=%.2f "
         "sensitivity=%.2f wheel_scale=%.2f hotkey=%s edge_push=%d "
         "leave_margin=%d entry_inset=%d return_inset=%d flush_ms=%d "
         "enter_guard_ms=%d reenter_ms=%d anchor_extra=%d place_delay_ms=%d "
         "clip_cursor=%s place_scale=%.2f reanchor=%d/%ds altgr=%s "
         "android_accel_off=%s android_pointer_speed=%d",
         pe_side_name(app->cfg.edge), app->cfg.monitor_index, w, h,
         app->cfg.pointer_scale, app->cfg.sensitivity, app->cfg.wheel_scale,
         hotkey, app->cfg.edge_push, app->cfg.leave_margin,
         app->cfg.entry_inset, app->cfg.return_inset, app->cfg.flush_ms,
         app->cfg.enter_guard_ms, app->cfg.reenter_ms, app->cfg.anchor_extra,
         app->cfg.place_delay_ms, app->cfg.clip_cursor ? "yes" : "no",
         app->cfg.place_scale, app->cfg.reanchor_entries,
         app->cfg.reanchor_s, app->cfg.altgr ? "yes" : "no",
         app->cfg.android_accel_off ? "yes" : "no",
         app->cfg.android_pointer_speed);

    if (params->script_path) {
        if (!pe_script_init(&app->script, &app->session,
                            params->script_path)) {
            goto fail;
        }
        app->script_initialized = true;
        if (!pe_script_start(&app->script)) {
            goto fail;
        }
        app->script_started = true;
        LOGI("Input from the script %s", params->script_path);
        app->running = true;
        return true;
    }

#ifdef _WIN32
    pe_win_install_safety_handlers();
    if (!pe_win_driver_start(&app->session, &app->cfg)) {
        LOGE("Could not start the input capture");
        goto fail;
    }
    app->driver_started = true;
    LOGI("Edge handoff ready: push the mouse past the %s edge of monitor %d "
         "to control the phone. Emergency shortcut: %s",
         pe_side_name(app->cfg.edge), app->cfg.monitor_index, hotkey);
#else
    LOGW("The edge handoff captures the mouse with Windows APIs: nothing to do "
         "on this system. Use --script to inject input.");
#endif
    app->running = true;
    return true;

fail:
    pe_app_stop(app, false);
    pe_app_destroy(app);
    return false;
}

void
pe_app_notify_disconnect(struct pe_app *app) {
    if (!app->lifecycle_initialized) {
        return;
    }
    sc_mutex_lock(&app->lifecycle);
    if (app->running) {
        // Give the mouse back right now: the phone cannot receive anything
        pe_session_disconnect(&app->session, now_ms());
        pe_sender_stop(&app->sender, false);
    }
    sc_mutex_unlock(&app->lifecycle);
}

void
pe_app_stop(struct pe_app *app, bool phone_alive) {
    if (app->lifecycle_initialized) {
        // From now on, a disconnection notification does nothing
        sc_mutex_lock(&app->lifecycle);
        app->running = false;
        sc_mutex_unlock(&app->lifecycle);
    }

    if (app->script_started) {
        pe_script_stop(&app->script);
        pe_script_join(&app->script);
        app->script_started = false;
    }

    // Give the control back to the PC first, queuing the release of what is
    // held on the phone
    if (app->session_initialized) {
        if (phone_alive) {
            pe_session_release(&app->session, now_ms());
        } else {
            pe_session_disconnect(&app->session, now_ms());
        }
    }

#ifdef _WIN32
    if (app->driver_started) {
        pe_win_driver_stop();
        app->driver_started = false;
    }
#endif

    if (app->sender_started) {
        pe_sender_stop(&app->sender, phone_alive);
        pe_sender_join(&app->sender);
        app->sender_started = false;
    }

    if (phone_alive) {
        android_restore(app);
    }
}

void
pe_app_destroy(struct pe_app *app) {
    for (unsigned i = 0; i < 2; ++i) {
        free(app->android_settings[i].original);
        app->android_settings[i].original = NULL;
    }
    free(app->adb_serial);
    app->adb_serial = NULL;
    if (app->script_initialized) {
        pe_script_destroy(&app->script);
        app->script_initialized = false;
    }
    if (app->session_initialized) {
        pe_session_destroy(&app->session);
        app->session_initialized = false;
    }
    if (app->sender_initialized) {
        pe_sender_destroy(&app->sender);
        app->sender_initialized = false;
    }
    if (app->lifecycle_initialized) {
        sc_mutex_destroy(&app->lifecycle);
        app->lifecycle_initialized = false;
    }
}
