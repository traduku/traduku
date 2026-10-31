# Platform-agnostic composition root (linked into every DE shell).
add_library(lt_app STATIC AppController.cpp)
add_library(lt::app ALIAS lt_app)
target_include_directories(lt_app PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(lt_app PUBLIC lt::core lt::ui lt::interfaces Qt6::Widgets)
