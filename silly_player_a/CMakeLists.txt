project(silly_player_a)

include_directories(${CMAKE_CURRENT_SOURCE_DIR})
include_directories(${CMAKE_SOURCE_DIR}/silly_player_lib)

set(${PROJECT_NAME}_SOURCES
	player_audio.c)
set(${PROJECT_NAME}_HEADERS
	../silly_player_lib/c99defs.h
	../silly_player_lib/silly_player.h)

source_group("${PROJECT_NAME}\\Source Files" FILES ${${PROJECT_NAME}_SOURCES})
source_group("${PROJECT_NAME}\\Header Files" FILES ${${PROJECT_NAME}_HEADERS})

add_executable(${PROJECT_NAME} ${${PROJECT_NAME}_SOURCES} ${${PROJECT_NAME}_HEADERS})

target_link_libraries(${PROJECT_NAME}
	silly_player)

#copy files
function(install_myself)
	add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
		COMMAND "${CMAKE_COMMAND}" -E copy
			"${CMAKE_SOURCE_DIR}/3rd/bin32/avcodec-57.dll" "${CMAKE_BINARY_DIR}/${PROJECT_NAME}/$<CONFIGURATION>/"
		VERBATIM)
	add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
		COMMAND "${CMAKE_COMMAND}" -E copy
			"${CMAKE_SOURCE_DIR}/3rd/bin32/avdevice-57.dll" "${CMAKE_BINARY_DIR}/${PROJECT_NAME}/$<CONFIGURATION>/"
		VERBATIM)
	add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
		COMMAND "${CMAKE_COMMAND}" -E copy
			"${CMAKE_SOURCE_DIR}/3rd/bin32/avfilter-6.dll" "${CMAKE_BINARY_DIR}/${PROJECT_NAME}/$<CONFIGURATION>/"
		VERBATIM)
	add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
		COMMAND "${CMAKE_COMMAND}" -E copy
			"${CMAKE_SOURCE_DIR}/3rd/bin32/avformat-57.dll" "${CMAKE_BINARY_DIR}/${PROJECT_NAME}/$<CONFIGURATION>/"
		VERBATIM)
	add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
		COMMAND "${CMAKE_COMMAND}" -E copy
			"${CMAKE_SOURCE_DIR}/3rd/bin32/avutil-55.dll" "${CMAKE_BINARY_DIR}/${PROJECT_NAME}/$<CONFIGURATION>/"
		VERBATIM)
	add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
		COMMAND "${CMAKE_COMMAND}" -E copy
			"${CMAKE_SOURCE_DIR}/3rd/bin32/libogg-0.dll" "${CMAKE_BINARY_DIR}/${PROJECT_NAME}/$<CONFIGURATION>/"
		VERBATIM)
	add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
		COMMAND "${CMAKE_COMMAND}" -E copy
			"${CMAKE_SOURCE_DIR}/3rd/bin32/libopus-0.dll" "${CMAKE_BINARY_DIR}/${PROJECT_NAME}/$<CONFIGURATION>/"
		VERBATIM)
	add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
		COMMAND "${CMAKE_COMMAND}" -E copy
			"${CMAKE_SOURCE_DIR}/3rd/bin32/libvorbis-0.dll" "${CMAKE_BINARY_DIR}/${PROJECT_NAME}/$<CONFIGURATION>/"
		VERBATIM)
	add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
		COMMAND "${CMAKE_COMMAND}" -E copy
			"${CMAKE_SOURCE_DIR}/3rd/bin32/libvorbisenc-2.dll" "${CMAKE_BINARY_DIR}/${PROJECT_NAME}/$<CONFIGURATION>/"
		VERBATIM)
	add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
		COMMAND "${CMAKE_COMMAND}" -E copy
			"${CMAKE_SOURCE_DIR}/3rd/bin32/libx264-148.dll" "${CMAKE_BINARY_DIR}/${PROJECT_NAME}/$<CONFIGURATION>/"
		VERBATIM)
	add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
		COMMAND "${CMAKE_COMMAND}" -E copy
			"${CMAKE_SOURCE_DIR}/3rd/bin32/swresample-2.dll" "${CMAKE_BINARY_DIR}/${PROJECT_NAME}/$<CONFIGURATION>/"
		VERBATIM)
	add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
		COMMAND "${CMAKE_COMMAND}" -E copy
			"${CMAKE_SOURCE_DIR}/3rd/bin32/swscale-4.dll" "${CMAKE_BINARY_DIR}/${PROJECT_NAME}/$<CONFIGURATION>/"
		VERBATIM)
	add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
		COMMAND "${CMAKE_COMMAND}" -E copy
			"${CMAKE_SOURCE_DIR}/3rd/bin32/zlib.dll" "${CMAKE_BINARY_DIR}/${PROJECT_NAME}/$<CONFIGURATION>/"
		VERBATIM)

	add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
		COMMAND "${CMAKE_COMMAND}" -E copy
			"${CMAKE_SOURCE_DIR}/3rd/bin32/SDL2.dll" "${CMAKE_BINARY_DIR}/${PROJECT_NAME}/$<CONFIGURATION>/"
		VERBATIM)

	add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
		COMMAND "${CMAKE_COMMAND}" -E copy
			"${CMAKE_BINARY_DIR}/silly_player_lib/$<CONFIGURATION>/silly_player.dll" "${CMAKE_BINARY_DIR}/${PROJECT_NAME}/$<CONFIGURATION>/"
		VERBATIM)
endfunction()

install_myself()