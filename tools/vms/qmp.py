#!/usr/bin/env python3
"""QMP client for the VM grid: screendump + key injection (virtual keyboard).

QEMU 10 QAPI (verified against query-qmp-schema of the running QMP):
  events: [{"type": "key",
            "data": {"key": {"type": "qcode", "data": "c"}, "down": true}}]
A tap = down, sleep, up(reversed) — a real key press.
"""
import json
import socket
import sys
import time

QCODE = {  # normalized aliases -> QKeyCode enum names
    "ctrl_l": "ctrl", "alt_l": "alt", "shift_l": "shift",
    "meta": "meta_l", "super": "meta_l", "enter": "ret", "return": "ret", "space": "spc",
    "backspace": "backspace",
}


class Qmp:
    def __init__(self, sock_path):
        self.s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self.s.connect(sock_path)
        self.f = self.s.makefile("rw")
        self.f.readline()  # greeting
        self.cmd("qmp_capabilities")

    def cmd(self, name, args=None):
        msg = {"execute": name}
        if args:
            msg["arguments"] = args
        self.f.write(json.dumps(msg) + "\r\n")
        self.f.flush()
        while True:
            line = json.loads(self.f.readline())
            if "event" not in line:
                break
        if "error" in line:
            raise RuntimeError(f"QMP error on {name}: {line['error']}")
        return line.get("return", {})

    def key_event(self, down, name):
        self.cmd("input-send-event", {"events": [{
            "type": "key",
            "data": {"key": {"type": "qcode",
                             "data": QCODE.get(name.lower(), name.lower())},
                     "down": down}}]})

    def tap(self, keys, ms=40):
        codes = [k.lower() for k in keys]
        for c in codes:
            self.key_event(True, c)
        time.sleep(ms / 1000.0)
        for c in reversed(codes):
            self.key_event(False, c)


def main():
    sock, cmd = sys.argv[1], sys.argv[2]
    q = Qmp(sock)
    if cmd == "system_powerdown":
        q.cmd("system_powerdown")
    elif cmd == "screendump":
        fn = sys.argv[3]
        q.cmd("screendump", {"filename": fn})
        print("saved", fn)
    elif cmd == "key":
        q.tap(sys.argv[3].split())
    elif cmd == "keyseq":
        # keyseq "ctrl c;150;ctrl c"  (segments; pure numbers = ms delays)
        for p in (x.strip() for x in sys.argv[3].split(";")):
            if not p:
                continue
            if p.isdigit():
                time.sleep(int(p) / 1000.0)
            else:
                q.tap(p.split())
    else:
        q.cmd(cmd)
    q.s.close()


if __name__ == "__main__":
    main()
