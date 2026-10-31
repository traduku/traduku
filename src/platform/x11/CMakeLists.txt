# Passive keyboard monitor for X11 and XWayland sessions.
add_library(lt_platform_x11 STATIC X11HotkeyBackend.cpp)
add_library(lt::platform_x11 ALIAS lt_platform_x11)
target_include_directories(lt_platform_x11 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../..)
target_link_libraries(lt_platform_x11 PUBLIC lt::core lt::interfaces PkgConfig::X11)
