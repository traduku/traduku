# The GNOME application: thin composition root, and deliberately NO KF6
# (GNOME_PLAN.md D3 - GNOME ships none of it; tray/notifications/shortcut
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
