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
lt_add_test(tst_llmstreamclient SOURCES integration/tst_llmstreamclient.cpp integration/mocksseserver.h
    LIBS lt::core Qt6::Network)
target_include_directories(tst_llmstreamclient PRIVATE integration)
