#!/usr/bin/env python3
"""QMP client for the VM grid: screendump + key injection (virtual keyboard)."""
import json
import socket
import sys
import time


def qmp(sock_path):
    s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    s.connect(sock_path)
    f = s.makefile("rw")

    def read():
        return json.loads(f.readline())

    def send(obj):
        f.write(json.dumps(obj) + "\r\n")
        f.flush()
        return read()

    read()  # greeting
    send({"execute": "qmp_capabilities"})
    return s, f, send


def main():
    sock, cmd = sys.argv[1], sys.argv[2]
    s, f, send = qmp(sock)
    if cmd == "system_powerdown":
        r = send({"execute": "system_powerdown"})
    elif cmd == "screendump":
        fn = sys.argv[3]
        r = send({"execute": "screendump", "arguments": {"filename": fn}})
        print("saved", fn)
    elif cmd == "key":
        # key <vm> "ctrl c" -> press+release
        keys = sys.argv[3].split()
        r = send({"execute": "sendkey", "arguments": {"keys": keys}})
        time.sleep(0.08)
    elif cmd == "keyseq":
        # keyseq <vm> "ctrl c;150;ctrl c"  (; segments, numbers = ms delay)
        seq = sys.argv[3]
        parts = [p.strip() for p in seq.split(";")]
        for p in parts:
            if not p:
                continue
            if p.isdigit():
                time.sleep(int(p) / 1000.0)
            else:
                send({"execute": "sendkey", "arguments": {"keys": p.split()}})
    else:
        r = send({"execute": cmd})
    if "error" in r:
        print("QMP error:", r["error"], file=sys.stderr)
        sys.exit(1)
    s.close()


if __name__ == "__main__":
    main()
