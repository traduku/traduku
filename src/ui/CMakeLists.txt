# DE-agnostic Qt Widgets UI (works on KDE, GNOME, bare X11, Windows...).
add_library(lt_ui STATIC
    TranslatorWindow.cpp
    SettingsDialog.cpp
)
add_library(lt::ui ALIAS lt_ui)
target_include_directories(lt_ui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(lt_ui PUBLIC lt::core Qt6::Widgets Qt6::Network)
