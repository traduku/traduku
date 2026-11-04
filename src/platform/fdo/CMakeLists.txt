# Freedesktop-spec D-Bus services that every desktop provides. No KF6 and no
# libnotify here: notifications today, SNI tray (dbusmenu) next.
add_library(lt_platform_fdo STATIC FdoNotifier.cpp)
add_library(lt::platform_fdo ALIAS lt_platform_fdo)
target_include_directories(lt_platform_fdo PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../..)
target_link_libraries(lt_platform_fdo PUBLIC lt::interfaces Qt6::Core Qt6::DBus)
