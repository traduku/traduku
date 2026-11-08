# Platform-agnostic composition root (linked into every DE shell).
# cli.cpp/BackendInfo.cpp are shared by all shells - parsing and diagnostics
# must not be duplicated per shell (see tests/unit/tst_cli.cpp).
#
# The one exception is app/backends/, which names the two backends whose
# *identity* is the platform (clipboard, autostart, sounds). Exactly one of
# those files is compiled, so the CLI, the selftest and the tray on every OS
# still run through one code path - see app/PlatformBackends.h for why the
# split lives here and not inside a #ifdef.
if(WIN32)
    set(LT_APP_PLATFORM_SOURCES backends/PlatformBackendsWin.cpp)
    set(LT_APP_PLATFORM_LIBS lt::platform_win)
else()
    set(LT_APP_PLATFORM_SOURCES backends/PlatformBackendsUnix.cpp)
    set(LT_APP_PLATFORM_LIBS lt::platform_wl)
endif()

add_library(lt_app STATIC
    AppController.cpp SelfTest.cpp cli.cpp BackendInfo.cpp AppFlavour.cpp
    SingleInstance.cpp TrayChoice.cpp TrayChoice.h
    ${LT_APP_PLATFORM_SOURCES})
add_library(lt::app ALIAS lt_app)
target_include_directories(lt_app PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(lt_app PUBLIC lt::core lt::ui lt::interfaces lt::platform_qt
                    ${LT_APP_PLATFORM_LIBS} Qt6::Widgets Qt6::DBus)
