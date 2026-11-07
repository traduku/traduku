# Platform-agnostic composition root (linked into every DE shell).
# Platform-agnostic composition root (linked into every DE shell).
# cli.cpp/BackendInfo.cpp are shared by the kde and gnome shells - parsing
# and diagnostics must not be duplicated per shell (see tests/unit/tst_cli.cpp).
add_library(lt_app STATIC AppController.cpp SelfTest.cpp cli.cpp BackendInfo.cpp
                 SingleInstance.cpp TrayChoice.cpp TrayChoice.h)
add_library(lt::app ALIAS lt_app)
target_include_directories(lt_app PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(lt_app PUBLIC lt::core lt::ui lt::interfaces lt::platform_qt lt::platform_wl
                    Qt6::Widgets Qt6::DBus)
