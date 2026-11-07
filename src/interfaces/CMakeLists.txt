# Abstract platform services. Q_OBJECT interfaces force a (header-only)
# STATIC target so AUTOMOC runs over them.
add_library(lt_interfaces STATIC
    iclipboard.h
    ihotkeybackend.h
    itray.h
    inotifier.h
    iautostart.h
    iglobalshortcut.h
)
add_library(lt::interfaces ALIAS lt_interfaces)
target_include_directories(lt_interfaces PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(lt_interfaces PUBLIC Qt6::Core)
