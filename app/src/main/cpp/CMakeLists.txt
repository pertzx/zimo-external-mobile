cmake_minimum_required(VERSION 3.18.1)

project("stormcheats")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

find_library(log-lib log)
find_library(android-lib android)
find_library(EGL-lib EGL)
find_library(GLESv3-lib GLESv3)

set(CPP_DIR ${CMAKE_SOURCE_DIR})
set(EXTERNAL_DIR ${CMAKE_SOURCE_DIR}/../External)

set(IMGUI_DIR ${EXTERNAL_DIR}/ImGui)
set(FREETYPE_INCLUDE_DIR ${EXTERNAL_DIR}/FreeType/Include)

# ============================================================
# IMGUI
# ============================================================

set(IMGUI_SOURCES
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/imgui_stdlib.cpp
    ${IMGUI_DIR}/imgui_impl_opengl3.cpp
    ${IMGUI_DIR}/imgui_impl_android.cpp
    ${IMGUI_DIR}/Custom.cpp
    ${IMGUI_DIR}/stb_image.cpp
)

# ============================================================
# CLIENT
# ============================================================

file(GLOB_RECURSE CLIENT_SOURCES CONFIGURE_DEPENDS
    ${CPP_DIR}/Client/*.cpp
)

# O Client não deve compilar o backend do daemon.
# O daemon possui sua própria cópia das rotinas backend.
list(FILTER CLIENT_SOURCES EXCLUDE REGEX
    ".*/Client/Memory/.*\\.cpp$"
)

list(FILTER CLIENT_SOURCES EXCLUDE REGEX
    ".*/Client/Offsets/.*\\.cpp$"
)

list(FILTER CLIENT_SOURCES EXCLUDE REGEX
    ".*/Client/Unity/.*\\.cpp$"
)

list(FILTER CLIENT_SOURCES EXCLUDE REGEX
    ".*/Client/Draw/.*\\.cpp$"
)

add_library(client SHARED
    ${CLIENT_SOURCES}

    ${IMGUI_SOURCES}

    ${EXTERNAL_DIR}/Notify/Notify.cpp
)

target_include_directories(client PRIVATE

    ${CPP_DIR}/Client
    ${CPP_DIR}/Client/Config
    ${CPP_DIR}/Client/Draw
    ${CPP_DIR}/Client/Fonts
    ${CPP_DIR}/Client/IPC
    ${CPP_DIR}/Client/Interface
    ${CPP_DIR}/Client/Math
    ${CPP_DIR}/Client/Memory
    ${CPP_DIR}/Client/Offsets
    ${CPP_DIR}/Client/Unity
    ${CPP_DIR}/Client/Utils

    ${CPP_DIR}/Shared

    ${CPP_DIR}

    ${IMGUI_DIR}
    ${FREETYPE_INCLUDE_DIR}
    ${EXTERNAL_DIR}
)

target_compile_definitions(client PRIVATE
    IMGUI_DEFINE_MATH_OPERATORS
)

target_compile_options(client PRIVATE
    -fdata-sections
    -ffunction-sections
)

target_link_options(client PRIVATE
    -Wl,--gc-sections
)

target_link_libraries(client
    ${log-lib}
    ${android-lib}
    ${EGL-lib}
    ${GLESv3-lib}
)

# ============================================================
# DAEMON BACKEND
#
# IMPORTANTE:
# Aqui NÃO usamos add_library(SHARED).
#
# O resultado é um ELF executável chamado "daemon".
# ============================================================

set(DAEMON_SOURCES

    # --------------------------------------------------------
    # Entry point
    # --------------------------------------------------------

    ${CPP_DIR}/Daemon/daemon_main.cpp

    # --------------------------------------------------------
    # Backend
    # --------------------------------------------------------

    ${CPP_DIR}/Daemon/DaemonApp.cpp
    ${CPP_DIR}/Daemon/IPC/IPCServer.cpp

    # --------------------------------------------------------
    # Memory
    #
    # Essas fontes foram movidas para Client durante a
    # reorganização, mas continuam pertencendo ao backend.
    # --------------------------------------------------------

    ${CPP_DIR}/Client/Memory/Memory.cpp
    ${CPP_DIR}/Client/Memory/MemoryExternal.cpp
    ${CPP_DIR}/Client/Memory/EmulatorEnv.cpp

    # --------------------------------------------------------
    # Offsets
    # --------------------------------------------------------

    ${CPP_DIR}/Client/Offsets/Offsets.cpp

    # --------------------------------------------------------
    # Unity / UTF
    # --------------------------------------------------------

    ${CPP_DIR}/Client/Unity/Unity.cpp
    ${CPP_DIR}/Client/Unity/UTF/UTF8.cpp

    # --------------------------------------------------------
    # Data / leitura / entidades
    #
    # O Data.cpp deve ser colocado aqui caso esteja dentro
    # de Client/Draw depois da reorganização.
    # --------------------------------------------------------

    ${CPP_DIR}/Client/Draw/Draw.cpp
    ${CPP_DIR}/Client/Draw/Silent.cpp
    ${CPP_DIR}/Client/Draw/Skeleton.cpp
)

# ============================================================
# DAEMON EXECUTABLE
# ============================================================

add_executable(daemon
    ${DAEMON_SOURCES}
)

target_include_directories(daemon PRIVATE

    # --------------------------------------------------------
    # cpp root
    # --------------------------------------------------------

    ${CPP_DIR}

    # --------------------------------------------------------
    # Shared
    # --------------------------------------------------------

    ${CPP_DIR}/Shared

    # --------------------------------------------------------
    # Client headers usados pelo backend
    # --------------------------------------------------------

    ${CPP_DIR}/Client
    ${CPP_DIR}/Client/Draw
    ${CPP_DIR}/Client/Memory
    ${CPP_DIR}/Client/Offsets
    ${CPP_DIR}/Client/Unity
    ${CPP_DIR}/Client/Unity/UTF
    ${CPP_DIR}/Client/Utils
    ${CPP_DIR}/Client/Math

    # --------------------------------------------------------
    # Daemon
    # --------------------------------------------------------

    ${CPP_DIR}/Daemon
    ${CPP_DIR}/Daemon/IPC

    # --------------------------------------------------------
    # External
    # --------------------------------------------------------

    ${IMGUI_DIR}
    ${FREETYPE_INCLUDE_DIR}
    ${EXTERNAL_DIR}
)

target_compile_definitions(daemon PRIVATE
    DAEMON_BUILD
    IMGUI_DEFINE_MATH_OPERATORS
)

target_compile_options(daemon PRIVATE
    -fdata-sections
    -ffunction-sections
)

target_link_options(daemon PRIVATE
    -Wl,--gc-sections
)

target_link_libraries(daemon

    ${log-lib}
    ${android-lib}

    ${EGL-lib}
    ${GLESv3-lib}
)

# ============================================================
# DAEMON OUTPUT
#
# O Gradle/CMake gera:
#
# assets/bin/arm64-v8a/daemon
# assets/bin/armeabi-v7a/daemon
#
# O DaemonService depois copia esse arquivo para:
#
# /data/local/tmp/daemon
# ============================================================

set(DAEMON_ASSET_DIR
    ${CMAKE_SOURCE_DIR}/../assets/bin/${ANDROID_ABI}
)

file(MAKE_DIRECTORY
    ${DAEMON_ASSET_DIR}
)

set_target_properties(daemon PROPERTIES

    OUTPUT_NAME "daemon"

    RUNTIME_OUTPUT_DIRECTORY
        ${DAEMON_ASSET_DIR}

    RUNTIME_OUTPUT_DIRECTORY_DEBUG
        ${DAEMON_ASSET_DIR}

    RUNTIME_OUTPUT_DIRECTORY_RELEASE
        ${DAEMON_ASSET_DIR}
)

# ============================================================
# ARCHITECTURE LOG
# ============================================================

message(STATUS "========================================")
message(STATUS "Storm Cheats native build")
message(STATUS "ANDROID_ABI = ${ANDROID_ABI}")
message(STATUS "CLIENT      = libclient.so")
message(STATUS "DAEMON      = executable")
message(STATUS "DAEMON_DIR  = ${DAEMON_ASSET_DIR}")
message(STATUS "========================================")