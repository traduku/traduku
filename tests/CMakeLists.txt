# lt_add_test(<name> SOURCES a.cpp ... [LIBS lt::core ...] [QT QtTest])
# Registers a QtTest executable with CTest; GUI tests run offscreen.
function(lt_add_test name)
    cmake_parse_arguments(ARG "" "" "SOURCES;LIBS" ${ARGN})
    add_executable(${name} ${ARG_SOURCES})
    target_link_libraries(${name} PRIVATE Qt6::Test ${ARG_LIBS})
    add_test(NAME ${name} COMMAND ${name})
    set_tests_properties(${name} PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_LOGGING_RULES=*.debug=false")
endfunction()

# Harness smoke test: proves QtTest + CTest + offscreen GUI work.
lt_add_test(tst_harness SOURCES tst_harness.cpp LIBS Qt6::Widgets)
