# Passive keyboard monitor for X11 and XWayland sessions.
add_library(lt_platform_x11 STATIC
    X11HotkeyBackend.cpp
    # Clipboard through X selections: the reader for sessions where Qt sees
    # nothing (wayland background app) and the one X11 promise rides on.
    X11Clipboard.cpp
)
add_library(lt::platform_x11 ALIAS lt_platform_x11)
target_include_directories(lt_platform_x11 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../..)
target_link_libraries(lt_platform_x11 PUBLIC lt::core lt::interfaces PkgConfig::X11)

# X RECORD key emitter: a separate process because libXtst's blocking
# XRecordEnableContext cannot be interrupted from within the same process
# (see X11HotkeyBackend.h). Pure Xlib: no Qt, fast start.
add_executable(traduku-record-keys record-keys-main.cpp RecordKeyEmitter.cpp)
target_include_directories(traduku-record-keys PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(traduku-record-keys PRIVATE PkgConfig::X11)
set_target_properties(traduku-record-keys PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
# The GNOME shell listens on /dev/input (evdev) and never talks to X, so it
# must not ship a binary that advertises an X key listener it cannot use -
# packaged installs are read as a description of what the program does.
if(NOT LT_SHELL_EFFECTIVE STREQUAL "gnome")
    install(TARGETS traduku-record-keys RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()
