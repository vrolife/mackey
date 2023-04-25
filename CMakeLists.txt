cmake_minimum_required(VERSION 3.16)
project(gtkmac)

add_library(frida-gum STATIC IMPORTED)
target_include_directories(frida-gum INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/${CMAKE_SYSTEM_PROCESSOR})
set_target_properties(frida-gum PROPERTIES IMPORTED_LOCATION ${CMAKE_CURRENT_SOURCE_DIR}/${CMAKE_SYSTEM_PROCESSOR}/libfrida-gum.a)

find_package(PkgConfig)
pkg_check_modules(XCB IMPORTED_TARGET xcb-keysyms)

add_library(gtkmac MODULE gtkmac.cpp)
target_link_libraries(gtkmac PRIVATE frida-gum PkgConfig::XCB)
