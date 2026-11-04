# /dev/input gesture observation (GNOME_PLAN.md D1). No KF6, no libevdev:
# two ioctls and a read loop - all gesture logic stays in core's
# DoublePressDetector, shared with the X11 backend.
add_library(lt_platform_evdev STATIC DeviceFinder.cpp EvdevHotkeyBackend.cpp)
add_library(lt::platform_evdev ALIAS lt_platform_evdev)
target_include_directories(lt_platform_evdev PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../..)
target_link_libraries(lt_platform_evdev PUBLIC lt::interfaces lt::core Qt6::Core)
