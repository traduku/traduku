# KDE Plasma layer: tray, notifications, global shortcuts. The only KF6-UI
# dependent code in the tree; swap for a GNOME layer to port the app.
add_library(lt_platform_kde STATIC
    KdeTray.cpp
    KdeNotifier.cpp
    KdeGlobalShortcut.cpp
)
add_library(lt::platform_kde ALIAS lt_platform_kde)
target_include_directories(lt_platform_kde PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../..)
target_link_libraries(lt_platform_kde PUBLIC
    lt::core lt::interfaces
    KF6::StatusNotifierItem KF6::Notifications KF6::GlobalAccel KF6::DBusAddons)
