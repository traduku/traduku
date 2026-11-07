# Qt-native pieces: the clipboard fallback, the session probe that decides
# which clipboard/gesture backend to use, the XDG autostart entry, and the
# tray (QSystemTrayIcon, which on Wayland is Qt's generic DBus SNI client).
add_library(lt_platform_qt STATIC
    QtClipboard.cpp XdgAutostart.cpp SessionInfo.cpp QtTray.cpp QtTray.h)
add_library(lt::platform_qt ALIAS lt_platform_qt)
target_include_directories(lt_platform_qt PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../..)
target_link_libraries(lt_platform_qt PUBLIC Qt6::Gui Qt6::Widgets Qt6::DBus lt::core lt::interfaces)
