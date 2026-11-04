#include "pe_config.h"

#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PE_DEFAULT_PHONE_WIDTH 1080
#define PE_DEFAULT_PHONE_HEIGHT 2340

void
pe_config_defaults(struct pe_config *cfg) {
    cfg->edge = PE_SIDE_RIGHT;
    cfg->monitor_index = 0;
    cfg->phone_size_auto = true;
    cfg->phone_width = PE_DEFAULT_PHONE_WIDTH;
    cfg->phone_height = PE_DEFAULT_PHONE_HEIGHT;
    cfg->phone_rotation = 0;
    cfg->pointer_scale = 1.0f;
    cfg->sensitivity = 1.0f;
    cfg->wheel_scale = 1.0f;
    cfg->hotkey.mods = PE_MOD_CTRL | PE_MOD_ALT;
    cfg->hotkey.key = PE_SC_PAUSE;
    cfg->edge_push = 12;
    cfg->leave_margin = 24;
    cfg->entry_inset = 24;
    cfg->return_inset = 3;
    cfg->flush_ms = 8;
    cfg->enter_guard_ms = 300;
    cfg->reenter_ms = 300;
    cfg->anchor_extra = 3;
    cfg->place_delay_ms = 2;
}

const char *
pe_side_name(enum pe_side side) {
    switch (side) {
        case PE_SIDE_LEFT: return "left";
        case PE_SIDE_RIGHT: return "right";
        case PE_SIDE_TOP: return "top";
        case PE_SIDE_BOTTOM: return "bottom";
    }
    return "?";
}

enum pe_side
pe_side_opposite(enum pe_side side) {
    switch (side) {
        case PE_SIDE_LEFT: return PE_SIDE_RIGHT;
        case PE_SIDE_RIGHT: return PE_SIDE_LEFT;
        case PE_SIDE_TOP: return PE_SIDE_BOTTOM;
        case PE_SIDE_BOTTOM: return PE_SIDE_TOP;
    }
    return PE_SIDE_LEFT;
}

static void
set_err(char *err, unsigned errlen, const char *fmt, const char *a,
        const char *b) {
    if (err && errlen) {
        snprintf(err, errlen, fmt, a, b);
    }
}

static bool
parse_int(const char *s, long min, long max, long *out) {
    if (!*s) {
        return false;
    }
    char *end;
    errno = 0;
    long v = strtol(s, &end, 10);
    if (errno || *end || v < min || v > max) {
        return false;
    }
    *out = v;
    return true;
}

static bool
parse_float(const char *s, double min, double max, float *out) {
    if (!*s) {
        return false;
    }
    char *end;
    errno = 0;
    double v = strtod(s, &end);
    if (errno || *end || !isfinite(v) || v < min || v > max) {
        return false;
    }
    *out = (float) v;
    return true;
}

static bool
parse_int_setting(const char *value, long min, long max, int *out) {
    long v;
    if (!parse_int(value, min, max, &v)) {
        return false;
    }
    *out = (int) v;
    return true;
}

static bool
parse_phone_size(struct pe_config *cfg, const char *value) {
    if (!strcmp(value, "auto")) {
        cfg->phone_size_auto = true;
        return true;
    }

    char *end;
    errno = 0;
    long w = strtol(value, &end, 10);
    if (errno || (*end != 'x' && *end != 'X') || end == value) {
        return false;
    }
    const char *hs = end + 1;
    long h;
    if (!parse_int(hs, 100, 10000, &h) || w < 100 || w > 10000) {
        return false;
    }
    cfg->phone_size_auto = false;
    cfg->phone_width = (int) w;
    cfg->phone_height = (int) h;
    return true;
}

static void
normalize_key(const char *in, char *out, size_t len) {
    size_t i = 0;
    for (; in[i] && i + 1 < len; ++i) {
        char c = (char) tolower((unsigned char) in[i]);
        out[i] = c == '-' ? '_' : c;
    }
    out[i] = '\0';
}

enum pe_set_result
pe_config_set(struct pe_config *cfg, const char *key_in, const char *value,
              char *err, unsigned errlen) {
    char key[40];
    normalize_key(key_in, key, sizeof(key));

    bool ok;
    if (!strcmp(key, "edge")) {
        ok = true;
        if (!strcmp(value, "left")) {
            cfg->edge = PE_SIDE_LEFT;
        } else if (!strcmp(value, "right")) {
            cfg->edge = PE_SIDE_RIGHT;
        } else if (!strcmp(value, "top")) {
            cfg->edge = PE_SIDE_TOP;
        } else if (!strcmp(value, "bottom")) {
            cfg->edge = PE_SIDE_BOTTOM;
        } else {
            ok = false;
        }
        if (!ok) {
            set_err(err, errlen, "%s: expected left, right, top or bottom, "
                    "got \"%s\"", key, value);
            return PE_SET_INVALID_VALUE;
        }
        return PE_SET_OK;
    }

    if (!strcmp(key, "hotkey")) {
        struct pe_hotkey hk;
        char msg[96];
        if (!pe_hotkey_parse(value, &hk, msg, sizeof(msg))) {
            set_err(err, errlen, "hotkey: %s%s", msg, "");
            return PE_SET_INVALID_VALUE;
        }
        cfg->hotkey = hk;
        return PE_SET_OK;
    }

    if (!strcmp(key, "phone_size")) {
        if (!parse_phone_size(cfg, value)) {
            set_err(err, errlen, "%s: expected auto or WIDTHxHEIGHT "
                    "(100-10000), got \"%s\"", key, value);
            return PE_SET_INVALID_VALUE;
        }
        return PE_SET_OK;
    }

    struct {
        const char *name;
        int *field;
        long min;
        long max;
    } ints[] = {
        {"monitor", &cfg->monitor_index, 0, 63},
        {"edge_push", &cfg->edge_push, 0, 1000},
        {"leave_margin", &cfg->leave_margin, 0, 1000},
        {"entry_inset", &cfg->entry_inset, 0, 500},
        {"return_inset", &cfg->return_inset, 0, 100},
        {"flush_ms", &cfg->flush_ms, 1, 100},
        {"enter_guard_ms", &cfg->enter_guard_ms, 0, 5000},
        {"reenter_ms", &cfg->reenter_ms, 0, 5000},
        {"anchor_extra", &cfg->anchor_extra, 0, 100},
        {"place_delay_ms", &cfg->place_delay_ms, 0, 50},
    };
    for (size_t i = 0; i < sizeof(ints) / sizeof(ints[0]); ++i) {
        if (!strcmp(key, ints[i].name)) {
            if (!parse_int_setting(value, ints[i].min, ints[i].max,
                                   ints[i].field)) {
                char range[48];
                snprintf(range, sizeof(range), "%ld-%ld", ints[i].min,
                         ints[i].max);
                char msg[160];
                snprintf(msg, sizeof(msg), "%s: expected an integer in %s",
                         key, range);
                set_err(err, errlen, "%s, got \"%s\"", msg, value);
                return PE_SET_INVALID_VALUE;
            }
            return PE_SET_OK;
        }
    }

    if (!strcmp(key, "phone_rotation")) {
        long v;
        if (!parse_int(value, 0, 270, &v) || v % 90) {
            set_err(err, errlen, "%s: expected 0, 90, 180 or 270, got \"%s\"",
                    key, value);
            return PE_SET_INVALID_VALUE;
        }
        cfg->phone_rotation = (int) v;
        return PE_SET_OK;
    }

    struct {
        const char *name;
        float *field;
    } floats[] = {
        {"pointer_scale", &cfg->pointer_scale},
        {"sensitivity", &cfg->sensitivity},
        {"wheel_scale", &cfg->wheel_scale},
    };
    for (size_t i = 0; i < sizeof(floats) / sizeof(floats[0]); ++i) {
        if (!strcmp(key, floats[i].name)) {
            if (!parse_float(value, 0.05, 20.0, floats[i].field)) {
                set_err(err, errlen, "%s: expected a number in 0.05-20, "
                        "got \"%s\"", key, value);
                return PE_SET_INVALID_VALUE;
            }
            return PE_SET_OK;
        }
    }

    return PE_SET_UNKNOWN_KEY;
}

bool
pe_ini_parse(const char *text, pe_ini_cb cb, void *userdata, char *err,
             unsigned errlen) {
    unsigned lineno = 0;
    const char *p = text;
    while (*p) {
        ++lineno;
        const char *eol = strpbrk(p, "\r\n");
        size_t len = eol ? (size_t) (eol - p) : strlen(p);
        const char *next = eol ? eol : p + len;
        if (*next == '\r' && next[1] == '\n') {
            ++next;
        }
        if (*next) {
            ++next;
        }

        char line[512];
        if (len >= sizeof(line)) {
            snprintf(err, errlen, "line %u: line too long", lineno);
            return false;
        }
        memcpy(line, p, len);
        line[len] = '\0';
        p = next;

        // Skip the UTF-8 BOM of the first line (Notepad)
        char *s = line;
        if (lineno == 1 && (unsigned char) s[0] == 0xEF
                && (unsigned char) s[1] == 0xBB
                && (unsigned char) s[2] == 0xBF) {
            s += 3;
        }

        while (*s == ' ' || *s == '\t') {
            ++s;
        }
        if (!*s || *s == ';' || *s == '#') {
            continue;
        }
        if (*s == '[') {
            if (!strchr(s, ']')) {
                snprintf(err, errlen, "line %u: unterminated section", lineno);
                return false;
            }
            continue;
        }

        char *eq = strchr(s, '=');
        if (!eq) {
            snprintf(err, errlen, "line %u: expected key = value", lineno);
            return false;
        }
        *eq = '\0';
        char *key = s;
        char *value = eq + 1;

        // trim key
        char *end = key + strlen(key);
        while (end > key && (end[-1] == ' ' || end[-1] == '\t')) {
            *--end = '\0';
        }
        // trim value, and strip an inline comment preceded by a space
        while (*value == ' ' || *value == '\t') {
            ++value;
        }
        for (char *c = value; *c; ++c) {
            if ((*c == ';' || *c == '#') && c > value
                    && (c[-1] == ' ' || c[-1] == '\t')) {
                *c = '\0';
                break;
            }
        }
        end = value + strlen(value);
        while (end > value && (end[-1] == ' ' || end[-1] == '\t')) {
            *--end = '\0';
        }

        if (!*key) {
            snprintf(err, errlen, "line %u: empty key", lineno);
            return false;
        }

        char msg[200] = "";
        if (!cb(userdata, key, value, msg, sizeof(msg))) {
            snprintf(err, errlen, "line %u: %s", lineno,
                     *msg ? msg : "invalid entry");
            return false;
        }
    }
    return true;
}

void
pe_config_oriented_size(const struct pe_config *cfg, int *width,
                        int *height) {
    if (cfg->phone_rotation == 90 || cfg->phone_rotation == 270) {
        *width = cfg->phone_height;
        *height = cfg->phone_width;
    } else {
        *width = cfg->phone_width;
        *height = cfg->phone_height;
    }
}

bool
pe_config_validate(const struct pe_config *cfg, char *err, unsigned errlen) {
    if (cfg->phone_width < 100 || cfg->phone_height < 100) {
        snprintf(err, errlen, "invalid phone size %dx%d", cfg->phone_width,
                 cfg->phone_height);
        return false;
    }
    if (!(cfg->pointer_scale > 0) || !(cfg->sensitivity > 0)) {
        snprintf(err, errlen, "pointer_scale and sensitivity must be > 0");
        return false;
    }
    return true;
}
