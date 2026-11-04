#ifndef PE_MONITORS_H
#define PE_MONITORS_H

#include <stdbool.h>

#include "edge/pe_edge.h"

struct pe_monitor_info {
    struct pe_rect rect;
    bool primary;
};

/**
 * Sort monitors into the order used by the `monitor` setting: the primary
 * monitor first (index 0), then the others from left to right, and from top
 * to bottom for the ones with the same left coordinate.
 *
 * `in` and `out` must hold `count` elements, and may not overlap.
 */
void
pe_monitors_sort(const struct pe_monitor_info *in, struct pe_monitor_info *out,
                 int count);

#endif
