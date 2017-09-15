# Once done these will be defined:
#
#  SDL2_FOUND
#  SDL2_INCLUDE_DIRS
#  SDL2_LIBRARIES

if(CMAKE_SIZEOF_VOID_P EQUAL 8)
	set(_lib_suffix 64)
else()
	set(_lib_suffix 32)
endif()

find_path(SDL2_INCLUDE_DIR
	NAMES SDL.h
	HINTS
		ENV sdl2Path${_lib_suffix}
		ENV sdl2Path
		ENV DepsPath${_lib_suffix}
		ENV DepsPath
		${sdl2Path${_lib_suffix}}
		${sdl2Path}
		${DepsPath${_lib_suffix}}
		${DepsPath}
		${_SDL2_INCLUDE_DIRS}
	PATHS
		/usr/include /usr/local/include /opt/local/include /sw/include
	PATH_SUFFIXES
		include/SDL2)

#find SDL2.lib
find_library(SDL2_LIB
	NAMES ${_SDL2_LIBRARIES} SDL2
	HINTS
		ENV sdl2Path${_lib_suffix}
		ENV sdl2Path
		ENV DepsPath${_lib_suffix}
		ENV DepsPath
		${sdl2Path${_lib_suffix}}
		${sdl2Path}
		${DepsPath${_lib_suffix}}
		${DepsPath}
		${_SDL2_LIBRARY_DIRS}
	PATHS
		/usr/lib /usr/local/lib /opt/local/lib /sw/lib
	PATH_SUFFIXES
		lib${_lib_suffix} lib
		libs${_lib_suffix} libs
		bin${_lib_suffix} bin
		../lib${_lib_suffix} ../lib
		../libs${_lib_suffix} ../libs
		../bin${_lib_suffix} ../bin)

#find SDL2main.lib
find_library(SDL2main_LIB
	NAMES ${_SDL2_LIBRARIES} SDL2main
	HINTS
		ENV sdl2Path${_lib_suffix}
		ENV sdl2Path
		ENV DepsPath${_lib_suffix}
		ENV DepsPath
		${sdl2Path${_lib_suffix}}
		${sdl2Path}
		${DepsPath${_lib_suffix}}
		${DepsPath}
		${_SDL2_LIBRARY_DIRS}
	PATHS
		/usr/lib /usr/local/lib /opt/local/lib /sw/lib
	PATH_SUFFIXES
		lib${_lib_suffix} lib
		libs${_lib_suffix} libs
		bin${_lib_suffix} bin
		../lib${_lib_suffix} ../lib
		../libs${_lib_suffix} ../libs
		../bin${_lib_suffix} ../bin)

set(SDL2_LIB
	${SDL2_LIB}
	${SDL2main_LIB})

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(SDL2 DEFAULT_MSG SDL2_LIB SDL2_INCLUDE_DIR)
mark_as_advanced(SDL2_INCLUDE_DIR SDL2_LIB)

if(SDL2_FOUND)
	set(SDL2_INCLUDE_DIRS ${SDL2_INCLUDE_DIR})
	set(SDL2_LIBRARIES ${SDL2_LIB})
endif()
