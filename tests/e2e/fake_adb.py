#!/usr/bin/env python3
"""A fake `adb` (and, in `shell app_process`, a fake scrcpy server) used by the
end-to-end tests on Linux: no phone, no Windows.

Behaviour is driven by environment variables:
  FAKE_ADB_DIR        directory for the state (reverse port) and the log
  FAKE_ADB_DIE_MS     if set, the fake server drops the connection after N ms
  FAKE_ADB_VIEWPORT   "WxH:orientation" for `dumpsys input` (default 1080x2340:0)
  FAKE_ADB_NO_DUMPSYS if set, `dumpsys input` prints nothing useful
The control messages received by the server are logged as JSON lines in
$FAKE_ADB_DIR/server.jsonl.
"""
import json
import os
import socket
import sys
import time

STATE = os.environ.get("FAKE_ADB_DIR", "/tmp/fake_adb")
os.makedirs(STATE, exist_ok=True)
LOG = os.path.join(STATE, "server.jsonl")


def log(obj):
    with open(LOG, "a") as f:
        f.write(json.dumps(obj) + "\n")


def recv_exact(sock, n):
    buf = b""
    while len(buf) < n:
        chunk = sock.recv(n - len(buf))
        if not chunk:
            return None
        buf += chunk
    return buf


def run_server(args):
    with open(os.path.join(STATE, "reverse_port")) as f:
        port = int(f.read())
    log({"event": "server_args", "args": args})
    # The client listens (adb reverse); the server connects to it
    sock = socket.create_connection(("127.0.0.1", port))
    name = b"FakePhone".ljust(64, b"\0")
    sock.sendall(name)
    die_ms = os.environ.get("FAKE_ADB_DIE_MS")
    start = time.time()
    while True:
        if die_ms and (time.time() - start) * 1000 >= int(die_ms):
            log({"event": "server_died"})
            sock.close()
            return 0
        sock.settimeout(0.05)
        try:
            t = sock.recv(1)
        except socket.timeout:
            continue
        if not t:
            break
        sock.settimeout(5)
        t = t[0]
        if t == 12:  # UHID_CREATE
            hdr = recv_exact(sock, 6)
            nlen = recv_exact(sock, 1)[0]
            if nlen:
                recv_exact(sock, nlen)
            dsize = int.from_bytes(recv_exact(sock, 2), "big")
            desc = recv_exact(sock, dsize)
            log({"event": "uhid_create", "id": int.from_bytes(hdr[:2], "big"),
                 "desc_size": dsize, "desc": desc.hex()})
        elif t == 13:  # UHID_INPUT
            hdr = recv_exact(sock, 4)
            size = int.from_bytes(hdr[2:4], "big")
            data = recv_exact(sock, size)
            log({"event": "uhid_input", "id": int.from_bytes(hdr[:2], "big"),
                 "data": data.hex(), "t": time.time()})
        elif t == 14:  # UHID_DESTROY
            hdr = recv_exact(sock, 2)
            log({"event": "uhid_destroy",
                 "id": int.from_bytes(hdr, "big")})
        else:
            log({"event": "unexpected_type", "type": t})
            break
    log({"event": "server_exit"})
    return 0


def main():
    argv = sys.argv[1:]
    serial = None
    if argv[:1] == ["-s"]:
        serial = argv[1]
        argv = argv[2:]
    if not argv:
        return 1
    cmd = argv[0]

    if cmd in ("start-server", "kill-server"):
        return 0
    if cmd == "devices":
        print("List of devices attached")
        print("FAKE0001\tdevice usb:1-1 product:r0s model:SM_S901B "
              "device:r0s transport_id:1")
        return 0
    if cmd == "push":
        print("1 file pushed")
        return 0
    if cmd == "reverse":
        if argv[1] == "--remove":
            return 0
        port = argv[2].split(":")[1]
        with open(os.path.join(STATE, "reverse_port"), "w") as f:
            f.write(port)
        return 0
    if cmd == "shell":
        rest = argv[1:]
        if rest[:2] == ["wm", "size"]:
            print("Physical size: 1080x2340")
            return 0
        if rest[:2] == ["dumpsys", "input"]:
            if os.environ.get("FAKE_ADB_NO_DUMPSYS"):
                print("Input Manager State:")
                return 0
            vp = os.environ.get("FAKE_ADB_VIEWPORT", "1080x2340:0")
            size, orient = vp.split(":")
            w, h = size.split("x")
            print("Input Reader State (Time since last refresh: 0.0 ms):")
            print("  Viewports:")
            print("    Viewport INTERNAL: displayId=0, uniqueId=local:1, "
                  f"port=129, orientation={orient}, "
                  f"logicalFrame=[0, 0, {w}, {h}], "
                  f"physicalFrame=[0, 0, {w}, {h}], "
                  f"deviceSize=[{w}, {h}], isActive=[1]")
            return 0
        if rest and rest[0].startswith("CLASSPATH="):
            return run_server(rest)
        return 0
    return 0


if __name__ == "__main__":
    sys.exit(main())
