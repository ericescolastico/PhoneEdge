#ifndef PE_INJECT_H
#define PE_INJECT_H

/**
 * Phone-side injector: translates the actions of the edge state machine to
 * the existing scrcpy mouse and key processors (the UHID ones), which push
 * the HID reports to the device through the controller.
 */

#include <stdbool.h>
#include <stdint.h>

#include "pe/pe_session.h"
#include "trait/key_processor.h"
#include "trait/mouse_processor.h"

struct pe_inject {
    struct sc_mouse_processor *mp;
    struct sc_key_processor *kp;
    uint8_t sc_buttons; // buttons currently pressed, as sc_mouse_button
};

void
pe_inject_init(struct pe_inject *inj, struct sc_mouse_processor *mp,
               struct sc_key_processor *kp);

// To give to the sender, with the `pe_inject` as `ud`
extern const struct pe_phone_ops pe_inject_ops;

// Conversions, exposed for the tests
uint8_t
pe_inject_buttons_to_sc(uint8_t pe_buttons);

uint16_t
pe_inject_mods_to_sc(uint8_t hid_mods);

#endif
