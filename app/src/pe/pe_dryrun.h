#ifndef PE_DRYRUN_H
#define PE_DRYRUN_H

/**
 * Null mouse and key processors, for --dry-run: no phone, nothing is sent. It
 * lets the user test the PC side (edge detection, swallowing, return, shortcut)
 * before the phone is even connected.
 */

#include "trait/key_processor.h"
#include "trait/mouse_processor.h"

struct pe_dryrun {
    struct sc_mouse_processor mp;
    struct sc_key_processor kp;
};

void
pe_dryrun_init(struct pe_dryrun *d);

#endif
