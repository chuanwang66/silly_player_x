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

#zlib
find_package(ZLIB REQUIRED)
include_directories(SYSTEM ${ZLIB_INCLUDE_DIR})

if(WIN32)
	set(silly_player_lib_PLATFORM_SOURCES
		util/threading-windows.c
		util/pipe-windows.c
		util/platform-windows.c)
	set(silly_player_lib_PLATFORM_HEADERS
		util/threading-windows.h
		util/windows/win-version.h
		util/windows/ComPtr.hpp
		util/windows/CoTaskMemPtr.hpp
		util/windows/HRError.hpp
		util/windows/WinHandle.hpp)
	set(silly_player_lib_PLATFORM_DEPS winmm)
	if(MSVC)
		set(silly_player_lib_PLATFORM_DEPS
			${silly_player_lib_PLATFORM_DEPS}
			w32-pthreads)
	endif()
elseif(APPLE)
elseif(UNIX)
endif()

set(silly_player_util_SOURCES
	util/array-serializer.c
	util/file-serializer.c
	util/base.c
	util/platform.c
	util/cf-lexer.c
	util/bmem.c
	util/config-file.c
	util/lexer.c
	util/dstr.c
	util/utf8.c
	util/crc32.c
	util/text-lookup.c
	util/cf-parser.c
	util/profiler.c)
set(silly_player_util_HEADERS
	util/array-serializer.h
	util/file-serializer.h
	util/utf8.h
	util/crc32.h
	util/base.h
	util/text-lookup.h
	util/vc/vc_inttypes.h
	util/vc/vc_stdbool.h
	util/vc/vc_stdint.h
	util/bmem.h
	util/c99defs.h
	util/util_uint128.h
	util/cf-parser.h
	util/threading.h
	util/pipe.h
	util/cf-lexer.h
	util/darray.h
	util/circlebuf.h
	util/dstr.h
	util/serializer.h
	util/config-file.h
	util/lexer.h
	util/platform.h
	util/profiler.h
	util/profiler.hpp)

set(silly_player_lib_SOURCES
	${silly_player_lib_PLATFORM_SOURCES}
	${silly_player_util_SOURCES}
	global.c
	packet_queue.c
	parse.c
	audio.c
	video.c
	silly_player.c)
set(silly_player_lib_HEADERS
	${silly_player_lib_PLATFORM_HEADERS}
	${silly_player_util_HEADERS}
	c99defs.h
	packet_queue.h
	parse.h
	audio.h
	video.h
	silly_player_internal.h
	silly_player.h)

source_group("silly_player_lib\\Source Files" FILES ${silly_player_lib_SOURCES})
source_group("silly_player_lib\\Header Files" FILES ${silly_player_lib_HEADERS})
source_group("util\\Source Files" FILES ${silly_player_util_SOURCES})
source_group("util\\Header Files" FILES ${silly_player_util_HEADERS})

add_library(silly_player SHARED ${silly_player_lib_SOURCES} ${silly_player_lib_HEADERS})

target_link_libraries(silly_player
	${silly_player_lib_PLATFORM_DEPS}
	${FFMPEG_LIBRARIES}
	${SDL2_LIBRARIES}
	${ZLIB_LIBRARIES})