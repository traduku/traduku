# lt_core: DE-agnostic heart of the app. Pure Qt (Core/Network/Multimedia).
# Anything KDE/Wayland/X11-specific lives in src/interfaces + src/platform.
add_library(lt_core STATIC
    languages.cpp
    settings.cpp
)
add_library(lt::core ALIAS lt_core)
target_include_directories(lt_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(lt_core PUBLIC Qt6::Core Qt6::Network)
