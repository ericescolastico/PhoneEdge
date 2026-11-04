#include "common.h"

#include <assert.h>
#include <string.h>

#include "cli.h"
#include "options.h"

static struct scrcpy_cli_args
default_args(void) {
    struct scrcpy_cli_args args = {
        .opts = scrcpy_options_default,
        .help = false,
        .version = false,
        .pause_on_exit = false,
    };
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

int main(int argc, char *argv[]) {
    (void) argc;
    (void) argv;

    test_flag_version();
    test_flag_help();
    test_options();
    test_port_range_reversed();
    test_invalid();
    return 0;
}
