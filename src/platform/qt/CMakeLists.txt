# Qt-native clipboard (works on X11 and as universal fallback).
# Qt-native clipboard (works on X11 and as universal fallback) plus the
# session probe that decides which clipboard/gesture backend to use.
add_library(lt_platform_qt STATIC QtClipboard.cpp XdgAutostart.cpp SessionInfo.cpp)
add_library(lt::platform_qt ALIAS lt_platform_qt)
target_include_directories(lt_platform_qt PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../..)
target_link_libraries(lt_platform_qt PUBLIC Qt6::Gui lt::interfaces)
