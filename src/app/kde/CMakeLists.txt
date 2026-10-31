# The KDE application: thin composition root (see PLAN.md).
add_executable(kde-llm-translate main.cpp)
target_link_libraries(kde-llm-translate PRIVATE
    lt::app lt::ui lt::core lt::interfaces
    lt::platform_kde lt::platform_qt lt::platform_wl)
if(LT_BUILD_X11)
    target_link_libraries(kde-llm-translate PRIVATE lt::platform_x11)
    target_compile_definitions(kde-llm-translate PRIVATE LT_HAVE_X11)
endif()
install(TARGETS kde-llm-translate RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
