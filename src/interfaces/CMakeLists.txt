# interfaces: header-only abstract platform services (Qt Core at most).
add_library(lt_interfaces INTERFACE)
add_library(lt::interfaces ALIAS lt_interfaces)
target_include_directories(lt_interfaces INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(lt_interfaces INTERFACE Qt6::Core)
