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
