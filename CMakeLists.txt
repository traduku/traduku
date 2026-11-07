cmake_minimum_required(VERSION 3.21)

project(traduku VERSION 0.2.0 DESCRIPTION
    "Clipboard translator powered by any OpenAI-compatible LLM (streaming)"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(QT_MIN_VERSION "6.4.0")
find_package(Qt6 ${QT_MIN_VERSION} REQUIRED
    COMPONENTS Core Gui Widgets Network Multimedia Test Core5Compat DBus)
find_package(PkgConfig REQUIRED)
include(CTest)
include(GNUInstallDirs)

# Declared before any add_subdirectory so tests see the switches.
option(LT_BUILD_X11 "Build the X11 (XI2/RECORD) passive hotkey backend" ON)
option(LT_BUILD_KDE "Build the KDE Platform backend and the tray application" ON)
option(LT_BUILD_SPIKES "Build the tools/spike Stage 0 diagnostics (manual tools, not tests)" ON)

# Which DE shell the `traduku` executable is (GNOME_PLAN.md D5). One binary
# name, one shell per build: two targets both called `traduku` would fail
# CMake anyway, so the choice is made here and enforced in src/.
#   auto  - KDE when the KF6 stack is present, GNOME otherwise
#   kde   - require KF6, fail loudly if it is missing
#   gnome - never look for KF6 (GNOME ships none; see GNOME_PLAN.md D3)
set(LT_SHELL "auto" CACHE STRING "Shell to build: auto|kde|gnome")
set_property(CACHE LT_SHELL PROPERTY STRINGS auto kde gnome)
if(NOT LT_SHELL MATCHES "^(auto|kde|gnome)$")
    message(FATAL_ERROR "LT_SHELL must be one of auto, kde, gnome (got '${LT_SHELL}')")
endif()

# Which tray the KDE shell shows (GNOME_PLAN.md Stage 6b). The GNOME shell has
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
if(LT_BUILD_SPIKES)
    add_subdirectory(tools/spike)
endif()

# ---- install rules (used by the distro packages) --------------------------
# The binary, its .desktop and notification data are installed by the shell
# that was built (src/app/kde, src/app/gnome) - a GNOME build must not ship
# KDE files, and one build contains exactly one shell (LT_SHELL).
install(FILES LICENSE DESTINATION ${CMAKE_INSTALL_DATADIR}/licenses/${PROJECT_NAME}
        OPTIONAL)
