# Traduku — working list

## Grid: measured, from packages (no compiling in guests)

| profile | install | e2e |
|---|---|---|
| `arch-kde` | `traduku-0.3.0-1` via pacman | **8/8** |
| `ubuntu-gnome` | `traduku-gnome_0.3.0-1` via apt | **4/8** |
| `debian-kde` | .deb path wired, not yet run | — |
| `debian-gnome` | not provisioned | — |
| `ubuntu-kde` | not provisioned | — |

## Next (each named, with its evidence)

1. **test_06 on GNOME** — its window list is a KWin query (`'Traduku' in ''` on
   GNOME). Needs a desktop-aware assertion: X11 via `xdotool search`, Wayland
   via the app's own log/journal line for the window, or `gdbus` on the shell.
2. **test_04 on GNOME** — no `org.kde.StatusNotifierWatcher` in the guest: the
   appindicator extension is now installed by `mkvm.sh` but must be *enabled*
   for the vm user (`gnome-extensions enable ubuntu-appindicators@ubuntu.com`
   or `org.gnome.shell enabled-extensions`), and the test should assert the
   item on whichever watcher the desktop runs.
3. **test_02/03 on GNOME** — they drive the KDE first-run autostart prompt,
   which the GNOME shell intentionally does not have (it relies on the
   packaged `.desktop`). Either skip on gnome profiles with a reason, or assert
   the packaged autostart entry instead.
4. **Re-provision the guests** so the mkvm fixes are in the images, not just in
   the running VM: `./tools/vms/vmctl.sh mkvm ubuntu-gnome ubuntu gnome`,
   `mkvm debian-kde debian kde`, `mkvm ubuntu-kde ubuntu kde`,
   `mkvm debian-gnome debian gnome`. Verify with `check-grid.sh <vm>`.
5. **deb-builder reproducibility** — `vm-install.sh` builds the .debs in
   `traduku-deb-builder:trixie`; the ctest gate runs both shells there. Add a
   `lintian` pass once the GNOME package has a real user base.
6. **VM loop speed** — still unmeasured. Time one full run
   (`mkvm → up → install → pytest`) before optimising; candidates are btrfs
   snapshots of the base images, reusing overlays, and pushing the host-built
   binary for developer runs while CI installs packages.

## Release (Stage 8)

- Version is **0.3.0** everywhere and `tests/cmake/test_versions_agree.sh`
  fails the build if `CMakeLists.txt`, `PKGBUILD` and `changelog` drift.
- Packages are built from `git archive HEAD`: **commit before building**.
- `packaging/arch/mkrelease.sh release-out` → `traduku` + `traduku-gnome`
  (with `.sig`); `vm-install.sh` builds the debs in the builder container.
- README still documents the KDE install only; it needs the GNOME section
  (wl-clipboard, the udev rule and why `input` group membership is the wrong
  fix) and a "which package for which desktop" table.

## Operator-side checks still open

- Live confirmation of the installed Qt tray on this host (pid of
  `~/.local/bin/traduku`, first menu row `Traduku 0.3.0 · tray qt [built-in
  default]`).
- Nothing has been pushed to the remote; master is local only.
