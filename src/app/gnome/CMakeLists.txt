# The GNOME application: thin composition root, and deliberately NO KF6
# (ROADMAP.md §2 - GNOME ships none of it; tray/notifications/shortcut
# are plain D-Bus, the gesture is /dev/input, the clipboard is wl-clipboard).

if(TARGET traduku)
    message(FATAL_ERROR
        "two traduku shells are configured in one build - LT_SHELL selects "
        "exactly one (kde or gnome)")
endif()

add_executable(traduku main.cpp)
# GNOME has no KF6 to link, so the tray is QtTray by construction; the
# definition exists so both shells report the same way in --backend-info and a
# package cannot pass LT_TRAY=kde to this one by mistake and get silence.
target_compile_definitions(traduku PRIVATE LT_TRAY_DEFAULT="qt")
target_compile_definitions(traduku PRIVATE LT_VERSION="${PROJECT_VERSION}")
target_link_libraries(traduku PRIVATE
    lt::app lt::ui lt::core lt::interfaces
    lt::platform_qt lt::platform_wl lt::platform_fdo
    lt::platform_evdev)

install(TARGETS traduku RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
# A GNOME-specific .desktop: the one at the repo root carries KDE-only keys
# (X-KDE-autostart-after, org.kde.* bus name, KDE category).
install(FILES ${CMAKE_SOURCE_DIR}/packaging/gnome/traduku.desktop
        DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)

# ... and the same entry in the session's autostart. This is the GNOME
# flavour's ONLY autostart: the first-run dialog that asks the Plasma user
# "start Traduku at login?" does not exist in this shell yet (Stage 6), and a
# program whose whole interface is a key gesture does nothing at all until
# something starts it - "install, reboot, press Ctrl+C twice, nothing" is the
# silence this project refuses (ARCHITECTURE.md, ROADMAP.md §1). /etc/xdg is
# the distro-owned half of XDG autostart, so the user's own
# ~/.config/autostart still wins and GNOME's Startup Applications list shows
# the entry (NoDisplay is deliberately not set: an entry the user cannot
# switch off is not an entry they installed).
#
# Absolute /etc, not ${CMAKE_INSTALL_SYSCONFDIR}: that one is relative to the
# prefix, and this project installs into /usr - which would put the entry in
# /usr/etc/xdg/autostart, where no session ever reads it (the same shape of
# packaging bug as the udev rule in the multiarch dir below). DESTDIR is still
# honoured, so dpkg and makepkg both stage it correctly.
install(FILES ${CMAKE_SOURCE_DIR}/packaging/gnome/traduku.desktop
        DESTINATION /etc/xdg/autostart)

# The gesture reads /dev/input/event*, and nothing else in the stack does this
# for keyboards: systemd's own 70-uaccess.rules tags joysticks, and
# 73-seat-late.rules only applies the ACL for devices tagged by then. A
# package that installs the binary without this rule installs a translator
# that cannot see the keyboard - and "add the user to the input group" is not
# an alternative, that hands every program the user runs a keylogger.
# Not ${CMAKE_INSTALL_LIBDIR}: on Debian that is the multiarch dir
# (/usr/lib/x86_64-linux-gnu), and udev only reads /lib/udev/rules.d (=> /usr/lib
# via the merged-usr symlink) plus /etc/udev/rules.d. A rule installed into the
# multiarch path is invisible and the failure looks like "the gesture is
# broken on the packaged build only".
install(FILES ${CMAKE_SOURCE_DIR}/packaging/udev/70-traduku-uaccess.rules
        DESTINATION ${CMAKE_INSTALL_PREFIX}/lib/udev/rules.d)
