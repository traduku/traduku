# Traduku — working list

Operator list of 2026-11-07, after the GNOME/KDE merge into `master`
(`712c240`). Both shells were then confirmed working by the operator on real
hardware: Ubuntu/GNOME and Arch/KDE.

## Now

- [x] **1.** Remove the installed old app, install the new one on the Qt tray,
      launch it, and wait for the operator's confirmation.
- [x] **1.1** Delete `KdeTray` in its own commit (KDE now runs on
      `QSystemTrayIcon` like GNOME; KF6 StatusNotifierItem dependency gone).
- [x] **2.** HTTP-status clarity: a clear error on a wrong API configuration or
      a missing answer. Existing coverage was real but the *wording* named no
      endpoint and no remedy, and "200 OK with nothing in it" was reported as
      "the model returned an empty translation" even when the endpoint was a
      proxy HTML page.

## Thinking / design

- [ ] **2b.** Guest reset and rebuild are slow — decide on a faster loop
      (btrfs snapshots of the guest image vs. qcow2 backing chains vs.
      `qemu -snapshot` + a pre-baked base). See `tools/vms/README` notes.

## Stage 7 — VMs

- [ ] **3.** Add a GNOME/Wayland guest (`mkvm.sh` currently forces
      `WaylandEnable=False`), run the e2e grid, report the results.

## Next

- [ ] **4.** Packages: Ubuntu `.deb` for the GNOME shell (Stage 8), Arch
      `PKGBUILD`s for both shells (the existing one builds KDE only).
- [ ] **5.** e2e tests that install and test the packages, per platform.
- [ ] **6.** A release with packages for each platform, and a README section on
      installing from packages.
- [ ] **7.** Push everything to `master` on the remote.

## Still open from GNOME_PLAN.md

- [ ] Stage 4 — `X11Clipboard` + the clipboard priority chain.
- [ ] Stage 3B — gsettings global-shortcut fallback + `--trigger` (only if it
      earns its keep; the shortcut portal refuses us: `portal.Error.NotAllowed`).
- [ ] Stage 8 — packaging, README/ARCHITECTURE for the GNOME shell, version bump.
