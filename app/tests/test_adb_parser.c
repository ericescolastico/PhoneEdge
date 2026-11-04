#include "common.h"

#include <assert.h>

#include "adb/adb_device.h"
#include "adb/adb_parser.h"

static void test_adb_devices(void) {
    char output[] =
        "List of devices attached\n"
        "0123456789abcdef	device usb:2-1 product:MyProduct model:MyModel "
            "device:MyDevice transport_id:1\n"
        "192.168.1.1:5555	device product:MyWifiProduct model:MyWifiModel "
            "device:MyWifiDevice trandport_id:2\n";

    struct sc_vec_adb_devices vec = SC_VECTOR_INITIALIZER;
    bool ok = sc_adb_parse_devices(output, &vec);
    assert(ok);
    assert(vec.size == 2);

    struct sc_adb_device *device = &vec.data[0];
    assert(!strcmp("0123456789abcdef", device->serial));
    assert(!strcmp("device", device->state));
    assert(!strcmp("MyModel", device->model));

    device = &vec.data[1];
    assert(!strcmp("192.168.1.1:5555", device->serial));
    assert(!strcmp("device", device->state));
    assert(!strcmp("MyWifiModel", device->model));

    sc_adb_devices_destroy(&vec);
}

static void test_adb_devices_cr(void) {
    char output[] =
        "List of devices attached\r\n"
        "0123456789abcdef	device usb:2-1 product:MyProduct model:MyModel "
            "device:MyDevice transport_id:1\r\n"
        "192.168.1.1:5555	device product:MyWifiProduct model:MyWifiModel "
            "device:MyWifiDevice trandport_id:2\r\n";

    struct sc_vec_adb_devices vec = SC_VECTOR_INITIALIZER;
    bool ok = sc_adb_parse_devices(output, &vec);
    assert(ok);
    assert(vec.size == 2);

    struct sc_adb_device *device = &vec.data[0];
    assert(!strcmp("0123456789abcdef", device->serial));
    assert(!strcmp("device", device->state));
    assert(!strcmp("MyModel", device->model));

    device = &vec.data[1];
    assert(!strcmp("192.168.1.1:5555", device->serial));
    assert(!strcmp("device", device->state));
    assert(!strcmp("MyWifiModel", device->model));

    sc_adb_devices_destroy(&vec);
}

static void test_adb_devices_daemon_start(void) {
    char output[] =
        "* daemon not running; starting now at tcp:5037\n"
        "* daemon started successfully\n"
        "List of devices attached\n"
        "0123456789abcdef	device usb:2-1 product:MyProduct model:MyModel "
            "device:MyDevice transport_id:1\n";

    struct sc_vec_adb_devices vec = SC_VECTOR_INITIALIZER;
    bool ok = sc_adb_parse_devices(output, &vec);
    assert(ok);
    assert(vec.size == 1);

    struct sc_adb_device *device = &vec.data[0];
    assert(!strcmp("0123456789abcdef", device->serial));
    assert(!strcmp("device", device->state));
    assert(!strcmp("MyModel", device->model));

    sc_adb_devices_destroy(&vec);
}

static void test_adb_devices_daemon_start_mixed(void) {
    char output[] =
        "List of devices attached\n"
        "adb server version (41) doesn't match this client (39); killing...\n"
        "* daemon started successfully *\n"
        "0123456789abcdef	unauthorized usb:1-1\n"
        "87654321	device usb:2-1 product:MyProduct model:MyModel "
            "device:MyDevice\n";

    struct sc_vec_adb_devices vec = SC_VECTOR_INITIALIZER;
    bool ok = sc_adb_parse_devices(output, &vec);
    assert(ok);
    assert(vec.size == 2);

    struct sc_adb_device *device = &vec.data[0];
    assert(!strcmp("0123456789abcdef", device->serial));
    assert(!strcmp("unauthorized", device->state));
    assert(!device->model);

    device = &vec.data[1];
    assert(!strcmp("87654321", device->serial));
    assert(!strcmp("device", device->state));
    assert(!strcmp("MyModel", device->model));

    sc_adb_devices_destroy(&vec);
}

static void test_adb_devices_without_eol(void) {
    char output[] =
        "List of devices attached\n"
        "0123456789abcdef	device usb:2-1 product:MyProduct model:MyModel "
            "device:MyDevice transport_id:1";

    struct sc_vec_adb_devices vec = SC_VECTOR_INITIALIZER;
    bool ok = sc_adb_parse_devices(output, &vec);
    assert(ok);
    assert(vec.size == 1);

    struct sc_adb_device *device = &vec.data[0];
    assert(!strcmp("0123456789abcdef", device->serial));
    assert(!strcmp("device", device->state));
    assert(!strcmp("MyModel", device->model));

    sc_adb_devices_destroy(&vec);
}

static void test_adb_devices_without_header(void) {
    char output[] =
        "0123456789abcdef	device usb:2-1 product:MyProduct model:MyModel "
            "device:MyDevice transport_id:1\n";

    struct sc_vec_adb_devices vec = SC_VECTOR_INITIALIZER;
    bool ok = sc_adb_parse_devices(output, &vec);
    assert(!ok);
}

static void test_adb_devices_corrupted(void) {
    char output[] =
        "List of devices attached\n"
        "corrupted_garbage\n";

    struct sc_vec_adb_devices vec = SC_VECTOR_INITIALIZER;
    bool ok = sc_adb_parse_devices(output, &vec);
    assert(ok);
    assert(vec.size == 0);
}

static void test_adb_devices_spaces(void) {
    char output[] =
        "List of devices attached\n"
        "0123456789abcdef       unauthorized usb:1-4 transport_id:3\n";

    struct sc_vec_adb_devices vec = SC_VECTOR_INITIALIZER;
    bool ok = sc_adb_parse_devices(output, &vec);
    assert(ok);
    assert(vec.size == 1);

    struct sc_adb_device *device = &vec.data[0];
    assert(!strcmp("0123456789abcdef", device->serial));
    assert(!strcmp("unauthorized", device->state));
    assert(!device->model);

    sc_adb_devices_destroy(&vec);
}

static void test_adb_devices_serial_with_spaces(void) {
    char output[] =
        "List of devices attached\n"
        "adb-0123456789AB-CDdefg (2)._adb-tls-connect._tcp device "
            "product:blazer model:Pixel_10_Pro device:blazer transport_id:3\n";

    struct sc_vec_adb_devices vec = SC_VECTOR_INITIALIZER;
    bool ok = sc_adb_parse_devices(output, &vec);
    assert(ok);
    assert(vec.size == 1);

    struct sc_adb_device *device = &vec.data[0];
    assert(!strcmp("adb-0123456789AB-CDdefg (2)._adb-tls-connect._tcp",
                   device->serial));
    assert(!strcmp("device", device->state));
    assert(!strcmp("Pixel_10_Pro", device->model));

    sc_adb_devices_destroy(&vec);
}

static void test_adb_devices_with_devpath_without_colon(void) {
    char output[] =
        "List of devices attached\n"
        "12345678               device 3-1 product:manet model:23117RK66C "
            "device:manet transport_id:2\n";

    struct sc_vec_adb_devices vec = SC_VECTOR_INITIALIZER;
    bool ok = sc_adb_parse_devices(output, &vec);
    assert(ok);
    assert(vec.size == 1);

    struct sc_adb_device *device = &vec.data[0];
    assert(!strcmp("12345678", device->serial));
    assert(!strcmp("device", device->state));
    assert(!strcmp("23117RK66C", device->model));

    sc_adb_devices_destroy(&vec);
}

static void test_get_ip_single_line(void) {
    char ip_route[] = "192.168.1.0/24 dev wlan0  proto kernel  scope link  src "
                      "192.168.12.34\r\r\n";

    char *ip = sc_adb_parse_device_ip(ip_route);
    assert(ip);
    assert(!strcmp(ip, "192.168.12.34"));
    free(ip);
}

static void test_get_ip_single_line_without_eol(void) {
    char ip_route[] = "192.168.1.0/24 dev wlan0  proto kernel  scope link  src "
                      "192.168.12.34";

    char *ip = sc_adb_parse_device_ip(ip_route);
    assert(ip);
    assert(!strcmp(ip, "192.168.12.34"));
    free(ip);
}

static void test_get_ip_single_line_with_trailing_space(void) {
    char ip_route[] = "192.168.1.0/24 dev wlan0  proto kernel  scope link  src "
                      "192.168.12.34 \n";

    char *ip = sc_adb_parse_device_ip(ip_route);
    assert(ip);
    assert(!strcmp(ip, "192.168.12.34"));
    free(ip);
}

static void test_get_ip_multiline_first_ok(void) {
    char ip_route[] = "192.168.1.0/24 dev wlan0  proto kernel  scope link  src "
                      "192.168.1.2\r\n"
                      "10.0.0.0/24 dev rmnet  proto kernel  scope link  src "
                      "10.0.0.2\r\n";

    char *ip = sc_adb_parse_device_ip(ip_route);
    assert(ip);
    assert(!strcmp(ip, "192.168.1.2"));
    free(ip);
}

static void test_get_ip_multiline_second_ok(void) {
    char ip_route[] = "10.0.0.0/24 dev rmnet  proto kernel  scope link  src "
                      "10.0.0.3\r\n"
                      "192.168.1.0/24 dev wlan0  proto kernel  scope link  src "
                      "192.168.1.3\r\n";

    char *ip = sc_adb_parse_device_ip(ip_route);
    assert(ip);
    assert(!strcmp(ip, "192.168.1.3"));
    free(ip);
}

static void test_get_ip_multiline_second_ok_without_cr(void) {
    char ip_route[] = "10.0.0.0/24 dev rmnet  proto kernel  scope link  src "
                      "10.0.0.3\n"
                      "192.168.1.0/24 dev wlan0  proto kernel  scope link  src "
                      "192.168.1.3\n";

    char *ip = sc_adb_parse_device_ip(ip_route);
    assert(ip);
    assert(!strcmp(ip, "192.168.1.3"));
    free(ip);
}

static void test_get_ip_no_wlan(void) {
    char ip_route[] = "192.168.1.0/24 dev rmnet  proto kernel  scope link  src "
                      "192.168.12.34\r\r\n";

    char *ip = sc_adb_parse_device_ip(ip_route);
    assert(!ip);
}

static void test_get_ip_no_wlan_without_eol(void) {
    char ip_route[] = "192.168.1.0/24 dev rmnet  proto kernel  scope link  src "
                      "192.168.12.34";

    char *ip = sc_adb_parse_device_ip(ip_route);
    assert(!ip);
}

static void test_get_ip_truncated(void) {
    char ip_route[] = "192.168.1.0/24 dev rmnet  proto kernel  scope link  src "
                      "\n";

    char *ip = sc_adb_parse_device_ip(ip_route);
    assert(!ip);
}

static void test_wm_size_physical(void) {
    int w, h;
    assert(sc_adb_parse_wm_size("Physical size: 1080x2340\n", &w, &h));
    assert(w == 1080 && h == 2340);
}

static void test_wm_size_override_wins(void) {
    int w, h;
    assert(sc_adb_parse_wm_size("Physical size: 1080x2340\r\n"
                                "Override size: 720x1560\r\n", &w, &h));
    assert(w == 720 && h == 1560);
    // order does not matter
    assert(sc_adb_parse_wm_size("Override size: 720x1560\n"
                                "Physical size: 1080x2340\n", &w, &h));
    assert(w == 720 && h == 1560);
}

static void test_wm_size_garbage(void) {
    int w = 7, h = 7;
    assert(!sc_adb_parse_wm_size("", &w, &h));
    assert(!sc_adb_parse_wm_size("error: closed\n", &w, &h));
    assert(!sc_adb_parse_wm_size("Physical size: abc\n", &w, &h));
    assert(!sc_adb_parse_wm_size("Physical size: 0x0\n", &w, &h));
    assert(w == 7 && h == 7);
}

static void test_viewport_android12(void) {
    const char *dump =
        "Input Dispatcher State:\n"
        "  Viewports:\n"
        "    Viewport EXTERNAL: displayId=2, uniqueId=x, orientation=0, "
            "logicalFrame=[0, 0, 100, 100], isActive=[1]\n"
        "    Viewport INTERNAL: displayId=0, uniqueId=local:46198, port=129, "
            "orientation=1, logicalFrame=[0, 0, 2340, 1080], "
            "physicalFrame=[0, 0, 2340, 1080], deviceSize=[1080, 2340], "
            "isActive=[1]\n";
    int w, h, o;
    assert(sc_adb_parse_input_viewport(dump, &w, &h, &o));
    assert(w == 2340 && h == 1080 && o == 1);
}

static void test_viewport_android14(void) {
    const char *dump =
        "  DisplayViewport{type=INTERNAL, valid=true, isActive=true, "
            "displayId=0, uniqueId=local:4619827259835644672, port=129, "
            "orientation=0, logicalFrame=[0, 0, 1080, 2340], "
            "physicalFrame=[0, 0, 1080, 2340], deviceSize=[1080, 2340]}\n";
    int w, h, o;
    assert(sc_adb_parse_input_viewport(dump, &w, &h, &o));
    assert(w == 1080 && h == 2340 && o == 0);
}

static void test_viewport_missing(void) {
    int w, h, o;
    assert(!sc_adb_parse_input_viewport("nothing here\n", &w, &h, &o));
    assert(!sc_adb_parse_input_viewport(
        "Viewport EXTERNAL: displayId=2, logicalFrame=[0, 0, 1, 1]\n",
        &w, &h, &o));
}

static void test_surface_orientation(void) {
    int o;
    assert(sc_adb_parse_surface_orientation(
        "  SurfaceWidth: 1080px\n  SurfaceOrientation: 3\n", &o));
    assert(o == 3);
    assert(!sc_adb_parse_surface_orientation("SurfaceOrientation: 9\n", &o));
    assert(!sc_adb_parse_surface_orientation("", &o));
}

int main(int argc, char *argv[]) {
    (void) argc;
    (void) argv;

    test_adb_devices();
    test_adb_devices_cr();
    test_adb_devices_daemon_start();
    test_adb_devices_daemon_start_mixed();
    test_adb_devices_without_eol();
    test_adb_devices_without_header();
    test_adb_devices_corrupted();
    test_adb_devices_spaces();
    test_adb_devices_serial_with_spaces();
    test_adb_devices_with_devpath_without_colon();

    test_get_ip_single_line();
    test_get_ip_single_line_without_eol();
    test_get_ip_single_line_with_trailing_space();
    test_get_ip_multiline_first_ok();
    test_get_ip_multiline_second_ok();
    test_get_ip_multiline_second_ok_without_cr();
    test_get_ip_no_wlan();
    test_get_ip_no_wlan_without_eol();
    test_get_ip_truncated();

    test_wm_size_physical();
    test_wm_size_override_wins();
    test_wm_size_garbage();
    test_viewport_android12();
    test_viewport_android14();
    test_viewport_missing();
    test_surface_orientation();

    return 0;
}
