cmake_minimum_required(VERSION 3.10)
project(mgba-client-sdl)

find_package(SDL2 REQUIRED)
include_directories(${SDL2_INCLUDE_DIRS})

# Set a modern deployment target for Qt 6 compatibility
if(APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "10.15")
endif()

# Try to locate FFmpeg headers/libraries via pkg-config for RTP receiving
find_package(PkgConfig)
if(PkgConfig_FOUND)
	pkg_check_modules(FFMPEG libavformat libavcodec libswscale libswresample libavutil)
	if(FFMPEG_FOUND)
		include_directories(${FFMPEG_INCLUDE_DIRS})
		link_directories(${FFMPEG_LIBRARY_DIRS})
		set(ADDITIONAL_LIBS ${FFMPEG_LIBRARIES})
	else()
		set(ADDITIONAL_LIBS )
	endif()
else()
	set(ADDITIONAL_LIBS )
endif()

add_executable(mgba-client-sdl 
	main.cpp 
	ControlClient.cpp 
	UdpDiscovery.cpp 
	VideoReceiver.cpp 
	Gui.cpp 
	InputHandler.cpp 
)
target_link_libraries(mgba-client-sdl ${SDL2_LIBRARIES} ${ADDITIONAL_LIBS})
set_target_properties(mgba-client-sdl PROPERTIES OUTPUT_NAME mgba-client-sdl)
install(TARGETS mgba-client-sdl DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT mgba-client-sdl)
