# The KDE application: thin composition root (see PLAN.md).
add_executable(traduku main.cpp)
# Which tray main.cpp instantiates (see LT_TRAY / TRADUKU_TRAY).
target_compile_definitions(traduku PRIVATE LT_TRAY_DEFAULT="${LT_TRAY}")
# The GNOME shell got this in Stage 1; without it `traduku --version` printed
# "traduku" with no number, which is half of "which build is in my panel".
target_compile_definitions(traduku PRIVATE LT_VERSION="${PROJECT_VERSION}")
target_link_libraries(traduku PRIVATE
    lt::app lt::ui lt::core lt::interfaces
    lt::platform_kde lt::platform_qt lt::platform_wl)
if(LT_BUILD_X11)
    target_link_libraries(traduku PRIVATE lt::platform_x11)
    target_compile_definitions(traduku PRIVATE LT_HAVE_X11)
endif()
install(TARGETS traduku RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

install(FILES ${CMAKE_SOURCE_DIR}/traduku.desktop
        DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
install(FILES ${CMAKE_SOURCE_DIR}/traduku.notifyrc
        DESTINATION ${CMAKE_INSTALL_DATADIR}/knotifications6
        RENAME traduku.notifyrc)
