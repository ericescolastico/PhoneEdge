#ifndef SC_ADB_PARSER_H
#define SC_ADB_PARSER_H

#include "common.h"

#include <stdbool.h>

#include "adb/adb_device.h"

/**
 * Parse the available devices from the output of `adb devices`
 *
 * The parameter must be a NUL-terminated string.
 *
 * Warning: this function modifies the buffer for optimization purposes.
 */
bool
sc_adb_parse_devices(char *str, struct sc_vec_adb_devices *out_vec);

/**
 * Parse the ip from the output of `adb shell ip route`
 *
 * The parameter must be a NUL-terminated string.
 *
 * Warning: this function modifies the buffer for optimization purposes.
 */
char *
sc_adb_parse_device_ip(char *str);

/**
 * Parse the screen size from the output of `adb shell wm size`:
 *
 *     Physical size: 1080x2340
 *     Override size: 720x1560    (only if the resolution was changed)
 *
 * The override size has priority. The size is in the natural orientation of
 * the device (rotation is not applied).
 *
 * The parameter must be a NUL-terminated string (not modified).
 */
bool
sc_adb_parse_wm_size(const char *str, int *width, int *height);

/**
 * Parse the size of the internal display (with the rotation applied) and its
 * rotation, from the output of `adb shell dumpsys input`, from a line like:
 *
 *     Viewport INTERNAL: displayId=0, ..., orientation=1,
 *         logicalFrame=[0, 0, 2340, 1080], ...
 *
 * (the exact layout varies between Android versions: only the `INTERNAL`,
 * `displayId=`, `orientation=` and `logicalFrame=[l, t, r, b]` fields are
 * used). `orientation` is a number of 90 degrees counterclockwise turns
 * (Surface.ROTATION_*), -1 if absent.
 *
 * The parameter must be a NUL-terminated string (not modified).
 */
bool
sc_adb_parse_input_viewport(const char *str, int *width, int *height,
                            int *orientation);

/**
 * Parse the rotation (0 to 3) from a `SurfaceOrientation: N` line of the
 * output of `adb shell dumpsys input` (older Android versions).
 */
bool
sc_adb_parse_surface_orientation(const char *str, int *orientation);

#endif
