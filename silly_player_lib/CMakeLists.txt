project(silly_player_lib)

add_definitions(-DSILLY_PLAYER_EXPORTS)

if(MSVC)
	set(CMAKE_C_FLAGS_DEBUG "/DDEBUG=1 /D_DEBUG=1 ${CMAKE_C_FLAGS_DEBUG}")
	set(CMAKE_CXX_FLAGS_DEBUG "/DDEBUG=1 /D_DEBUG=1 ${CMAKE_C_FLAGS_DEBUG}")

	if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
		set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} /SAFESEH:NO")
		set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} /SAFESEH:NO")
		set(CMAKE_MODULE_LINKER_FLAGS "${CMAKE_MODULE_LINKER_FLAGS} /SAFESEH:NO")
	endif()
else()
	if(MINGW)
		set(CMAKE_WIDL "widl" CACHE STRING "wine IDL header file generation program")
		set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -D_WIN32_WINNT=0x0600 -DWINVER=0x0600")
		set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -D_WIN32_WINNT=0x0600 -DWINVER=0x0600")
	endif()
	set(CMAKE_C_FLAGS_DEBUG "-DDEBUG=1 -D_DEBUG=1 ${CMAKE_C_FLAGS_DEBUG}")
	set(CMAKE_CXX_FLAGS_DEBUG "-DDEBUG=1 -D_DEBUG=1 ${CMAKE_C_FLAGS_DEBUG}")
endif()

include_directories(${CMAKE_CURRENT_SOURCE_DIR})

#ffmpeg
find_package(FFmpeg COMPONENTS avcodec avformat avutil swscale swresample REQUIRED)
include_directories(${FFMPEG_INCLUDE_DIRS})

#SDL2
find_package(SDL2 REQUIRED)
include_directories(${SDL2_INCLUDE_DIRS})

set(silly_player_lib_SOURCES
	global.c
	packet_queue.c
	parse.c
	audio.c
	video.c
	silly_player.c)
set(silly_player_lib_HEADERS
	c99defs.h
	packet_queue.h
	parse.h
	audio.h
	video.h
	silly_player_internal.h
	silly_player.h)

source_group("silly_player_lib\\Source Files" FILES ${silly_player_lib_SOURCES})
source_group("silly_player_lib\\Header Files" FILES ${silly_player_lib_HEADERS})

add_library(silly_player SHARED ${silly_player_lib_SOURCES} ${silly_player_lib_HEADERS})

target_link_libraries(silly_player
	${FFMPEG_LIBRARIES}
	${SDL2_LIBRARIES})