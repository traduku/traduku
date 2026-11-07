# Per-module test registration (tests live in tests/unit/, integration in
# tests/integration/). lt_add_test wires QtTest + CTest + offscreen QPA.

function(lt_add_test name)
    cmake_parse_arguments(ARG "" "" "SOURCES;LIBS" ${ARGN})
    add_executable(${name} ${ARG_SOURCES})
    target_link_libraries(${name} PRIVATE Qt6::Test ${ARG_LIBS})
    add_test(NAME ${name} COMMAND ${name})
    set_tests_properties(${name} PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_LOGGING_RULES=*.debug=false")
endfunction()

# Harness smoke test: proves QtTest + CTest + offscreen GUI work.
lt_add_test(tst_harness SOURCES unit/tst_harness.cpp LIBS Qt6::Widgets)

# M1: core languages + settings
lt_add_test(tst_languages SOURCES unit/tst_languages.cpp LIBS lt::core)
lt_add_test(tst_settings  SOURCES unit/tst_settings.cpp  LIBS lt::core)

# M2: core double-Ctrl+C detector
lt_add_test(tst_doublepress SOURCES unit/tst_doublepressdetector.cpp LIBS lt::core)

# M3: core streaming SSE parser
lt_add_test(tst_sseparser SOURCES unit/tst_sseparser.cpp LIBS lt::core)

# M4: streaming client (integration: in-process mock SSE server)
lt_add_test(tst_llmstreamclient SOURCES integration/tst_llmstreamclient.cpp
    integration/mocksseserver.h
    LIBS lt::core Qt6::Network)
target_include_directories(tst_llmstreamclient PRIVATE integration)

# M5: translation engine (prompt + orchestration, integration via mock server)
lt_add_test(tst_translationengine SOURCES integration/tst_translationengine.cpp
    integration/mocksseserver.h
    LIBS lt::core Qt6::Network)
target_include_directories(tst_translationengine PRIVATE integration)

# M6: clipboard interfaces + backends (integration: fake tool runner)
lt_add_test(tst_clipboards SOURCES integration/tst_clipboards.cpp
    LIBS lt::platform_wl lt::platform_qt lt::interfaces Qt6::Widgets)

# M7: core sound player (events, theme resolution, backend chain)
lt_add_test(tst_soundplayer SOURCES unit/tst_soundplayer.cpp LIBS lt::core)

# M8: UI windows (offscreen)
lt_add_test(tst_translatorwindow SOURCES unit/tst_translatorwindow.cpp
    LIBS lt::ui Qt6::Widgets)
lt_add_test(tst_settingsdialog SOURCES unit/tst_settingsdialog.cpp
    LIBS lt::ui Qt6::Widgets)

# M9: shortcut monitor (core glue) + X11 hotkey backend pipeline
lt_add_test(tst_shortcutmonitor SOURCES unit/tst_shortcutmonitor.cpp
    LIBS lt::core Qt6::Widgets)
target_include_directories(tst_shortcutmonitor PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)
if(LT_BUILD_X11)
    lt_add_test(tst_x11hotkey SOURCES integration/tst_x11hotkey.cpp
        LIBS lt::platform_x11)
endif()

# M10: full app pipeline (hotkey -> stream -> window -> clipboard -> sounds)
lt_add_test(tst_appcontroller SOURCES integration/tst_appcontroller.cpp
    integration/mocksseserver.h ../src/app/AppController.h
    LIBS lt::app lt::ui lt::core Qt6::Network)
target_include_directories(tst_appcontroller PRIVATE integration ${CMAKE_CURRENT_SOURCE_DIR}/../src)

# M10: XDG autostart (cross-DE, Qt-only)
lt_add_test(tst_xdgautostart SOURCES unit/tst_xdgautostart.cpp
    LIBS lt::platform_qt)

# BUG-1: unicode escape safety net (core)
lt_add_test(tst_unicodeescapes SOURCES unit/tst_unicodeescapes.cpp LIBS lt::core)

# M12: core chunker (translation units, join==identity)
lt_add_test(tst_chunker SOURCES unit/tst_chunker.cpp LIBS lt::core)

# M13: translation ledger (chunk states, incremental re-edit diff)
lt_add_test(tst_translationledger SOURCES unit/tst_translationledger.cpp LIBS lt::core)

# M15: chunked incremental pipeline (streaming mock)
lt_add_test(tst_chunkedtranslator
    SOURCES integration/tst_chunkedtranslator.cpp integration/mocksseserver.h
    LIBS lt::core Qt6::Network)

# M16: chunk aligner (whole-text result -> per-chunk mapping)
lt_add_test(tst_aligner SOURCES unit/tst_aligner.cpp LIBS lt::core)

# G1 (GNOME plan, Stage 1): shared CLI parsing - both shells use it, so it
# is tested instead of duplicated per shell.
lt_add_test(tst_cli SOURCES unit/tst_cli.cpp LIBS lt::app Qt6::Core)
target_include_directories(tst_cli PRIVATE ${CMAKE_SOURCE_DIR}/src)

# G1: --backend-info rendering (the operator's "why is nothing happening"
# output; tests/e2e greps these lines).
lt_add_test(tst_backendinfo SOURCES unit/tst_backendinfo.cpp LIBS lt::app)
target_include_directories(tst_backendinfo PRIVATE ${CMAKE_SOURCE_DIR}/src)

# G1: one-shot `traduku --translate` (the mode scripts and e2e use)
lt_add_test(tst_oneshot SOURCES integration/tst_oneshot.cpp
    integration/mocksseserver.h
    LIBS lt::app lt::core Qt6::Network)
target_include_directories(tst_oneshot PRIVATE integration ${CMAKE_SOURCE_DIR}/src)

# GNOME shell tray: QSystemTrayIcon with the KDE tray's behaviour (D3)
lt_add_test(tst_qttray SOURCES unit/tst_qttray.cpp
    LIBS lt::platform_qt lt::core Qt6::Widgets Qt6::DBus)
target_include_directories(tst_qttray PRIVATE ${CMAKE_SOURCE_DIR}/src)

# Stage 6b: the KDE and Qt trays must stay interchangeable until KdeTray is
# deleted - same interface, same menu, same intents. Links KF6, so it is
# configured only when the KDE layer is.
if(TARGET lt::platform_qt)
    # The menu contract the KDE tray established, pinned on the Qt one.
    lt_add_test(tst_trayparity SOURCES unit/tst_trayparity.cpp
        LIBS lt::platform_qt lt::core Qt6::Widgets Qt6::DBus)
    target_include_directories(tst_trayparity PRIVATE ${CMAKE_SOURCE_DIR}/src)
endif()

# LT_TRAY (build default) vs TRADUKU_TRAY (per-run override for the Stage 6b
# comparison): pure, so the choice is tested rather than trusted.
lt_add_test(tst_traychoice SOURCES unit/tst_traychoice.cpp LIBS lt::app lt::core)
target_include_directories(tst_traychoice PRIVATE ${CMAKE_SOURCE_DIR}/src)

# The switch itself is a build-time guarantee: a typo must fail at configure.
add_test(NAME cmake_lt_tray_option
         COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/cmake/test_lt_tray_option.sh
                 ${CMAKE_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR}/cmake-scratch)

add_test(NAME versions_agree
         COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/cmake/test_versions_agree.sh
                 ${CMAKE_SOURCE_DIR})
set_tests_properties(versions_agree PROPERTIES
    ENVIRONMENT "PATH=$ENV{PATH}")
set_tests_properties(cmake_lt_tray_option PROPERTIES TIMEOUT 180)

# selftest --live must use the config file it was handed (--config), not the
# user's default path
lt_add_test(tst_selftest SOURCES integration/tst_selftest.cpp
    integration/mocksseserver.h ../src/app/SelfTest.h
    LIBS lt::app lt::core lt::ui Qt6::Network Qt6::Widgets)
target_include_directories(tst_selftest PRIVATE integration ${CMAKE_SOURCE_DIR}/src)

# G1: session detection (QPA plugin name is not the session; offscreen CI
# must still see wayland/x11 so backends are picked correctly)
lt_add_test(tst_sessioninfo SOURCES unit/tst_sessioninfo.cpp LIBS lt::platform_qt)

# G2: FDO notifications (GNOME ships no KNotification; org.freedesktop.
# Notifications is what the shell actually shows). Tests own a private bus
# (fakebus.h) because the desktop already owns the well-known name.
lt_add_test(tst_fdonotifier SOURCES integration/tst_fdonotifier.cpp
    integration/fakebus.h
    LIBS lt::platform_fdo lt::interfaces Qt6::DBus)
target_include_directories(tst_fdonotifier PRIVATE integration)

# G2: single instance (D5) - session-bus name as the ownership token plus a
# QLocalServer activation channel
lt_add_test(tst_singleinstance SOURCES integration/tst_singleinstance.cpp
    integration/fakebus.h
    LIBS lt::app Qt6::DBus)
target_include_directories(tst_singleinstance PRIVATE integration)

# G3: /dev/input gesture backend (D1) - fake DeviceFinder + raw input_event
# bytes, so no /dev access and no uaccess rule is needed to test it
lt_add_test(tst_evdevhotkey SOURCES unit/tst_evdevhotkey.cpp
    LIBS lt::platform_evdev lt::core lt::interfaces Qt6::Core)
