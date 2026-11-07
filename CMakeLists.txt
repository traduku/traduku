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
set(LT_TRAY "kde" CACHE STRING "Tray implementation for the KDE shell: kde|qt")
set_property(CACHE LT_TRAY PROPERTY STRINGS kde qt)
if(NOT LT_TRAY MATCHES "^(kde|qt)$")
    message(FATAL_ERROR "LT_TRAY must be one of kde, qt (got '${LT_TRAY}')")
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
