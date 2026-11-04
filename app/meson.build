src = [
    'src/main.c',
    'src/adb/adb.c',
    'src/adb/adb_device.c',
    'src/adb/adb_parser.c',
    'src/adb/adb_tunnel.c',
    'src/cli.c',
    'src/compat.c',
    'src/control_msg.c',
    'src/controller.c',
    'src/device_msg.c',
    'src/edge/pe_config.c',
    'src/edge/pe_edge.c',
    'src/edge/pe_keymap.c',
    'src/edge/pe_vpos.c',
    'src/events.c',
    'src/options.c',
    'src/receiver.c',
    'src/scrcpy.c',
    'src/server.c',
    'src/hid/hid_keyboard.c',
    'src/hid/hid_mouse.c',
    'src/uhid/keyboard_uhid.c',
    'src/uhid/mouse_uhid.c',
    'src/util/env.c',
    'src/util/file.c',
    'src/util/intr.c',
    'src/util/log.c',
    'src/util/memory.c',
    'src/util/net.c',
    'src/util/net_intr.c',
    'src/util/process.c',
    'src/util/process_intr.c',
    'src/util/rand.c',
    'src/util/str.c',
    'src/util/strbuf.c',
    'src/util/thread.c',
    'src/util/tick.c',
]

feature_test_macros = [
    '-D_GNU_SOURCE',
    '-D_POSIX_C_SOURCE=200809L',
    '-D_XOPEN_SOURCE=700',
]

conf = configuration_data()

if host_machine.system() == 'windows'
    windows = import('windows')
    src += [
        'src/util/command.c',
        'src/sys/win/file.c',
        'src/sys/win/process.c',
        windows.compile_resources('phoneedge-windows.rc'),
    ]
    feature_test_macros += [
        '-D_WIN32_WINNT=0x0600',
        '-DWINVER=0x0600',
    ]
else
    src += [
        'src/sys/unix/file.c',
        'src/sys/unix/process.c',
    ]
    if host_machine.system() == 'darwin'
        feature_test_macros += '-D_DARWIN_C_SOURCE'
    endif
endif


add_project_arguments(feature_test_macros, language: 'c')

cc = meson.get_compiler('c')

static = get_option('static')

dependencies = [
    dependency('sdl3', version: '>= 3.2.0', static: static),
    # "required: false" because libm may be part of libc itself
    cc.find_library('m', required: false),
]

if host_machine.system() == 'windows'
    dependencies += cc.find_library('mingw32')
    dependencies += cc.find_library('ws2_32')
endif

check_functions = [
    'strdup',
    'asprintf',
    'vasprintf',
    'nrand48',
    'jrand48',
    'reallocarray',
]

foreach f : check_functions
  if cc.has_function(f)
    define = 'HAVE_' + f.underscorify().to_upper()
    conf.set(define, true)
  endif
endforeach

conf.set('HAVE_SOCK_CLOEXEC', host_machine.system() != 'windows' and
                              cc.has_header_symbol('sys/socket.h', 'SOCK_CLOEXEC'))

# the version, updated on release
conf.set_quoted('SCRCPY_VERSION', meson.project_version())

# the version of the scrcpy-server we push to the device: the server rejects
# clients with a different version, so this must match the prebuilt server
conf.set_quoted('SCRCPY_SERVER_VERSION', '5.0.1')

# the prefix used during configuration (meson --prefix=PREFIX)
conf.set_quoted('PREFIX', get_option('prefix'))

# use scrcpy-server from the same directory as the executable
conf.set('PORTABLE', get_option('portable'))

# the default client TCP port range for the "adb reverse" tunnel
# overridden by option --port
conf.set('DEFAULT_LOCAL_PORT_RANGE_FIRST', '27183')
conf.set('DEFAULT_LOCAL_PORT_RANGE_LAST', '27199')

# run a server debugger and wait for a client to be attached
conf.set('SERVER_DEBUGGER', get_option('server_debugger'))

configure_file(configuration: conf, output: 'config.h')

src_dir = include_directories('src')

executable('phoneedge', src,
           dependencies: dependencies,
           include_directories: src_dir,
           install: true,
           c_args: [])

### TESTS

# do not build tests in release (assertions would not be executed at all)
if get_option('buildtype') == 'debug'
    tests = [
        ['test_adb_parser', [
            'tests/test_adb_parser.c',
            'src/adb/adb_device.c',
            'src/adb/adb_parser.c',
            'src/util/str.c',
            'src/util/strbuf.c',
        ]],
        ['test_binary', [
            'tests/test_binary.c',
        ]],
        ['test_cli', [
            'tests/test_cli.c',
            'src/cli.c',
            'src/options.c',
            'src/util/log.c',
            'src/util/net.c',
            'src/util/str.c',
            'src/util/strbuf.c',
        ]],
        ['test_command_windows', [
          'tests/test_command_windows.c',
          'src/util/command.c',
          'src/util/str.c',
          'src/util/strbuf.c',
        ]],
        ['test_control_msg_serialize', [
            'tests/test_control_msg_serialize.c',
            'src/control_msg.c',
            'src/util/str.c',
            'src/util/strbuf.c',
        ]],
        ['test_device_msg_deserialize', [
            'tests/test_device_msg_deserialize.c',
            'src/device_msg.c',
        ]],
        ['test_pe_edge', [
            'tests/test_pe_edge.c',
            'src/edge/pe_config.c',
            'src/edge/pe_edge.c',
            'src/edge/pe_keymap.c',
            'src/edge/pe_vpos.c',
        ]],
        ['test_pe_keymap', [
            'tests/test_pe_keymap.c',
            'src/edge/pe_keymap.c',
        ]],
        ['test_pe_config', [
            'tests/test_pe_config.c',
            'src/edge/pe_config.c',
            'src/edge/pe_keymap.c',
        ]],
        ['test_pe_vpos', [
            'tests/test_pe_vpos.c',
            'src/edge/pe_vpos.c',
        ]],
        ['test_hid_keyboard', [
            'tests/test_hid_keyboard.c',
            'src/hid/hid_keyboard.c',
            'src/util/log.c',
        ]],
        ['test_strbuf', [
            'tests/test_strbuf.c',
            'src/util/strbuf.c',
        ]],
        ['test_str', [
            'tests/test_str.c',
            'src/util/str.c',
            'src/util/strbuf.c',
        ]],
        ['test_vecdeque', [
            'tests/test_vecdeque.c',
            'src/util/memory.c',
        ]],
        ['test_vector', [
            'tests/test_vector.c',
        ]],
    ]

    foreach t : tests
        sources = t[1] + ['src/compat.c']
        exe = executable(t[0], sources,
                         include_directories: src_dir,
                         dependencies: dependencies,
                         c_args: ['-DSC_TEST'])
        test(t[0], exe)
    endforeach
endif
