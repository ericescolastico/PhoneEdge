#ifndef PE_SCRIPT_H
#define PE_SCRIPT_H

/**
 * Script input driver: feeds the edge session from a text file instead of the
 * hooks of the PC. It makes the whole chain (state machine, sender, UHID,
 * server) testable without Windows, and lets the user check that the phone
 * reacts to injected input (`--script`) independently of the Windows hooks.
 *
 * One command per line, '#' starts a comment:
 *
 *     monitor L T R B          declare the (only) monitor, default 0 0 1920 1080
 *     move DX DY X Y           raw mouse delta, cursor at (X, Y)
 *     down|up left|right|middle|x1|x2
 *     wheel N                  vertical wheel, N in 1/120 of notch
 *     hwheel N                 horizontal wheel
 *     key down|up HEX          HID scancode (e.g. 0x04 for 'a')
 *     sleep MS                 advance the clock (and really wait)
 *     tick                     flush the pending motion
 *     disconnect               the phone is gone
 *     release                  application exit
 *     state                    log whether the phone is active
 *     quit                     exit the application
 */

#include <stdatomic.h>
#include <stdbool.h>

#include "pe/pe_session.h"
#include "util/thread.h"

struct pe_script {
    struct pe_session *session;
    char *path;
    sc_thread thread;
    bool started;
    atomic_bool stop;
};

bool
pe_script_init(struct pe_script *sc, struct pe_session *session,
               const char *path);

bool
pe_script_start(struct pe_script *sc);

void
pe_script_stop(struct pe_script *sc);

void
pe_script_join(struct pe_script *sc);

void
pe_script_destroy(struct pe_script *sc);

// Run one line (exposed for the tests). Return false on a syntax error.
bool
pe_script_run_line(struct pe_session *session, uint64_t *clock, char *line);

#endif
