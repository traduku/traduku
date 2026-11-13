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

# Which QApplication flavour, and what QPA a console mode needs: shared by both
# shells (src/app/AppFlavour.cpp). The shells used to keep their own copies and
# only one grew the headless branch - --backend-info aborted without a display.
lt_add_test(tst_appflavour SOURCES unit/tst_appflavour.cpp LIBS lt::app Qt6::Gui)

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
if(NOT WIN32)
    lt_add_test(tst_clipboards SOURCES integration/tst_clipboards.cpp
        LIBS lt::platform_wl lt::platform_qt lt::interfaces Qt6::Widgets)
endif()

# M6b: the target/charset negotiation both clipboard readers share. It is its
# own suite because its two callers are in different platform layers: testing
# it through either one would make the shared rule look platform-specific.
lt_add_test(tst_texttargets SOURCES unit/tst_texttargets.cpp LIBS lt::core)

# M6c: how an X11 clipboard reader finds a server when it was started by a
# wayland session (DISPLAY/XAUTHORITY through XWayland's /proc entry). Pure
# string work, so it is unit-tested headless; tst_x11clipboard drives a real
# server.
if(NOT WIN32)
add_executable(fake-xwayland support/fake-xwayland.cpp)   # /proc camouflage
lt_add_test(tst_x11session SOURCES unit/tst_x11session.cpp LIBS lt::platform_x11)
endif()
# M6d: the X selection protocol against a real server. Skips (loudly) on a
# headless host; TRADUKU_REQUIRE_X11=1 in the container gate turns that skip
# into a failure, so the code is never shipped untested.
if(NOT WIN32)
    lt_add_test(tst_x11clipboard SOURCES integration/tst_x11clipboard.cpp
        LIBS lt::platform_x11 Threads::Threads)
    set_tests_properties(tst_x11session PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;FAKE_XWAYLAND=$<TARGET_FILE:fake-xwayland>")
endif()

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

# M10: XDG autostart (cross-DE, Qt-only). Unix only, and not because the suite
# is flaky: XDG_CONFIG_HOME is an environment variable Windows does not read, so
# on that OS the test would aim at %LOCALAPPDATA% and grade a file nobody
# promised to write. The Windows autostart has its own rows
# (tst_winautostart, tst_winautostart_registry) and --selftest asks the registry
# there - see app/PlatformBackends.h.
if(NOT WIN32)
    lt_add_test(tst_xdgautostart SOURCES unit/tst_xdgautostart.cpp
        LIBS lt::platform_qt)
endif()

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

# The two shell gates need a bash. Where there is none, the test is registered
# as a failing-by-own-admission script rather than dropped: a suite that went
# from 37 rows to 35 because an interpreter was missing is a suite that quietly
# stopped checking versions.
find_program(BASH_PROGRAM NAMES bash)
if(NOT BASH_PROGRAM)
    message(WARNING "no bash: cmake_lt_tray_option and versions_agree cannot run "
                    "here, which means two gates are not gating")
    # Register them anyway, as a command CMake cannot launch: BAD_COMMAND in the
    # report is a loud hole, while a suite that went from 37 rows to 35 because
    # an interpreter was missing is a suite that quietly stopped checking
    # versions.
    set(BASH_PROGRAM bash-not-found)
endif()

# The interpreter is named, not left to the shebang: Windows has no idea what to
# do with a .sh file, and on this platform "the gate does not run" was showing
# up as BAD_COMMAND rather than as a decision.
#
# The switch itself is a build-time guarantee: a typo must fail at configure.
# Which LT_SHELL values exist, and how to find a compiler, are facts the script
# cannot guess from the source tree.
if(WIN32)
    set(_traduku_test_shell windows)
else()
    set(_traduku_test_shell gnome)
endif()
get_filename_component(_bash_dir "${BASH_PROGRAM}" DIRECTORY)
# The packaging gate asks git which ignore rule matched, so git has to be
# findable by name - the same reason bash is named explicitly: a gate that
# cannot launch its interpreter reports nothing at all.
find_program(GIT_PROGRAM NAMES git)
if(GIT_PROGRAM)
    get_filename_component(_git_dir "${GIT_PROGRAM}" DIRECTORY)
else()
    set(_git_dir "")
endif()
# How a *fresh* configure of this tree finds Qt: the same prefix this build was
# configured with. Without it the child cmake fails for a reason that has
# nothing to do with what the gate is checking.
set(_tray_test_cmake_args
    -G "${CMAKE_GENERATOR}"
    "-DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}"
    "-DCMAKE_PREFIX_PATH=${CMAKE_PREFIX_PATH}")
# The toolchain too, when it was named: a MinGW configure finds g++ only
# through PATH, and a child configure that picks a different compiler than the
# one this build was configured with is testing a build nobody ships.
if(DEFINED CMAKE_C_COMPILER)
    list(APPEND _tray_test_cmake_args "-DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}")
endif()
if(DEFINED CMAKE_CXX_COMPILER)
    list(APPEND _tray_test_cmake_args "-DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}")
endif()
if(DEFINED CMAKE_MAKE_PROGRAM)
    list(APPEND _tray_test_cmake_args "-DCMAKE_MAKE_PROGRAM=${CMAKE_MAKE_PROGRAM}")
endif()

add_test(NAME cmake_lt_tray_option
         COMMAND ${BASH_PROGRAM} ${CMAKE_CURRENT_SOURCE_DIR}/cmake/test_lt_tray_option.sh
                 ${CMAKE_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR}/cmake-scratch
                 ${_traduku_test_shell} ${_tray_test_cmake_args})

add_test(NAME versions_agree
         COMMAND ${BASH_PROGRAM} ${CMAKE_CURRENT_SOURCE_DIR}/cmake/test_versions_agree.sh
                 ${CMAKE_SOURCE_DIR})

# Nothing under packaging/ may be invisible to git: .gitignore's build-* used to
# have no anchor and swallowed build-portable.sh, which kept "the release script"
# a file that existed on one machine. See the script's header.
add_test(NAME packaging_tracked
         COMMAND ${BASH_PROGRAM} ${CMAKE_CURRENT_SOURCE_DIR}/cmake/test_packaging_tracked.sh
                 ${CMAKE_SOURCE_DIR})

# The tray label reached a user as "Traduku 0.3.0 ? tray qt" because a UTF-8
# byte sequence was pasted into a QStringLiteral() - which builds UTF-16, one
# QChar per byte. Every unit test that reads that string agrees with the bug, so
# the only gate that works is one that reads the source. Comments are exempt: a
# comment that quotes the mojibake it is describing is the only way to name that
# character, and a gate that punishes the incident report teaches people to
# delete incident reports.
add_test(NAME string_literals
         COMMAND ${BASH_PROGRAM} ${CMAKE_CURRENT_SOURCE_DIR}/cmake/test_string_literals.sh
                 ${CMAKE_SOURCE_DIR})
set_tests_properties(string_literals PROPERTIES TIMEOUT 120
    ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${_bash_dir}")
# The interpreter's own directory goes on PATH: these are shell scripts using
# sed and head, and a Git bash whose coreutils are not on PATH fails in a way
# that reads like a broken gate rather than a missing toolchain.
#
# ENVIRONMENT_MODIFICATION, not ENVIRONMENT: ENVIRONMENT is itself a ;-list, so
# writing "PATH=a;b;c" through it hands the test a PATH of exactly "a" and
# silently drops every directory after the first - which is how "cmake not
# found" and "c++ is broken" appeared in gates that had nothing to do with
# either. Prepending leaves the rest of the environment alone.
set_tests_properties(versions_agree PROPERTIES
    ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${_bash_dir}")
set_tests_properties(packaging_tracked PROPERTIES TIMEOUT 120
    ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${_bash_dir}:${_git_dir}")
set_tests_properties(cmake_lt_tray_option PROPERTIES TIMEOUT 300
    ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${_bash_dir}")

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
if(NOT WIN32)
    lt_add_test(tst_fdonotifier SOURCES integration/tst_fdonotifier.cpp
        integration/fakebus.h
        LIBS lt::platform_fdo lt::interfaces Qt6::DBus)
    target_include_directories(tst_fdonotifier PRIVATE integration)
endif()

# G2: single instance (D5) - session-bus name as the ownership token plus a
# QLocalServer activation channel
lt_add_test(tst_singleinstance SOURCES integration/tst_singleinstance.cpp
    integration/fakebus.h
    LIBS lt::app Qt6::DBus)
target_include_directories(tst_singleinstance PRIVATE integration)

# G3: /dev/input gesture backend (D1) - fake DeviceFinder + raw input_event
# bytes, so no /dev access and no uaccess rule is needed to test it
if(NOT WIN32)
    lt_add_test(tst_evdevhotkey SOURCES unit/tst_evdevhotkey.cpp
        LIBS lt::platform_evdev lt::core lt::interfaces Qt6::Core)
endif()

# ---------------------------------------------------------------------------
# W2 (ROADMAP.md §7.2): the Windows layer.
#
# The suites below run on every platform on purpose. Each of them tests a
# *decision* - which VK codes make a gesture, who owns a Run value, when a
# balloon counts as delivered, what advice a mute machine gets - and a decision
# that can only be executed on one OS is only ever tested on that one OS. Only
# the last two rows ask the machine anything, and those are Windows rows.
# ---------------------------------------------------------------------------

lt_add_test(tst_wingesture SOURCES unit/tst_wingesture.cpp
    LIBS lt::platform_win lt::core lt::interfaces Qt6::Widgets)
target_include_directories(tst_wingesture PRIVATE ${CMAKE_SOURCE_DIR}/src)

lt_add_test(tst_winautostart SOURCES unit/tst_winautostart.cpp
    LIBS lt::platform_win lt::core lt::interfaces)
target_include_directories(tst_winautostart PRIVATE ${CMAKE_SOURCE_DIR}/src)

lt_add_test(tst_winnotifier SOURCES unit/tst_winnotifier.cpp
    LIBS lt::platform_win lt::core lt::interfaces Qt6::Widgets)
target_include_directories(tst_winnotifier PRIVATE ${CMAKE_SOURCE_DIR}/src)

lt_add_test(tst_winsound SOURCES unit/tst_winsound.cpp
    LIBS lt::platform_win lt::core lt::interfaces)
target_include_directories(tst_winsound PRIVATE ${CMAKE_SOURCE_DIR}/src)

# The sound path AT STARTUP, in a process of its own: the row asks whether the
# shell created the player, and every other sound row in this suite plays a
# cue, which creates it. A row that runs after one is not a row about startup.
# Windows-only because WinMediaPlayer.cpp is Windows-only.
if(WIN32)
    lt_add_test(tst_winsoundstart SOURCES unit/tst_winsoundstart.cpp
        LIBS lt::platform_win lt::core lt::interfaces)
    target_include_directories(tst_winsoundstart PRIVATE ${CMAKE_SOURCE_DIR}/src)
endif()

lt_add_test(tst_winsessioninfo SOURCES unit/tst_winsessioninfo.cpp
    LIBS lt::platform_win lt::core)
target_include_directories(tst_winsessioninfo PRIVATE ${CMAKE_SOURCE_DIR}/src)

# TRADUKU_CLIPBOARD on an OS with one reader: the refusal is the product.
lt_add_test(tst_winbackends SOURCES unit/tst_winbackends.cpp
    LIBS lt::platform_win lt::core)
target_include_directories(tst_winbackends PRIVATE ${CMAKE_SOURCE_DIR}/src)

if(WIN32)
    # A console mode that cannot get a screen must print and leave. Only a
    # Windows row: on Unix the same failure is a message from Qt to stderr, and
    # there is no modal dialog to hang a service session here.
    lt_add_test(tst_consolefatal SOURCES integration/tst_consolefatal.cpp
        LIBS Qt6::Core)
    target_compile_definitions(tst_consolefatal PRIVATE
        TRADUKU_EXE="$<TARGET_FILE:traduku>")
    add_dependencies(tst_consolefatal traduku)

    # The real hook, the real SendInput, the real hive. These are the rows that
    # turn "the policy is right" into "the product works", and on a box with no
    # interactive desktop they skip with the reason rather than timing out.
    # The double-click rows: does the GUI mode start on a desktop, and refuse in
    # Windows terms when there is none. One of the two rows always skips, and
    # that is the honest shape - a build box cannot see a logged-in desktop.
    lt_add_test(tst_wingui SOURCES integration/tst_wingui.cpp
        LIBS lt::platform_win lt::core Qt6::Core)
    target_include_directories(tst_wingui PRIVATE ${CMAKE_SOURCE_DIR}/src)
    target_compile_definitions(tst_wingui PRIVATE TRADUKU_EXE="$<TARGET_FILE:traduku>")
    add_dependencies(tst_wingui traduku)

    lt_add_test(tst_winhook_real SOURCES integration/tst_winhook_real.cpp
        LIBS lt::platform_win lt::core lt::interfaces Qt6::Widgets)
    target_include_directories(tst_winhook_real PRIVATE ${CMAKE_SOURCE_DIR}/src)
    target_link_libraries(tst_winhook_real PRIVATE user32)

    lt_add_test(tst_winautostart_registry
        SOURCES integration/tst_winautostart_registry.cpp
        LIBS lt::platform_win lt::core lt::interfaces)
    target_include_directories(tst_winautostart_registry PRIVATE ${CMAKE_SOURCE_DIR}/src)
    target_link_libraries(tst_winautostart_registry PRIVATE advapi32)
endif()
