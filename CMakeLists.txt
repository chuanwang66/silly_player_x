cmake_minimum_required(VERSION 2.8.12)

project(silly_player_x)

set(DepsPath "${CMAKE_SOURCE_DIR}/3rd/")

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/cmake/Modules/")

add_subdirectory(deps)
add_subdirectory(silly_player_lib)
add_subdirectory(silly_player_a)
#add_subdirectory(silly_player_av)