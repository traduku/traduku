# Wayland clipboard through wl-clipboard tools + the clipboard factory.
add_library(lt_platform_wl STATIC WlClipboard.cpp ClipboardFactory.cpp)
add_library(lt::platform_wl ALIAS lt_platform_wl)
target_include_directories(lt_platform_wl PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../..)
# The factory may choose the X11 reader, so the choice must be linkable in
# every shell - wl is not "the wayland-only" library, it is where the clipboard
# decision lives, and that decision now includes XWayland.
target_link_libraries(lt_platform_wl PUBLIC lt::interfaces lt::platform_qt lt::platform_x11 Qt6::Gui Qt6::Core5Compat Qt6::Network)
