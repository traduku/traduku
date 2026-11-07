# KDE Plasma layer: notifications and global shortcuts.
#
# The tray is NOT here: KdeTray (KStatusNotifierItem) was removed once the Qt
# tray (platform/qt/QtTray.cpp) matched it entry for entry in Plasma. KF6's item
# also segfaulted when destroyed with a live session bus, which is what
# `Quit` from the released 0.2 build did.
add_library(lt_platform_kde STATIC
    KdeNotifier.cpp
    KdeGlobalShortcut.cpp
)
add_library(lt::platform_kde ALIAS lt_platform_kde)
target_include_directories(lt_platform_kde PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../..)
target_link_libraries(lt_platform_kde PUBLIC
    lt::core lt::interfaces
    KF6::Notifications KF6::GlobalAccel KF6::DBusAddons)
