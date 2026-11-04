#include "pe_monitors.h"

#include <string.h>

// a before b?
static bool
before(const struct pe_monitor_info *a, const struct pe_monitor_info *b) {
    if (a->primary != b->primary) {
        return a->primary;
    }
    if (a->rect.left != b->rect.left) {
        return a->rect.left < b->rect.left;
    }
    return a->rect.top < b->rect.top;
}

void
pe_monitors_sort(const struct pe_monitor_info *in, struct pe_monitor_info *out,
                 int count) {
    memcpy(out, in, count * sizeof(*in));
    // insertion sort: stable, a handful of elements
    for (int i = 1; i < count; ++i) {
        struct pe_monitor_info cur = out[i];
        int j = i - 1;
        while (j >= 0 && before(&cur, &out[j])) {
            out[j + 1] = out[j];
            --j;
        }
        out[j + 1] = cur;
    }
}
