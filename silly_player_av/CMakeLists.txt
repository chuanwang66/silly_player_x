project(silly_player_av)

include_directories(${CMAKE_CURRENT_SOURCE_DIR})
include_directories(${CMAKE_SOURCE_DIR}/silly_player_lib)

#SDL2
find_package(SDL2 REQUIRED)
include_directories(${SDL2_INCLUDE_DIRS})

set(silly_player_av_SOURCES
	player_av.c)
set(silly_player_av_HEADERS
	../silly_player_lib/c99defs.h
	../silly_player_lib/silly_player.h)

source_group("silly_player_av\\Source Files" FILES ${silly_player_av_SOURCES})
source_group("silly_player_av\\Header Files" FILES ${silly_player_av_HEADERS})

add_executable(silly_player_av ${silly_player_av_SOURCES} ${silly_player_av_HEADERS})

target_link_libraries(silly_player_av
	silly_player
	${SDL2_LIBRARIES})