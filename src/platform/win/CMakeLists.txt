# The Windows layer (ROADMAP.md §7.2).
#
# Half of it compiles everywhere, and that is not an accident to preserve: the
# gesture decision, the Run-key rules, the notification counters, the session
# sentence and the sound profile are all *decisions*, and a decision that can
# only be tested on one OS is a decision that only gets tested on one OS. The
# OS-specific part is the three files behind the if(WIN32), which are the only
# places in the port that include windows.h - plus WinMediaPlayer.cpp, the only
# place that links Qt Multimedia.

set(LT_WIN_PORTABLE
    WinGesturePolicy.cpp     # VK codes in, gesture out - no OS in sight
    WinHotkeyBackend.cpp     # queueing, start/stop, counters
    WinNotifier.cpp          # delivered/dropped counters
    WinAutostart.cpp         # Run-value ownership rules
    WinRegistryStore.cpp     # the read-only discipline
    WinSessionInfo.cpp       # the sentence --backend-info prints
    WinSound.cpp             # the .wav candidates and the advice words
    WinClipboardPolicy.cpp   # what TRADUKU_CLIPBOARD must refuse, and in words
)

add_library(lt_platform_win STATIC ${LT_WIN_PORTABLE})
add_library(lt::platform_win ALIAS lt_platform_win)
target_include_directories(lt_platform_win PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../..)
target_link_libraries(lt_platform_win PUBLIC lt::interfaces lt::core Qt6::Core Qt6::Gui Qt6::Widgets)

if(WIN32)
    target_sources(lt_platform_win PRIVATE
        WinKeyHook.cpp       # SetWindowsHookExW(WH_KEYBOARD_LL)
        WinConsole.cpp       # AttachConsole for --translate from cmd
        WinSessionFacts.cpp  # WTSGetActiveConsoleSessionId & friends
        WinMediaPlayer.cpp   # QMediaPlayer over WMF, powershell fallback
        WinInstanceToken.cpp # CreateMutexW: the claim that really is exclusive
    )
    target_link_libraries(lt_platform_win PUBLIC Qt6::Multimedia)
    # user32: SetWindowsHookExW, GetSystemMetrics, GetUserObjectInformationW.
    # kernel32: CreateMutexW (the single-instance claim). advapi32: the Run key
    # - QSettings 6.8.3 cannot open it without creating it and cannot be asked
    # whether a value exists, so the probes go straight to Reg* (see
    # WinRegistryStore.h for what Qt 6.8.3 lacks). Qt6::Core happens to pull in
    # advapi32 for us; naming the libraries here is what says *why* they are
    # linked, which is the part a transitive dependency does not document.
    target_link_libraries(lt_platform_win PRIVATE user32 kernel32 advapi32)
endif()
