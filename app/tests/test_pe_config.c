#include "common.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "edge/pe_config.h"

static void test_defaults(void) {
    struct pe_config c;
    pe_config_defaults(&c);
    assert(c.edge == PE_SIDE_RIGHT);
    assert(c.monitor_index == 0);
    assert(c.phone_size_auto);
    assert(c.phone_width == 1080 && c.phone_height == 2340);
    assert(c.phone_rotation == 0);
    assert(c.pointer_scale == 1.0f);
    assert(c.hotkey.mods == (PE_MOD_CTRL | PE_MOD_ALT));
    assert(c.hotkey.key == PE_SC_PAUSE);
    char err[64];
    assert(pe_config_validate(&c, err, sizeof(err)));
}

static void test_set_values(void) {
    struct pe_config c;
    pe_config_defaults(&c);
    char err[128];

    assert(pe_config_set(&c, "edge", "left", err, sizeof(err)) == PE_SET_OK);
    assert(c.edge == PE_SIDE_LEFT);
    assert(pe_config_set(&c, "edge", "top", err, sizeof(err)) == PE_SET_OK);
    assert(c.edge == PE_SIDE_TOP);
    assert(pe_config_set(&c, "edge", "bottom", err, sizeof(err)) == PE_SET_OK);
    assert(c.edge == PE_SIDE_BOTTOM);
    assert(pe_config_set(&c, "monitor", "2", err, sizeof(err)) == PE_SET_OK);
    assert(c.monitor_index == 2);
    assert(pe_config_set(&c, "phone_size", "720x1560", err, sizeof(err))
           == PE_SET_OK);
    assert(!c.phone_size_auto && c.phone_width == 720
           && c.phone_height == 1560);
    assert(pe_config_set(&c, "phone-size", "auto", err, sizeof(err))
           == PE_SET_OK);
    assert(c.phone_size_auto);
    assert(pe_config_set(&c, "Phone_Rotation", "270", err, sizeof(err))
           == PE_SET_OK);
    assert(c.phone_rotation == 270);
    assert(pe_config_set(&c, "pointer-scale", "1.35", err, sizeof(err))
           == PE_SET_OK);
    assert(c.pointer_scale > 1.34f && c.pointer_scale < 1.36f);
    assert(pe_config_set(&c, "sensitivity", "0.5", err, sizeof(err))
           == PE_SET_OK);
    assert(c.sensitivity == 0.5f);
    assert(pe_config_set(&c, "hotkey", "ctrl+shift+f12", err, sizeof(err))
           == PE_SET_OK);
    assert(c.hotkey.key == 0x45);
    assert(pe_config_set(&c, "edge_push", "30", err, sizeof(err)) == PE_SET_OK);
    assert(c.edge_push == 30);
    assert(pe_config_set(&c, "flush_ms", "4", err, sizeof(err)) == PE_SET_OK);
    assert(c.flush_ms == 4);
}

static void test_set_errors(void) {
    struct pe_config c;
    pe_config_defaults(&c);
    char err[160];

    assert(pe_config_set(&c, "edge", "middle", err, sizeof(err))
           == PE_SET_INVALID_VALUE);
    assert(strstr(err, "left, right, top or bottom"));
    assert(c.edge == PE_SIDE_RIGHT); // unchanged

    assert(pe_config_set(&c, "monitor", "-1", err, sizeof(err))
           == PE_SET_INVALID_VALUE);
    assert(pe_config_set(&c, "monitor", "abc", err, sizeof(err))
           == PE_SET_INVALID_VALUE);
    assert(pe_config_set(&c, "monitor", "", err, sizeof(err))
           == PE_SET_INVALID_VALUE);
    assert(pe_config_set(&c, "phone_size", "1080", err, sizeof(err))
           == PE_SET_INVALID_VALUE);
    assert(pe_config_set(&c, "phone_size", "10x10", err, sizeof(err))
           == PE_SET_INVALID_VALUE);
    assert(pe_config_set(&c, "phone_rotation", "45", err, sizeof(err))
           == PE_SET_INVALID_VALUE);
    assert(pe_config_set(&c, "pointer_scale", "0", err, sizeof(err))
           == PE_SET_INVALID_VALUE);
    // below 0.25 an entry would need too many reports
    assert(pe_config_set(&c, "pointer_scale", "0.1", err, sizeof(err))
           == PE_SET_INVALID_VALUE);
    assert(strstr(err, "0.25"));
    assert(pe_config_set(&c, "return_inset", "1", err, sizeof(err))
           == PE_SET_INVALID_VALUE);
    assert(pe_config_set(&c, "pointer_scale", "nan", err, sizeof(err))
           == PE_SET_INVALID_VALUE);
    assert(pe_config_set(&c, "pointer_scale", "1.5x", err, sizeof(err))
           == PE_SET_INVALID_VALUE);
    assert(pe_config_set(&c, "hotkey", "ctrl+", err, sizeof(err))
           == PE_SET_INVALID_VALUE);
    assert(strstr(err, "hotkey"));
    assert(pe_config_set(&c, "flush_ms", "0", err, sizeof(err))
           == PE_SET_INVALID_VALUE);

    // Not an edge setting: left to the caller
    assert(pe_config_set(&c, "serial", "abc", err, sizeof(err))
           == PE_SET_UNKNOWN_KEY);
}

struct ini_ctx {
    struct pe_config cfg;
    char other[8][64];
    int other_count;
};

static bool
ini_cb(void *userdata, const char *key, const char *value, char *err,
       unsigned errlen) {
    struct ini_ctx *ctx = userdata;
    enum pe_set_result r = pe_config_set(&ctx->cfg, key, value, err, errlen);
    if (r == PE_SET_INVALID_VALUE) {
        return false;
    }
    if (r == PE_SET_UNKNOWN_KEY) {
        if (!strcmp(key, "serial") && ctx->other_count < 8) {
            snprintf(ctx->other[ctx->other_count++], 64, "%s=%s", key, value);
            return true;
        }
        snprintf(err, errlen, "unknown setting \"%s\"", key);
        return false;
    }
    return true;
}

static void test_ini(void) {
    struct ini_ctx ctx = {0};
    pe_config_defaults(&ctx.cfg);
    char err[200];

    const char *text =
        "\xEF\xBB\xBF; PhoneEdge\r\n"
        "[phoneedge]\r\n"
        "edge = left   ; the phone is at the left\r\n"
        "  monitor=1\r\n"
        "# comment\r\n"
        "\r\n"
        "Pointer-Scale = 1.25\r\n"
        "hotkey = ctrl+alt+pause\r\n"
        "serial = R5CT1234567\r\n"
        "phone_size = 1080x2340";   // no final newline
    assert(pe_ini_parse(text, ini_cb, &ctx, err, sizeof(err)));
    assert(ctx.cfg.edge == PE_SIDE_LEFT);
    assert(ctx.cfg.monitor_index == 1);
    assert(ctx.cfg.pointer_scale > 1.24f && ctx.cfg.pointer_scale < 1.26f);
    assert(!ctx.cfg.phone_size_auto);
    assert(ctx.other_count == 1);
    assert(!strcmp(ctx.other[0], "serial=R5CT1234567"));
}

static void test_ini_errors(void) {
    struct ini_ctx ctx = {0};
    pe_config_defaults(&ctx.cfg);
    char err[200];

    assert(!pe_ini_parse("edge = left\nthis is wrong\n", ini_cb, &ctx, err,
                         sizeof(err)));
    assert(!strncmp(err, "line 2:", 7));

    assert(!pe_ini_parse("\n\nedge = nowhere\n", ini_cb, &ctx, err,
                         sizeof(err)));
    assert(!strncmp(err, "line 3:", 7));
    assert(strstr(err, "left, right"));

    assert(!pe_ini_parse("bogus = 1\n", ini_cb, &ctx, err, sizeof(err)));
    assert(strstr(err, "bogus"));

    assert(!pe_ini_parse("[broken\n", ini_cb, &ctx, err, sizeof(err)));
    assert(!pe_ini_parse("= value\n", ini_cb, &ctx, err, sizeof(err)));

    // empty file is fine
    assert(pe_ini_parse("", ini_cb, &ctx, err, sizeof(err)));
    assert(pe_ini_parse("\n\n; only comments\n", ini_cb, &ctx, err,
                        sizeof(err)));
}

static void test_ini_long_comment_lines(void) {
    struct ini_ctx ctx = {0};
    pe_config_defaults(&ctx.cfg);
    char err[200];

    // a comment longer than the line buffer must not break the file
    char text[2048];
    int n = snprintf(text, sizeof(text), "# ");
    for (int i = 0; i < 700; ++i) {
        text[n++] = 'x';
    }
    n += snprintf(text + n, sizeof(text) - n, "\n   ; ");
    for (int i = 0; i < 700; ++i) {
        text[n++] = 'y';
    }
    snprintf(text + n, sizeof(text) - n, "\nedge = top\n");
    assert(pe_ini_parse(text, ini_cb, &ctx, err, sizeof(err)));
    assert(ctx.cfg.edge == PE_SIDE_TOP);

    // ... but a long key/value line is still an error
    char bad[1024];
    n = snprintf(bad, sizeof(bad), "edge = ");
    for (int i = 0; i < 600; ++i) {
        bad[n++] = 'z';
    }
    bad[n] = '\0';
    assert(!pe_ini_parse(bad, ini_cb, &ctx, err, sizeof(err)));
    assert(strstr(err, "too long"));
}

static void test_oriented_size(void) {
    struct pe_config c;
    pe_config_defaults(&c);
    int w, h;
    pe_config_oriented_size(&c, &w, &h);
    assert(w == 1080 && h == 2340);
    c.phone_rotation = 90;
    pe_config_oriented_size(&c, &w, &h);
    assert(w == 2340 && h == 1080);
    c.phone_rotation = 180;
    pe_config_oriented_size(&c, &w, &h);
    assert(w == 1080 && h == 2340);
    c.phone_rotation = 270;
    pe_config_oriented_size(&c, &w, &h);
    assert(w == 2340 && h == 1080);
}

static void test_sides(void) {
    assert(pe_side_opposite(PE_SIDE_LEFT) == PE_SIDE_RIGHT);
    assert(pe_side_opposite(PE_SIDE_RIGHT) == PE_SIDE_LEFT);
    assert(pe_side_opposite(PE_SIDE_TOP) == PE_SIDE_BOTTOM);
    assert(pe_side_opposite(PE_SIDE_BOTTOM) == PE_SIDE_TOP);
    assert(!strcmp(pe_side_name(PE_SIDE_TOP), "top"));
}

int main(int argc, char *argv[]) {
    (void) argc;
    (void) argv;

    test_defaults();
    test_set_values();
    test_set_errors();
    test_ini();
    test_ini_errors();
    test_ini_long_comment_lines();
    test_oriented_size();
    test_sides();
    return 0;
}
