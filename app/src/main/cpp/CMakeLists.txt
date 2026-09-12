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
# CLIENT (libclient.so - processo do app)
#
# O client agora tem TODO o jogo:
#   Memory (via ponte), Offsets, Unity, Draw/ESP/aimbot,
#   Silent, Skeleton, painel ImGui.
#
# Os READ/WRITE saem pela ponte (Client/Memory/BridgeClient.cpp)
# para o daemon root em /data/local/tmp/stormdaemon.
# ============================================================

file(GLOB_RECURSE CLIENT_SOURCES CONFIGURE_DEPENDS
    ${CPP_DIR}/Client/*.cpp
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
    ${CPP_DIR}/Shared/Bridge

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
# DAEMON (executável "stormdaemon")
#
# SOMENTE A PONTE de read/write:
#   - Daemon/daemon_main.cpp  (servidor do socket + ops de memória)
#   - Shared/Bridge/BridgeProtocol.hpp (protocolo compartilhado)
#
# NÃO compila Memory.cpp/Offsets/Unity/Draw: nenhuma lógica de jogo
# fica no daemon. Ele roda como root em /data/local/tmp/stormdaemon,
# instalado pelo DaemonService (Java).
#
# Observação: o Gradle usa ANDROID_STL=c++_shared, então o daemon
# precisa de libc++_shared.so ao lado dele; o DaemonService copia a
# lib do APK para /data/local/tmp e exporta LD_LIBRARY_PATH.
# ============================================================

add_executable(daemon
    ${CPP_DIR}/Daemon/daemon_main.cpp
)

target_include_directories(daemon PRIVATE

    ${CPP_DIR}
    ${CPP_DIR}/Shared
    ${CPP_DIR}/Shared/Bridge
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
)

# ============================================================
# DAEMON OUTPUT
#
# O Gradle/CMake gera:
#
#   assets/bin/arm64-v8a/stormdaemon
#   assets/bin/armeabi-v7a/stormdaemon
#
# O DaemonService (Java) instala em:
#
#   /data/local/tmp/stormdaemon   (chmod 755, via su)
#
# e inicia com:  su -c "exec /data/local/tmp/stormdaemon"
#
# Socket da ponte: /data/local/tmp/stormbridge.sock
# ============================================================

set(DAEMON_ASSET_DIR
    ${CMAKE_SOURCE_DIR}/../assets/bin/${ANDROID_ABI}
)

file(MAKE_DIRECTORY
    ${DAEMON_ASSET_DIR}
)

set_target_properties(daemon PROPERTIES

    OUTPUT_NAME "stormdaemon"

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
message(STATUS "CLIENT      = libclient.so (jogo completo + ponte)")
message(STATUS "DAEMON      = stormdaemon (so ponte read/write)")
message(STATUS "DAEMON_DIR  = ${DAEMON_ASSET_DIR}")
message(STATUS "========================================")
