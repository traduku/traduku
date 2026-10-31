# Wayland clipboard through wl-clipboard tools + the clipboard factory.
add_library(lt_platform_wl STATIC WlClipboard.cpp ClipboardFactory.cpp)
add_library(lt::platform_wl ALIAS lt_platform_wl)
target_include_directories(lt_platform_wl PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../..)
target_link_libraries(lt_platform_wl PUBLIC lt::interfaces lt::platform_qt Qt6::Gui)
