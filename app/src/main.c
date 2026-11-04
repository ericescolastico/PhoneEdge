#include "common.h"

#include <stdbool.h>
#include <stdio.h>
#include <SDL3/SDL.h>

#include "cli.h"
#include "events.h"
#include "options.h"
#include "scrcpy.h"
#include "util/file.h"
#include "util/log.h"
#include "util/net.h"

#ifdef _WIN32
#include <winsock2.h>
#include <windows.h>
#include "util/str.h"
#include "win/win_driver.h"
#endif

static int
main_scrcpy(int argc, char *argv[]) {
#ifdef _WIN32
    // disable buffering, we want logs immediately
    // even line buffering (setvbuf() with mode _IOLBF) is not sufficient
    setbuf(stdout, NULL);
    setbuf(stderr, NULL);
#endif

    printf("PhoneEdge " SCRCPY_VERSION
           " (fork do scrcpy, veja NOTICE)\n");

    struct scrcpy_cli_args args = {
        .help = false,
        .version = false,
        .pause_on_exit = false,
    };
    scrcpy_options_init(&args.opts);

    // phoneedge.ini next to the executable is read automatically
    char *default_config = sc_file_get_local_path("phoneedge.ini");
    if (default_config && sc_file_is_regular(default_config)) {
        args.opts.config_path = default_config;
    }

#ifndef NDEBUG
    args.opts.log_level = SC_LOG_LEVEL_DEBUG;
#endif

    enum scrcpy_exit_code ret;

    if (!scrcpy_parse_args(&args, argc, argv)) {
        ret = SCRCPY_EXIT_FAILURE;
        goto end;
    }

    sc_set_log_level(args.opts.log_level);

    if (args.opts.debug_log) {
        if (!sc_log_set_file(args.opts.debug_log)) {
            LOGE("Não foi possível abrir o arquivo de log %s",
                 args.opts.debug_log);
        } else {
            LOGI("Log detalhado em %s", args.opts.debug_log);
        }
    }

    if (args.help) {
        scrcpy_print_usage(argv[0]);
        ret = SCRCPY_EXIT_SUCCESS;
        goto end;
    }

    if (args.version) {
        printf("PhoneEdge " SCRCPY_VERSION "\n");
        ret = SCRCPY_EXIT_SUCCESS;
        goto end;
    }

    if (args.opts.list_monitors) {
#ifdef _WIN32
        pe_win_print_monitors();
#else
        printf("--list-monitors só funciona no Windows.\n");
#endif
        ret = SCRCPY_EXIT_SUCCESS;
        goto end;
    }

    if (!net_init()) {
        ret = SCRCPY_EXIT_FAILURE;
        goto end;
    }

    sc_log_configure();

    ret = scrcpy(&args.opts);

    net_cleanup();

end:
    free(default_config);
    sc_log_close_file();
    if (args.pause_on_exit) {
        printf("Press Enter to continue...\n");
        getchar();
    }

    return ret;
}

int
main(int argc, char *argv[]) {
#ifndef _WIN32
    return main_scrcpy(argc, argv);
#else
    (void) argc;
    (void) argv;
    int wargc;
    wchar_t **wargv = CommandLineToArgvW(GetCommandLineW(), &wargc);
    if (!wargv) {
        LOG_OOM();
        return SCRCPY_EXIT_FAILURE;
    }

    char **argv_utf8 = malloc((wargc + 1) * sizeof(*argv_utf8));
    if (!argv_utf8) {
        LOG_OOM();
        LocalFree(wargv);
        return SCRCPY_EXIT_FAILURE;
    }

    argv_utf8[wargc] = NULL;

    for (int i = 0; i < wargc; ++i) {
        argv_utf8[i] = sc_str_from_wchars(wargv[i]);
        if (!argv_utf8[i]) {
            LOG_OOM();
            for (int j = 0; j < i; ++j) {
                free(argv_utf8[j]);
            }
            LocalFree(wargv);
            free(argv_utf8);
            return SCRCPY_EXIT_FAILURE;
        }
    }

    LocalFree(wargv);

    int ret = main_scrcpy(wargc, argv_utf8);

    for (int i = 0; i < wargc; ++i) {
        free(argv_utf8[i]);
    }
    free(argv_utf8);

    return ret;
#endif
}
