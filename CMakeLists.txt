cmake_minimum_required(VERSION 3.21)

project(traduku VERSION 0.4.0 DESCRIPTION
    "Clipboard translator powered by any OpenAI-compatible LLM (streaming)"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(QT_MIN_VERSION "6.4.0")
# Multimedia is asked for on Windows only, and only because something there
# actually links it: the Windows sound backend plays the system .wav files
# through QMediaPlayer (WMF). Nothing else in the tree touches Qt Multimedia -
# the freedesktop path detached-launches pw-play/paplay/aplay - so requiring
# it everywhere made every Linux package carry a dependency no code used.
set(LT_QT_COMPONENTS Core Gui Widgets Network Test Core5Compat DBus)
if(WIN32)
    list(APPEND LT_QT_COMPONENTS Multimedia)
endif()
find_package(Qt6 ${QT_MIN_VERSION} REQUIRED COMPONENTS ${LT_QT_COMPONENTS})

# pkg-config exists to answer the X11 question. Asking it unconditionally made
# the first Windows configure die in FindPkgConfig before CMake had said a
# word about the port - the worst kind of first error: it names a tool, not a
# decision. X11 is off on Windows, so the dependency is now taken where it is
# needed (ROADMAP.md §7 W0).
if(NOT WIN32)
    find_package(PkgConfig REQUIRED)
endif()
include(CTest)
include(GNUInstallDirs)

# Declared before any add_subdirectory so tests see the switches.
# The defaults are per-platform because the layers they switch do not exist on
# every OS. A switch that can stay ON while the thing it switches cannot
# exist is a switch that eventually lies in --backend-info.
if(WIN32)
    set(LT_BUILD_X11_DEFAULT OFF)
    set(LT_BUILD_KDE_DEFAULT OFF)
else()
    set(LT_BUILD_X11_DEFAULT ON)
    set(LT_BUILD_KDE_DEFAULT ON)
endif()
option(LT_BUILD_X11 "Build the X11 (XI2/RECORD) passive hotkey backend" ${LT_BUILD_X11_DEFAULT})
option(LT_BUILD_KDE "Build the KDE Platform backend and the tray application" ${LT_BUILD_KDE_DEFAULT})

if(WIN32 AND LT_BUILD_X11)
    message(FATAL_ERROR
        "LT_BUILD_X11=ON on Windows: there is no X server to observe. The X11 "
        "layer is off there by default; do not turn it on.")
endif()
if(WIN32 AND LT_BUILD_KDE)
    message(FATAL_ERROR
        "LT_BUILD_KDE=ON on Windows: KNotification and KGlobalAccel have no "
        "KDE session to talk to here (ROADMAP.md §7.2).")
endif()
option(LT_BUILD_SPIKES "Build the tools/spike Stage 0 diagnostics (manual tools, not tests)" ON)

# Which DE shell the `traduku` executable is (ROADMAP.md §2). One binary
# name, one shell per build: two targets both called `traduku` would fail
# CMake anyway, so the choice is made here and enforced in src/.
#   auto  - KDE when the KF6 stack is present, GNOME otherwise
#   kde   - require KF6, fail loudly if it is missing
#   gnome - never look for KF6 (GNOME ships none; see ROADMAP.md §2)
set(LT_SHELL "auto" CACHE STRING "Shell to build: auto|kde|gnome|windows")
set_property(CACHE LT_SHELL PROPERTY STRINGS auto kde gnome windows)
if(NOT LT_SHELL MATCHES "^(auto|kde|gnome|windows)$")
    message(FATAL_ERROR
        "LT_SHELL must be one of auto, kde, gnome, windows (got '${LT_SHELL}')")
endif()
if(WIN32 AND (LT_SHELL STREQUAL "kde" OR LT_SHELL STREQUAL "gnome"))
    message(FATAL_ERROR
        "LT_SHELL=${LT_SHELL} on Windows: that shell wires backends this OS "
        "does not have (evdev and its udev rule, X RECORD, the FDO "
        "notification service). Build the windows shell with "
        "-DLT_SHELL=windows, or build this shell on Linux.")
endif()

# Which tray the KDE shell shows (ROADMAP.md §2). The GNOME shell has
# no KF6 to link, so it always uses QtTray; the value is validated and passed
# through anyway, so a packaging typo fails at configure time in either
# package rather than shipping the tray nobody tested.
#   kde - KStatusNotifierItem (KF6), the historical default
#   qt  - QSystemTrayIcon, the same code the GNOME shell runs
# TRADUKU_TRAY overrides it per run so the comparison needs no rebuild.
# Which tray a build starts with. "none" is for builds that must not put an
# icon in the panel (kiosk, --translate-only image). There used to be a "kde"
# value selecting KStatusNotifierItem; the Qt tray replaced it in Plasma and the
# KDE one is deleted, so asking for it is now a configuration error - old
# packaging recipes carry `-DLT_TRAY=kde` and must fail here, not silently ship
# something else.
set(LT_TRAY "qt" CACHE STRING "Tray implementation for the KDE shell: qt|none")
set_property(CACHE LT_TRAY PROPERTY STRINGS qt none)
if(NOT LT_TRAY MATCHES "^(qt|none)$")
    message(FATAL_ERROR "LT_TRAY must be one of qt, none (got '${LT_TRAY}'). "
        "The KDE (KStatusNotifierItem) tray was removed; both shells use the Qt "
        "one, which registers with the same StatusNotifier hosts (Plasma, "
        "GNOME appindicator).")
endif()

add_subdirectory(src)
add_subdirectory(tests)
# The spikes answer "which backend can actually work on this compositor" -
# evdev, wl-clipboard, SNI over the session bus. Windows asks none of those
# questions, and building a tool that advertises an ability the OS does not
# have is how diagnostics start lying, so they are configured on Linux only
# (and said out loud, rather than silently missing from the build).
if(LT_BUILD_SPIKES AND NOT WIN32)
    add_subdirectory(tools/spike)
elseif(LT_BUILD_SPIKES)
    message(STATUS "Traduku spikes: not built on Windows (they probe "
                   "/dev/input, wl-clipboard and the SNI bus)")
endif()

# ---- install rules (used by the distro packages) --------------------------
# The binary, its .desktop and notification data are installed by the shell
# that was built (src/app/kde, src/app/gnome) - a GNOME build must not ship
# KDE files, and one build contains exactly one shell (LT_SHELL).
install(FILES LICENSE DESTINATION ${CMAKE_INSTALL_DATADIR}/licenses/${PROJECT_NAME}
        OPTIONAL)
