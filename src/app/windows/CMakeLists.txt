# The Windows shell: traduku.exe.
#
# GUI subsystem. That is not cosmetic: a console-subsystem binary flashes a
# black window every time the tray app starts, and the tray app is what people
# autostart. The console modes are not lost - app/windows/main.cpp attaches to
# the parent console, and the decision behind it is tested in
# tests/unit/tst_appflavour.cpp (decideConsoleAttach).
add_executable(traduku WIN32 main.cpp)
target_include_directories(traduku PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../..)
target_link_libraries(traduku PRIVATE
    lt::app lt::ui lt::core lt::interfaces
    lt::platform_qt lt::platform_win
    Qt6::Widgets Qt6::Network Qt6::Multimedia)
# LT_TRAY_DEFAULT is not cosmetic: without it the tray switch is a CMake
# variable nobody reads, and a package that asked for no tray would ship one
# anyway. tests/cmake/test_lt_tray_option.sh greps compile_commands.json for
# exactly this define, on the target the platform actually builds.
target_compile_definitions(traduku PRIVATE
    LT_VERSION="${PROJECT_VERSION}"
    LT_TRAY_DEFAULT="${LT_TRAY}")
set_target_properties(traduku PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

install(TARGETS traduku RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES ${CMAKE_SOURCE_DIR}/LICENSE
        DESTINATION ${CMAKE_INSTALL_DATADIR}/licenses/${PROJECT_NAME} OPTIONAL)
# The three cues are the system's own .wav files under %WINDIR%\Media, so
# nothing audio ships with the package - unlike the Linux flavours, which read
# the freedesktop theme the distro installs. Stated here so nobody "adds the
# missing sounds" and starts shipping a second set.
