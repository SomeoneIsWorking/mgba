set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Set a modern deployment target for Qt 6 compatibility
if(APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "10.15")
endif()

# Find Qt components
option(FORCE_QT_VERSION "Force the used Qt version" OFF)

if(FORCE_QT_VERSION EQUAL 5 OR FORCE_QT_VERSION EQUAL 6)
	set(QT_VERSIONS ${FORCE_QT_VERSION})
else()
	set(QT_VERSIONS 6 5)
endif()

foreach(V ${QT_VERSIONS})
	set(QT Qt${V})
	set(QT_V ${V})
    find_package(${QT} COMPONENTS Core Network Gui Widgets OPTIONAL_COMPONENTS Multimedia)
    # For Qt6, try to find OpenGL components which provide QOpenGLShaderProgram
    if(QT_V GREATER_EQUAL 6)
        find_package(${QT} COMPONENTS OpenGL OpenGLWidgets OPTIONAL_COMPONENTS)
    endif()
    if(${${QT}Core_FOUND})
		break()
    endif()
endforeach()

if(NOT ${QT}Core_FOUND)
	message(WARNING "Cannot find required Qt modules (Core, WebSockets)")
	return()
endif()

set(CMAKE_AUTOMOC ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)

# Include paths for Qt platform
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../qt)
include_directories(${CMAKE_CURRENT_BINARY_DIR}/../qt)  # For generated UI files

# Source files - include full Qt platform for CoreController functionality
set(WEB_SERVER_SOURCES
    main.cpp
    WebMultiplayerServer.cpp
    ../qt/CoreController.cpp
    ../qt/MultiplayerController.cpp
    ../qt/ConfigController.cpp
    ../qt/LogController.cpp
    ../qt/Log.cpp
    ../qt/utils.cpp
    ../qt/VFileDevice.cpp
    ../qt/InputController.cpp
    ../qt/input/InputMapper.cpp
    ../qt/input/InputSource.cpp
    ../qt/input/InputDriver.cpp
    ../qt/ShortcutController.cpp
    ../qt/ActionMapper.cpp
    ../qt/Action.cpp
    ../qt/MemoryAccessLogController.cpp

    # Minimal extra platform sources required by InputController
    ../qt/InputProfile.cpp
    ../qt/input/Gamepad.cpp
    ../qt/input/GamepadAxisEvent.cpp
    ../qt/input/GamepadButtonEvent.cpp
    ../qt/input/GamepadHatEvent.cpp
    ../qt/CoreManager.cpp
    WebBroadcaster.cpp
    VideoSink.cpp
    ClientSocketWorker.cpp
    NativeTcpServer.cpp
    ServerSessionManager.cpp
    ClientSessionHandler.cpp
	LocalClientDetector.cpp
    WebServerGui.cpp
)

# Create executable
add_executable(${BINARY_NAME}-web-server ${WEB_SERVER_SOURCES})

# Qt libraries
set(QT_LIBRARIES ${QT}::Core ${QT}::Network ${QT}::Gui ${QT}::Widgets ${QT}::Multimedia)

# Link libraries
target_link_libraries(${BINARY_NAME}-web-server 
    ${BINARY_NAME}  # mGBA core library
    ${QT_LIBRARIES}
)

# Compiler definitions
target_compile_definitions(${BINARY_NAME}-web-server PRIVATE
    ${FEATURE_DEFINES}
    ${FUNCTION_DEFINES}
    ${OS_DEFINES}
)

# Install target
install(TARGETS ${BINARY_NAME}-web-server
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} 
    COMPONENT ${BINARY_NAME}-web-server
)