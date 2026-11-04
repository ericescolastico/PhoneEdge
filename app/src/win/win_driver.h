#ifndef PE_WIN_DRIVER_H
#define PE_WIN_DRIVER_H

/**
 * Windows input driver: captures the mouse (Raw Input + WH_MOUSE_LL) and the
 * keyboard (WH_KEYBOARD_LL) globally and feeds the edge session.
 *
 * Everything runs in a dedicated thread with its own message loop, so that the
 * hooks answer in microseconds (Windows removes the slow ones).
 *
 * Safety nets, so that the PC never stays without mouse/keyboard:
 *  - hooks are removed by Windows if the process dies or stalls;
 *  - pe_win_emergency_release() disables the hooks right away from any thread
 *    (console Ctrl+C/close handler, unhandled exception, atexit);
 *  - the emergency shortcut is handled by the session inside the hook.
 */

#include <stdbool.h>

#include "pe/pe_session.h"

// Start the capture thread (and install the hooks). On success, the session
// is fed from this thread.
bool
pe_win_driver_start(struct pe_session *session, const struct pe_config *cfg);

// Stop the capture thread and remove the hooks
void
pe_win_driver_stop(void);

// Disable the capture immediately: thread-safe, async-signal-ish (no
// allocation, no lock). Safe to call several times, even if not started.
void
pe_win_emergency_release(void);

// Install the process-wide handlers calling pe_win_emergency_release():
// console control events (Ctrl+C, close, logoff, shutdown), unhandled
// exceptions, and exit.
void
pe_win_install_safety_handlers(void);

// Release the cursor confinement (clip_cursor), if any. To call before placing
// the cursor: SetCursorPos() is limited to the clipping rectangle.
void
pe_win_release_clip(void);

// Print the monitors in the order used by the `monitor` setting
void
pe_win_print_monitors(void);

#endif
