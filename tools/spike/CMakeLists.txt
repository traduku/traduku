# Stage 0 diagnostics (GNOME_PLAN.md §9). These are MANUAL tools, not
# product code and not ctest suites: they answer "which backend can actually
# work on this compositor" and their output is recorded in GNOME_PLAN.md.
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

# python, no build step - copied next to the binaries so one directory holds
# the whole Stage 0 toolkit
configure_file(portal-spike.py ${CMAKE_BINARY_DIR}/bin/portal-spike.py COPYONLY)
