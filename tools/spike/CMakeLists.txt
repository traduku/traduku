# Stage 0 diagnostics (ROADMAP.md §3). These are MANUAL tools, not
# product code and not ctest suites: they answer "which backend can actually
# work on this compositor" and their output is recorded in ROADMAP.md.
# Nothing here is installed; nothing here is linked into traduku.

add_executable(clip-spike clip-spike.cpp)
target_link_libraries(clip-spike PRIVATE Qt6::Widgets)
set_target_properties(clip-spike PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(SPIKE_X11 QUIET IMPORTED_TARGET x11)
    if(SPIKE_X11_FOUND)
        target_link_libraries(clip-spike PRIVATE PkgConfig::SPIKE_X11)
        target_compile_definitions(clip-spike PRIVATE HAVE_X11)
    endif()
endif()

add_executable(evdev-spike evdev-spike.cpp
    ${CMAKE_SOURCE_DIR}/src/core/doublepressdetector.cpp)
target_include_directories(evdev-spike PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(evdev-spike PRIVATE Qt6::Core)
set_target_properties(evdev-spike PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# Stage 5 pre-work: which tray protocol can actually paint an icon and a menu
# here (Qt's own tray, and our own SNI client). The adaptors are generated from
# the spec XML - see xml/README.md for the command and for what it caught.
add_executable(tray-spike
    tray-spike.cpp tray-object.cpp tray-object.h
    snitypes.cpp snitypes.h spikelog.cpp spikelog.h
    sni-adaptor-impl.cpp sni-adaptor.h
    menu-adaptor-impl.cpp menu-adaptor.h)
target_include_directories(tray-spike PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(tray-spike PRIVATE Qt6::Widgets Qt6::DBus)
set_target_properties(tray-spike PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin AUTOMOC ON)

# python, no build step - copied next to the binaries so one directory holds
# the whole Stage 0 toolkit
configure_file(portal-spike.py ${CMAKE_BINARY_DIR}/bin/portal-spike.py COPYONLY)
