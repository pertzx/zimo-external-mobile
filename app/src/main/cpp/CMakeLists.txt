cmake_minimum_required(VERSION 3.18.1)
project("stormcheats")

# ==== Configurações globais ====
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ==== Encontrar bibliotecas do NDK ====
find_library(log-lib log)
find_library(android-lib android)
find_library(EGL-lib EGL)
find_library(GLESv3-lib GLESv3)

# ==== ImGui ====
set(IMGUI_DIR ${CMAKE_SOURCE_DIR}/External/imgui)
file(GLOB IMGUI_SOURCES 
    ${IMGUI_DIR}/*.cpp
    ${IMGUI_DIR}/backends/imgui_impl_android.cpp
    ${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp
)

# ==== FreeType ====
set(FREETYPE_DIR ${CMAKE_SOURCE_DIR}/External/freetype)
# (adicionar sources do freetype ou usar prebuilt)

# ==== Sources compartilhados ====
set(SHARED_SOURCES
    ${CMAKE_SOURCE_DIR}/Shared/Globals.cpp
    ${CMAKE_SOURCE_DIR}/Shared/Utils/Utils.cpp
    ${CMAKE_SOURCE_DIR}/Shared/Utils/AndroidUtils.cpp
    ${CMAKE_SOURCE_DIR}/Shared/Config/saveconfig.cpp
    ${CMAKE_SOURCE_DIR}/Shared/IPC/IPCProtocol.cpp
)

# ==== Sources do Painel ====
set(PANEL_SOURCES
    ${CMAKE_SOURCE_DIR}/Panel/main.cpp
    ${CMAKE_SOURCE_DIR}/Panel/PanelApp.cpp
    ${CMAKE_SOURCE_DIR}/Panel/AndroidOverlay.cpp
    ${CMAKE_SOURCE_DIR}/Panel/AndroidInput.cpp
    ${CMAKE_SOURCE_DIR}/Panel/AndroidRenderer.cpp
    ${CMAKE_SOURCE_DIR}/Panel/IPCClient.cpp
    ${CMAKE_SOURCE_DIR}/Panel/Interface/Interface.cpp
    ${CMAKE_SOURCE_DIR}/Panel/Interface/Custom.cpp
    ${CMAKE_SOURCE_DIR}/Panel/Fonts/Fonts.cpp
    ${CMAKE_SOURCE_DIR}/Panel/Notify/Notify.cpp
)

# ==== Sources do Daemon ====
set(DAEMON_SOURCES
    ${CMAKE_SOURCE_DIR}/Daemon/daemon_main.cpp
    ${CMAKE_SOURCE_DIR}/Daemon/DaemonApp.cpp
    ${CMAKE_SOURCE_DIR}/Daemon/IPCServer.cpp
    ${CMAKE_SOURCE_DIR}/Daemon/Memory/Memory.cpp
    ${CMAKE_SOURCE_DIR}/Daemon/Draw/Draw.cpp
    ${CMAKE_SOURCE_DIR}/Daemon/Draw/Silent.cpp
    ${CMAKE_SOURCE_DIR}/Daemon/Unity/Unity.cpp
    ${CMAKE_SOURCE_DIR}/Daemon/WebPanel/WebPanel.cpp
    ${CMAKE_SOURCE_DIR}/Daemon/Exploits/Exploits.cpp
)

# ==== Biblioteca do Painel ====
add_library(panel SHARED ${PANEL_SOURCES} ${SHARED_SOURCES} ${IMGUI_SOURCES})
target_include_directories(panel PRIVATE 
    ${CMAKE_SOURCE_DIR}/Shared
    ${CMAKE_SOURCE_DIR}/Panel
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends
    ${FREETYPE_DIR}/include
)
target_link_libraries(panel
    ${log-lib}
    ${android-lib}
    ${EGL-lib}
    ${GLESv3-lib}
    freetype
)

# ==== Biblioteca do Daemon ====
add_library(daemon SHARED ${DAEMON_SOURCES} ${SHARED_SOURCES})
target_include_directories(daemon PRIVATE 
    ${CMAKE_SOURCE_DIR}/Shared
    ${CMAKE_SOURCE_DIR}/Daemon
)
target_link_libraries(daemon
    ${log-lib}
    ${android-lib}
)

# ==== FreeType (prebuilt ou compilado) ====
add_subdirectory(${FREETYPE_DIR} ${CMAKE_BINARY_DIR}/freetype)
