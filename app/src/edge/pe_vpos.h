#ifndef PE_VPOS_H
#define PE_VPOS_H

/**
 * Estimated position of the phone pointer.
 *
 * A HID mouse is relative and Android never tells where the pointer is, so the
 * position is estimated by summing the motions sent. The estimate is clamped
 * to the phone screen, like the real pointer, and it drifts (Android applies
 * its own pointer speed/acceleration): it must be re-anchored on every entry.
 */

#include <stdbool.h>

struct pe_vpos {
    int width;
    int height;
    float x; // [0, width - 1]
    float y; // [0, height - 1]
};

void
pe_vpos_init(struct pe_vpos *v, int width, int height);

// Set the position (clamped)
void
pe_vpos_set(struct pe_vpos *v, float x, float y);

// Move by (dx, dy) pixels, clamping to the screen. The part of the motion that
// was cut by the clamp is returned in *cut_x and *cut_y (0 if none): negative
// when the low edge (left/top) was exceeded, positive for the high edge.
void
pe_vpos_move(struct pe_vpos *v, float dx, float dy, float *cut_x,
             float *cut_y);

#endif
