#!/usr/bin/env python3
"""Minimal RFB (VNC) client for virtual-keyboard injection.

QMP input-send-event targets the QEMU *active console*, which the serial
log steals. VNC KeyPressed events go straight to the guest keyboard
(PS/2 emulation) of the running graphical session — the standard remote
desktop path, console-focus independent.

usage:
  vnc.py <host> <port> key "ctrl c"
  vnc.py <host> <port> keyseq "ctrl c;250;ctrl c"
"""
import socket
import struct
import sys
import threading
import time

KEYSYM = {
    "ctrl": 0xFFE3, "ctrl_l": 0xFFE3, "ctrl_r": 0xFFE4,
    "alt": 0xFFE9, "alt_l": 0xFFE9, "alt_r": 0xFFEA, "meta": 0xFFEB,
    "meta_r": 0xFFEC, "super": 0xFFEB,
    "shift": 0xFFE1, "shift_l": 0xFFE1, "shift_r": 0xFFE2,
    "enter": 0xFF0D, "return": 0xFF0D, "esc": 0xFF1B, "tab": 0xFF09,
    "backspace": 0xFF08, "space": 0x20,
}


def keysym(name):
    return KEYSYM.get(name, ord(name) if len(name) == 1 else 0)


class Vnc:
    def __init__(self, host, port):
        self.s = socket.create_connection((host, port), timeout=10)
        f = self.s.makefile("rb")
        self.f = f
        ver = f.read(12)
        if not ver.startswith(b"RFB "):
            raise RuntimeError(f"not RFB: {ver!r}")
        self.s.sendall(b"RFB 003.008\n")  # client version reply (required)
        n = f.read(1)[0]
        types = f.read(n)
        if 1 not in types:
            raise RuntimeError(f"no auth-none type in {types!r}")
        self.s.sendall(b"\x01")
        if b"RFB 003.008" in ver:
            if f.read(4) != b"\x00\x00\x00\x00":
                raise RuntimeError("security handshake failed")
        self.s.sendall(b"\x01")  # ClientInit: shared flag
        # ServerInit: w(2) h(2) pixel-format(16) name-length(4) + name
        rest = f.read(20)
        w, h = struct.unpack(">HH", rest[:4])
        nlen = struct.unpack(">I", rest[16:20])[0]
        f.read(nlen)
        self.geom = (w, h)
        # QEMU drops clients that never request the framebuffer: speak like
        # a real viewer (SetEncodings + initial full FBUpdateRequest).
        self.s.sendall(struct.pack(">BBxx", 2, 0))  # SetEncodings, n=0
        self.s.sendall(struct.pack(">BBx", 3, 1) +
                       struct.pack(">IHHHH", 0, 0, 0, w, h))
        # QEMU disconnects VNC clients whose output buffer stalls: drain the
        # socket continuously in the background.
        self._stop = False
        t = threading.Thread(target=self._drain, daemon=True)
        t.start()

    def _drain(self):
        self.s.settimeout(0.5)
        while not self._stop:
            try:
                if self.s.recv(65536) == b"":
                    return
            except socket.timeout:
                pass
            except OSError:
                return

    def key(self, name, down):
        self.s.sendall(struct.pack(">BBII", 4, 0, keysym(name), 1 if down else 0))

    def tap(self, names, ms=40):
        for n in names:
            self.key(n, True)
        time.sleep(ms / 1000.0)
        for n in reversed(names):
            self.key(n, False)

    def close(self):
        self._stop = True
        try:
            self.s.close()
        except OSError:
            pass


def main():
    host, port, cmd, seq = sys.argv[1], int(sys.argv[2]), sys.argv[3], sys.argv[4]
    v = Vnc(host, port)
    for part in (p.strip() for p in seq.split(";")):
        if not part:
            continue
        if part.isdigit():
            time.sleep(int(part) / 1000.0)
        else:
            v.tap(part.split())
    time.sleep(0.2)  # let the server flush events
    v.close()


if __name__ == "__main__":
    main()
