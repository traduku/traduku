"""Grid access for the traduku E2E suite (see PIPELINE.md).

Host-side helper: SSH into a grid VM (QEMU, port 2200+idx) + QMP virtual
keyboard/screenshots through tools/vms/vmctl.sh (docker exec, the QMP socket
is root-owned). One Grid per VM; tests declare `TRADUKU_VM`.
"""
import os
import subprocess
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
VMCTL = ROOT / "tools" / "vms" / "vmctl.sh"
VMS_HOME = Path.home() / "vms"


class Grid:
    def __init__(self, vm):
        self.vm = vm
        idx = self._idx()
        self.port = 2200 + idx
        self.shots = VMS_HOME / "run" / vm / "shots"

    def _idx(self):
        out = self.vmctl("status")
        for line in out.splitlines():
            f = line.split()
            if f and f[0] == self.vm:
                return int(f[2].split("=")[1])
        raise RuntimeError(f"vm {self.vm} not in grid:\n{out}")

    def vmctl(self, *args):
        return subprocess.run([str(VMCTL), *args], capture_output=True, text=True).stdout

    # ---- ssh ----
    def _ssh_cmd(self, cmd):
        return ["ssh", "-i", str(VMS_HOME / "id_ed25519"),
                "-o", "StrictHostKeyChecking=no", "-o", "UserKnownHostsFile=/dev/null",
                "-o", "ConnectTimeout=10", "-o", "ServerAliveInterval=15",
                "-p", str(self.port), "vm@127.0.0.1", cmd]

    def run(self, cmd, timeout=120):
        """Run in the guest; returns (rc, stdout)."""
        p = subprocess.run(self._ssh_cmd(cmd), capture_output=True, text=True, timeout=timeout)
        return p.returncode, p.stdout

    def ok(self, cmd, timeout=120):
        rc, out = self.run(cmd, timeout)
        return rc == 0, out

    def push(self, local, remote):
        subprocess.run(["scp", "-i", str(VMS_HOME / "id_ed25519"),
                        "-o", "StrictHostKeyChecking=no", "-o", "UserKnownHostsFile=/dev/null",
                        "-P", str(self.port), str(local), f"vm@127.0.0.1:{remote}"],
                       check=True, capture_output=True)

    def pull(self, remote, local):
        subprocess.run(["scp", "-i", str(VMS_HOME / "id_ed25519"),
                        "-o", "StrictHostKeyChecking=no", "-o", "UserKnownHostsFile=/dev/null",
                        "-P", str(self.port), f"vm@127.0.0.1:{remote}", str(local)],
                       check=True, capture_output=True)

    # ---- session ----
    def wait_ssh(self, seconds=420):
        end = time.time() + seconds
        while time.time() < end:
            try:
                rc, _ = self.run("echo up", timeout=15)
                if rc == 0:
                    return True
            except subprocess.TimeoutExpired:
                pass
            time.sleep(5)
        return False

    def sudo(self, cmd):
        return self.run(f"echo vm | sudo -S {cmd}")

    def ses(self, script, timeout=120):
        """Run a snippet inside the captured graphical session."""
        return self.run(f"source /tmp/session.env; {script}", timeout=timeout)

    # ---- input (QMP scancodes: physical keys, layout-independent) ----
    def keyseq(self, seq):
        self.vmctl("keyseq", self.vm, seq)

    def double_ctrl_c(self):
        self.keyseq("ctrl c;200;ctrl c")

    def double_ctrl_alt_c(self):
        self.keyseq("ctrl alt c;200;ctrl alt c")

    # ---- framebuffer (QMP screendump: compositor independent) ----
    def shot(self, name):
        self.shots.mkdir(parents=True, exist_ok=True)
        (self.shots / f"{name}.ppm").unlink(missing_ok=True)
        self.vmctl("shot", self.vm, name)
        p = self.shots / f"{name}.ppm"
        for _ in range(10):
            if p.exists() and p.stat().st_size > 0:
                return p
            time.sleep(0.5)
        raise RuntimeError(f"screendump {name} produced nothing")

    def diff_frac(self, a, b):
        """Fraction of the framebuffer that changed between two shots."""
        out = subprocess.run(
            ["docker", "exec", "-i", "traduku-pipeline-vmm", "python3", "-"]
            , input=f"""
from PIL import Image, ImageChops
a=Image.open(r"/vms/run/{self.vm}/shots/{a}.ppm").convert("RGB")
b=Image.open(r"/vms/run/{self.vm}/shots/{b}.ppm").convert("RGB")
bbox=ImageChops.difference(a,b).getbbox()
area=(bbox[2]-bbox[0])*(bbox[3]-bbox[1]) if bbox else 0
print(frac:=area/(a.size[0]*a.size[1]))
""".strip(),
            capture_output=True, text=True).stdout.strip()
        return float(out.splitlines()[-1])
