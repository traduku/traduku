cmake_minimum_required(VERSION 3.21)

project(llm-translate VERSION 0.1.0 DESCRIPTION
    "Clipboard translator powered by any OpenAI-compatible LLM (streaming)"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(QT_MIN_VERSION "6.4.0")
find_package(Qt6 ${QT_MIN_VERSION} REQUIRED
    COMPONENTS Core Gui Widgets Network Multimedia Test Core5Compat)
find_package(PkgConfig REQUIRED)
include(CTest)
include(GNUInstallDirs)

# Declared before any add_subdirectory so tests see the switches.
option(LT_BUILD_X11 "Build the X11 (XI2/RECORD) passive hotkey backend" ON)
option(LT_BUILD_KDE "Build the KDE Platform backend and the tray application" ON)

add_subdirectory(src)
add_subdirectory(tests)

# ---- install rules (used by the distro packages) --------------------------
if(TARGET kde-llm-translate)
    install(TARGETS kde-llm-translate RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
    install(FILES llm-translate.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
    install(FILES llm-translate.notifyrc DESTINATION ${CMAKE_INSTALL_DATADIR}/knotifications6)
    install(FILES LICENSE DESTINATION ${CMAKE_INSTALL_DATADIR}/licenses/${PROJECT_NAME}
            OPTIONAL)
endif()
