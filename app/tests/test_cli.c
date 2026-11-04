#include "common.h"

#include <assert.h>
#include <string.h>

#include "cli.h"
#include "options.h"

static struct scrcpy_cli_args
default_args(void) {
    struct scrcpy_cli_args args = {
        .help = false,
        .version = false,
        .pause_on_exit = false,
    };
    scrcpy_options_init(&args.opts);
    return args;
}

static void test_flag_version(void) {
    struct scrcpy_cli_args args = default_args();
    char *argv[] = {"phoneedge", "-v"};

    bool ok = scrcpy_parse_args(&args, 2, argv);
    assert(ok);
    assert(!args.help);
    assert(args.version);
}

static void test_flag_help(void) {
    struct scrcpy_cli_args args = default_args();
    char *argv[] = {"phoneedge", "--help"};

    bool ok = scrcpy_parse_args(&args, 2, argv);
    assert(ok);
    assert(args.help);
    assert(!args.version);
}

static void test_options(void) {
    struct scrcpy_cli_args args = default_args();
    char *argv[] = {
        "phoneedge",
        "--serial", "0123456789abcdef",
        "--port", "1234:1236",
        "--stay-awake",
        "--force-adb-forward",
        "--kill-adb-on-close",
        "--tunnel-host", "1.2.3.4",
        "--tunnel-port", "1234",
        "--verbosity", "warn",
        "--pause-on-exit",
    };

    bool ok = scrcpy_parse_args(&args, ARRAY_LEN(argv), argv);
    assert(ok);

    const struct scrcpy_options *opts = &args.opts;
    assert(!strcmp(opts->serial, "0123456789abcdef"));
    assert(opts->port_range.first == 1234);
    assert(opts->port_range.last == 1236);
    assert(opts->stay_awake);
    assert(opts->force_adb_forward);
    assert(opts->kill_adb_on_close);
    assert(opts->tunnel_host == 0x01020304);
    assert(opts->tunnel_port == 1234);
    assert(opts->log_level == SC_LOG_LEVEL_WARN);
    assert(args.pause_on_exit);
}

static void test_port_range_reversed(void) {
    struct scrcpy_cli_args args = default_args();
    char *argv[] = {"phoneedge", "--port", "2000:1000"};

    bool ok = scrcpy_parse_args(&args, 3, argv);
    assert(ok);
    assert(args.opts.port_range.first == 1000);
    assert(args.opts.port_range.last == 2000);
}

static void test_invalid(void) {
    struct scrcpy_cli_args args = default_args();
    char *bad_level[] = {"phoneedge", "-V", "loud"};
    assert(!scrcpy_parse_args(&args, 3, bad_level));

    args = default_args();
    char *bad_port[] = {"phoneedge", "--port", "70000"};
    assert(!scrcpy_parse_args(&args, 3, bad_port));

    args = default_args();
    char *unknown[] = {"phoneedge", "--video-bit-rate", "5M"};
    assert(!scrcpy_parse_args(&args, 3, unknown));

    args = default_args();
    char *extra[] = {"phoneedge", "foo"};
    assert(!scrcpy_parse_args(&args, 2, extra));
}

static void test_edge_options(void) {
    struct scrcpy_cli_args args = default_args();
    char *argv[] = {
        "phoneedge",
        "--edge", "left",
        "--monitor=1",
        "--phone-size", "720x1560",
        "--phone-rotation", "90",
        "--pointer-scale", "1.3",
        "--sensitivity=0.5",
        "--hotkey", "ctrl+shift+f12",
        "--edge-push", "20",
        "--clip-cursor", "yes",
        "--debug-log", "log.txt",
        "--dry-run",
        "--script", "s.txt",
        "--list-monitors",
    };
    assert(scrcpy_parse_args(&args, ARRAY_LEN(argv), argv));
    const struct scrcpy_options *o = &args.opts;
    assert(o->edge.edge == PE_SIDE_LEFT);
    assert(o->edge.monitor_index == 1);
    assert(!o->edge.phone_size_auto);
    assert(o->edge.phone_width == 720 && o->edge.phone_height == 1560);
    assert(o->edge.phone_rotation == 90);
    assert(o->edge.pointer_scale > 1.29f && o->edge.pointer_scale < 1.31f);
    assert(o->edge.sensitivity == 0.5f);
    assert(o->edge.hotkey.key == 0x45);
    assert(o->edge.edge_push == 20);
    assert(o->edge.clip_cursor);
    assert(!strcmp(o->debug_log, "log.txt"));
    assert(!strcmp(o->script_path, "s.txt"));
    assert(o->dry_run && o->list_monitors);
    // --debug-log implies detailed logs...
    assert(o->log_level == SC_LOG_LEVEL_DEBUG);
}

static void test_debug_log_keeps_explicit_verbosity(void) {
    struct scrcpy_cli_args args = default_args();
    char *argv[] = {"phoneedge", "--debug-log", "x.txt", "-V", "warn"};
    assert(scrcpy_parse_args(&args, 5, argv));
    assert(args.opts.log_level == SC_LOG_LEVEL_WARN);
}

static void test_edge_option_errors(void) {
    struct scrcpy_cli_args args = default_args();
    char *e1[] = {"phoneedge", "--edge", "up"};
    assert(!scrcpy_parse_args(&args, 3, e1));
    char *e2[] = {"phoneedge", "--hotkey", "ctrl+alt+delete"};
    assert(!scrcpy_parse_args(&args, 3, e2));
    char *e3[] = {"phoneedge", "--pointer-scale", "0.1"};
    assert(!scrcpy_parse_args(&args, 3, e3));
    char *e4[] = {"phoneedge", "--config", "/nonexistent/phoneedge.ini"};
    assert(!scrcpy_parse_args(&args, 3, e4));
}

static void test_config_text_then_cli_override(void) {
    struct scrcpy_options o;
    scrcpy_options_init(&o);
    char err[200];
    const char *ini =
        "[phoneedge]\n"
        "edge = bottom\n"
        "monitor = 2\n"
        "pointer_scale = 1.4\n"
        "serial = R5CT123\n"
        "stay_awake = yes\n"
        "verbosity = warn\n"
        "port = 30000:30002\n";
    assert(scrcpy_load_config_text(&o, ini, err, sizeof(err)));
    assert(o.edge.edge == PE_SIDE_BOTTOM && o.edge.monitor_index == 2);
    assert(!strcmp(o.serial, "R5CT123"));
    assert(o.stay_awake);
    assert(o.log_level == SC_LOG_LEVEL_WARN && o.log_level_set);
    assert(o.port_range.first == 30000 && o.port_range.last == 30002);

    assert(!scrcpy_load_config_text(&o, "bogus = 1\n", err, sizeof(err)));
    assert(strstr(err, "line 1") && strstr(err, "bogus"));
    assert(!scrcpy_load_config_text(&o, "stay_awake = maybe\n", err,
                                    sizeof(err)));
    free((void *) o.serial);
}

int main(int argc, char *argv[]) {
    (void) argc;
    (void) argv;

    test_flag_version();
    test_flag_help();
    test_options();
    test_port_range_reversed();
    test_invalid();
    test_edge_options();
    test_debug_log_keeps_explicit_verbosity();
    test_edge_option_errors();
    test_config_text_then_cli_override();
    return 0;
}
