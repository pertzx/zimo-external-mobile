cmake_minimum_required(VERSION 3.10)
project(ZmInternalAndroid)

# Set minimum Android API level
set(ANDROID_PLATFORM android-21)
set(ANDROID_STL c++_shared)

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Include directories
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/shared
    ${CMAKE_CURRENT_SOURCE_DIR}/daemon/src
    ${CMAKE_CURRENT_SOURCE_DIR}/client/src
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/json/include
)

# Daemon library
add_library(zmdaemon SHARED
    daemon/src/main.cpp
    daemon/src/memory/memory.cpp
    daemon/src/memory/offsets.cpp
    daemon/src/memory/il2cpp.cpp
    daemon/src/memory/resolver.cpp
    daemon/src/game/readloop.cpp
    daemon/src/ipc/server.cpp
    daemon/src/daemon.cpp
)

# Client library
add_library(zmclient SHARED
    client/src/main.cpp
    client/src/render/renderer.cpp
    client/src/render/overlay.cpp
    client/src/render/fonts.cpp
    client/src/ui/interface.cpp
    client/src/ui/input.cpp
    client/src/ipc/client_ipc.cpp
    client/src/logic/esp.cpp
    client/src/logic/aimbot.cpp
    client/src/logic/silent.cpp
    client/src/logic/exploits.cpp
)

# Link libraries
find_library(log-lib log)
find_library(android-lib android)
find_library(egl-lib EGL)
find_library(gles-lib GLESv2)
find_library(dl-lib dl)

target_link_libraries(zmdaemon
    ${log-lib}
    ${android-lib}
    ${dl-lib}
)

target_link_libraries(zmclient
    ${log-lib}
    ${android-lib}
    ${egl-lib}
    ${gles-lib}
    ${dl-lib}
)

# Copy third-party libraries
add_custom_command(
    TARGET zmdaemon POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui
    $<TARGET_FILE_DIR:zmdaemon>
)

add_custom_command(
    TARGET zmclient POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui
    $<TARGET_FILE_DIR:zmclient>
)