#!/usr/bin/env python3
"""End-to-end tests of the whole client chain on Linux (no phone, no Windows):

    phoneedge --script ...  ->  edge state machine  ->  sender  ->  UHID
        ->  controller  ->  socket  ->  fake server (tests/e2e/fake_adb.py)

Usage: run_e2e.py PATH_TO_PHONEEDGE [LD_LIBRARY_PATH]
"""
import json
import os
import subprocess
import sys
import tempfile
import textwrap

HERE = os.path.dirname(os.path.abspath(__file__))
FAKE_ADB = os.path.join(HERE, "fake_adb.py")

KEYBOARD_ID = 1
MOUSE_ID = 2


def run(exe, script, env_extra=None, args=(), timeout=30):
    with tempfile.TemporaryDirectory() as tmp:
        script_path = os.path.join(tmp, "script.txt")
        with open(script_path, "w") as f:
            f.write(textwrap.dedent(script))
        server = os.path.join(tmp, "scrcpy-server")
        with open(server, "wb") as f:
            f.write(b"fake")
        env = dict(os.environ)
        env.update({
            "ADB": FAKE_ADB,
            "SCRCPY_SERVER_PATH": server,
            "FAKE_ADB_DIR": os.path.join(tmp, "state"),
        })
        env.update(env_extra or {})
        proc = subprocess.run(
            [exe, "-V", "debug", "--script", script_path, *args],
            env=env, capture_output=True, text=True, timeout=timeout)
        log = []
        logfile = os.path.join(tmp, "state", "server.jsonl")
        if os.path.exists(logfile):
            with open(logfile) as f:
                log = [json.loads(l) for l in f if l.strip()]
        return proc, log


def inputs(log, dev_id):
    return [bytes.fromhex(e["data"]) for e in log
            if e["event"] == "uhid_input" and e["id"] == dev_id]


def mouse_reports(log):
    """(buttons, dx, dy, wheel, pan) of each mouse report"""
    out = []
    for d in inputs(log, MOUSE_ID):
        s = lambda b: b - 256 if b > 127 else b
        out.append((d[0], s(d[1]), s(d[2]), s(d[3]), s(d[4])))
    return out


def check(cond, msg):
    if not cond:
        raise AssertionError(msg)


def test_devices_created(exe):
    proc, log = run(exe, "sleep 100\nquit\n")
    check(proc.returncode == 0, f"exit code {proc.returncode}\n{proc.stdout}\n{proc.stderr}")
    creates = [e for e in log if e["event"] == "uhid_create"]
    check({e["id"] for e in creates} == {KEYBOARD_ID, MOUSE_ID}, f"creates: {creates}")
    check(any("Connected to \"FakePhone\"" in l for l in proc.stdout.splitlines() + proc.stderr.splitlines()), proc.stdout)
    # the server was launched with the control-only parameters and the
    # version of the prebuilt server, not the one of PhoneEdge
    args = [e for e in log if e["event"] == "server_args"][0]["args"]
    check("5.0.1" in args, args)
    check("video=false" in args and "audio=false" in args, args)


def test_enter_move_click_leave(exe):
    script = """
        monitor 0 0 1920 1080
        # push against the right edge at the middle of the screen
        move 8 0 1919 540
        sleep 20
        move 8 0 1919 540
        sleep 600
        state
        # move the phone pointer down and click
        move 0 30 0 0
        sleep 20
        down left
        sleep 20
        up left
        sleep 20
        wheel -120
        sleep 20
        key down 0x04
        sleep 20
        key up 0x04
        sleep 20
        # come back through the left edge of the phone
        move -200 0 0 0
        sleep 20
        move -200 0 0 0
        sleep 20
        move -200 0 0 0
        sleep 20
        move -200 0 0 0
        sleep 50
        state
        quit
    """
    proc, log = run(exe, script)
    out = proc.stdout + proc.stderr
    check(proc.returncode == 0, f"exit code {proc.returncode}\n{out}")
    check("Control moved to the phone" in out, out)
    check("script: phone_active=1" in out, out)
    check("Control back to the PC: edge" in out, out)
    check(out.rindex("phone_active=0") > out.rindex("Control back"), out)

    reports = mouse_reports(log)
    check(len(reports) > 10, f"{len(reports)} mouse reports")
    # anchoring: the first reports go toward a corner at full scale
    check(reports[0][1:3] in ((-127, -127), (-127, 127)), reports[:3])
    # a left click: a report with only the left button, then without
    check(any(r[0] == 1 for r in reports), "no left button report")
    check(reports[-1][0] == 0 or any(r[0] == 0 for r in reports[-3:]), "button not released")
    # a wheel notch down
    check(any(r[3] == -1 for r in reports), "no wheel report")
    # the 'a' key: usage 0x04 pressed then released
    kb = inputs(log, KEYBOARD_ID)
    check(any(d[2] == 0x04 for d in kb), f"no 'a' in {[d.hex() for d in kb]}")
    check(kb[-1][2:] == b"\0" * 6, f"key not released {kb[-1].hex()}")


def test_hotkey_releases_everything(exe):
    script = """
        move 8 0 1919 300
        sleep 20
        move 8 0 1919 300
        sleep 600
        down left
        sleep 20
        key down 0xe0
        sleep 20
        key down 0xe2
        sleep 20
        key down 0x04
        sleep 20
        key down 0x48
        sleep 100
        state
        key up 0x48
        quit
    """
    proc, log = run(exe, script)
    out = proc.stdout + proc.stderr
    check(proc.returncode == 0, f"exit code {proc.returncode}\n{out}")
    check("Control back to the PC: emergency hotkey" in out, out)
    check(out.rindex("phone_active=0") > 0 and "phone_active=1" not in out.split("emergency hotkey")[1], out)
    # nothing is left pressed on the phone
    mouse = mouse_reports(log)
    check(mouse[-1][0] == 0, f"mouse button left pressed: {mouse[-3:]}")
    kb = inputs(log, KEYBOARD_ID)
    check(kb[-1][0] == 0 and kb[-1][2:] == b"\0" * 6, f"keyboard not released: {kb[-1].hex()}")


def test_disconnect_returns_mouse(exe):
    script = """
        move 8 0 1919 300
        sleep 20
        move 8 0 1919 300
        sleep 3000
        quit
    """
    proc, log = run(exe, script, env_extra={"FAKE_ADB_DIE_MS": "800"})
    out = proc.stdout + proc.stderr
    check(proc.returncode == 2, f"exit code {proc.returncode} (expected 2: disconnected)\n{out}")
    check("Device disconnected" in out, out)
    check("Control back to the PC: phone disconnected" in out, out)


def test_viewport_rotation_and_fallbacks(exe):
    proc, _ = run(exe, "sleep 50\nquit\n", env_extra={"FAKE_ADB_VIEWPORT": "2340x1080:1"})
    check("Phone screen: 2340x1080 (display viewport, rotation 1)" in proc.stdout + proc.stderr, proc.stdout + proc.stderr)
    proc, _ = run(exe, "sleep 50\nquit\n", env_extra={"FAKE_ADB_NO_DUMPSYS": "1"})
    check("Phone screen: 1080x2340 (wm size" in proc.stdout + proc.stderr, proc.stdout + proc.stderr)
    proc, _ = run(exe, "sleep 50\nquit\n", args=("--phone-size", "720x1560"))
    check("Phone screen: 720x1560 (from the configuration)" in proc.stdout + proc.stderr, proc.stdout + proc.stderr)


def test_config_file_and_cli_override(exe):
    with tempfile.TemporaryDirectory() as tmp:
        ini = os.path.join(tmp, "my.ini")
        with open(ini, "w") as f:
            f.write("[phoneedge]\nedge = left\nmonitor = 0\npointer_scale = 1.5\n"
                    "hotkey = ctrl+shift+f12\n")
        proc, log = run(exe, "sleep 50\nquit\n",
                        args=("--config", ini, "--pointer-scale", "2.0"))
        out = proc.stdout + proc.stderr
        check(proc.returncode == 0, out)
        check("Configuration loaded from" in out, out)
        # the ini is applied, then the command line overrides it
        check("edge=left" in out and "pointer_scale=2.00" in out, out)
        check("hotkey=Ctrl+Shift+F12" in out, out)
        proc, _ = run(exe, "quit\n", args=("--config", os.path.join(tmp, "nope.ini")))
        check(proc.returncode != 0, "a missing config must be an error")


def test_debug_log_file(exe):
    with tempfile.TemporaryDirectory() as tmp:
        path = os.path.join(tmp, "debug.log")
        proc, _ = run(exe, "move 20 0 1919 540\nsleep 30\nmove 20 0 1919 540\nsleep 200\nquit\n",
                      args=("--debug-log", path))
        check(proc.returncode == 0, proc.stdout + proc.stderr)
        text = open(path).read()
        check("Config: edge=right" in text, text)
        check("Control moved to the phone" in text, text)
        check("Control back to the PC: release" in text, text)
        check("] DEBUG:" in text and text.lstrip().startswith("["), text[:200])


def test_dry_run_without_adb(exe):
    # no phone, no adb: the ADB variable points to nothing
    with tempfile.TemporaryDirectory() as tmp:
        script = os.path.join(tmp, "s.txt")
        with open(script, "w") as f:
            f.write("move 20 0 1919 540\nsleep 30\nmove 20 0 1919 540\nsleep 100\nstate\nquit\n")
        env = dict(os.environ, ADB="/nonexistent/adb")
        proc = subprocess.run([exe, "--dry-run", "--script", script], env=env,
                              capture_output=True, text=True, timeout=30)
        out = proc.stdout + proc.stderr
        check(proc.returncode == 0, f"exit code {proc.returncode}\n{out}")
        check("Modo de teste (--dry-run)" in out, out)
        check("Control moved to the phone" in out and "phone_active=1" in out, out)


def test_android_settings_applied_and_restored(exe):
    proc, log = run(exe, "sleep 100\nquit\n",
                    args=("--android-accel-off=yes", "--android-pointer-speed=3"))
    out = proc.stdout + proc.stderr
    check(proc.returncode == 0, out)
    puts = [(e["key"], e["value"]) for e in log if e["event"] == "settings_put"]
    dels = [e["key"] for e in log if e["event"] == "settings_delete"]
    # applied first, then restored: the speed to its original value, and the
    # acceleration key (which did not exist) deleted
    check(("mouse_pointer_acceleration_enabled", "0") in puts, puts)
    check(("pointer_speed", "3") in puts, puts)
    check(("pointer_speed", "0") in puts, puts)          # restored
    check("mouse_pointer_acceleration_enabled" in dels, dels)
    check("Android: pointer_speed restored" in out, out)
    # not touched without the options
    proc, log = run(exe, "sleep 50\nquit\n")
    check(not [e for e in log if e["event"].startswith("settings_")], log)


def test_altgr_and_soft_reentry_on_the_wire(exe):
    script = """
        move 20 0 1919 540
        sleep 600
        key down 0xe0
        sleep 20
        key down 0xe2
        sleep 20
        key down 0x1a
        sleep 20
        key up 0x1a
        sleep 20
        key up 0xe2
        sleep 20
        key up 0xe0
        sleep 100
        quit
    """
    proc, log = run(exe, script)
    out = proc.stdout + proc.stderr
    check(proc.returncode == 0, out)
    kb = inputs(log, KEYBOARD_ID)
    mods = [d[0] for d in kb]
    keys = [d[2] for d in kb]
    # the 'w' (0x1a) goes out with the right Alt (0x40) only: AltGr+W = '?'
    idx = keys.index(0x1A)
    check(mods[idx] == 0x40, f"mods {mods[idx]:02x}: {[d.hex() for d in kb]}")


def test_bad_script_line(exe):
    proc, log = run(exe, "bogus command\nquit\n")
    check("invalid command" in proc.stdout + proc.stderr, proc.stdout + proc.stderr)


TESTS = [
    test_devices_created,
    test_enter_move_click_leave,
    test_hotkey_releases_everything,
    test_disconnect_returns_mouse,
    test_viewport_rotation_and_fallbacks,
    test_config_file_and_cli_override,
    test_debug_log_file,
    test_dry_run_without_adb,
    test_android_settings_applied_and_restored,
    test_altgr_and_soft_reentry_on_the_wire,
]


def main():
    exe = sys.argv[1]
    if len(sys.argv) > 2:
        os.environ["LD_LIBRARY_PATH"] = sys.argv[2]
    failed = 0
    for t in TESTS:
        try:
            t(exe)
            print(f"PASS {t.__name__}")
        except Exception as e:  # noqa: BLE001
            failed += 1
            print(f"FAIL {t.__name__}: {e}")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
