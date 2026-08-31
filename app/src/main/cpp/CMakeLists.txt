cmake_minimum_required(VERSION 3.18.1)
project("stormcheats")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_library(log-lib log)
find_library(android-lib android)
find_library(EGL-lib EGL)
find_library(GLESv3-lib GLESv3)

set(CPP_DIR ${CMAKE_SOURCE_DIR})
set(EXTERNAL_DIR ${CMAKE_SOURCE_DIR}/../../External)
set(IMGUI_DIR ${EXTERNAL_DIR}/ImGui)
set(FREETYPE_INCLUDE_DIR ${EXTERNAL_DIR}/FreeType/Include)

# ==== ImGui ====
set(IMGUI_SOURCES
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/imgui_stdlib.cpp
    ${IMGUI_DIR}/imgui_freetype.cpp
    ${IMGUI_DIR}/imgui_impl_opengl3.cpp
    ${IMGUI_DIR}/imgui_impl_android.cpp
    ${IMGUI_DIR}/Custom.cpp
)

# ==== Sources compartilhados ====
set(SHARED_SOURCES
    ${CPP_DIR}/Shared/Globals.hpp
    ${CPP_DIR}/Shared/Utils/Utils.cpp
    ${CPP_DIR}/Shared/Utils/Utils.hpp
    ${CPP_DIR}/Shared/Config/saveconfig.cpp
    ${CPP_DIR}/Shared/IPC/IPCProtocol.hpp
    ${CPP_DIR}/Shared/Math/Quaternion/Quaternion.cpp
    ${CPP_DIR}/Shared/Math/Quaternion/Quaternion.hpp
    ${CPP_DIR}/Shared/Math/Vectors/Vector2.cpp
    ${CPP_DIR}/Shared/Math/Vectors/Vector2.hpp
    ${CPP_DIR}/Shared/Math/Vectors/Vector3.cpp
    ${CPP_DIR}/Shared/Math/Vectors/Vector3.hpp
    ${CPP_DIR}/Shared/Math/Vectors/Vector4.cpp
    ${CPP_DIR}/Shared/Math/Vectors/Vector4.hpp
    ${CPP_DIR}/Shared/Offsets/Offsets.cpp
    ${CPP_DIR}/Shared/Offsets/Offsets.hpp
)

# ==== Sources do Painel ====
set(PANEL_SOURCES
    ${CPP_DIR}/Panel/main.cpp
    ${CPP_DIR}/Panel/PanelApp.cpp
    ${CPP_DIR}/Panel/PanelApp.hpp
    ${CPP_DIR}/Panel/AndroidOverlay.cpp
    ${CPP_DIR}/Panel/AndroidOverlay.hpp
    ${CPP_DIR}/Panel/AndroidInput.cpp
    ${CPP_DIR}/Panel/AndroidInput.hpp
    ${CPP_DIR}/Panel/IPC/IPCClient.cpp
    ${CPP_DIR}/Panel/IPC/IPCClient.hpp
    ${CPP_DIR}/Panel/Interface/Interface.cpp
    ${CPP_DIR}/Panel/Interface/Interface.hpp
    ${CPP_DIR}/Panel/Interface/unpacker.h
    ${CPP_DIR}/Panel/Fonts/Fonts.cpp
    ${CPP_DIR}/Panel/Fonts/Fonts.hpp
)

# ==== Sources do Daemon ====
set(DAEMON_SOURCES
    ${CPP_DIR}/Daemon/daemon_main.cpp
    ${CPP_DIR}/Daemon/DaemonApp.cpp
    ${CPP_DIR}/Daemon/DaemonApp.hpp
    ${CPP_DIR}/Daemon/IPC/IPCServer.cpp
    ${CPP_DIR}/Daemon/IPC/IPCServer.hpp
    ${CPP_DIR}/Daemon/Memory/Memory.cpp
    ${CPP_DIR}/Daemon/Memory/Memory.hpp
    ${CPP_DIR}/Daemon/Memory/MemoryExternal.h
    ${CPP_DIR}/Daemon/Memory/EmulatorEnv.cpp
    ${CPP_DIR}/Daemon/Memory/EmulatorEnv.hpp
    ${CPP_DIR}/Daemon/Draw/Draw.cpp
    ${CPP_DIR}/Daemon/Draw/Draw.hpp
    ${CPP_DIR}/Daemon/Draw/Silent.cpp
    ${CPP_DIR}/Daemon/Draw/Silent.hpp
    ${CPP_DIR}/Daemon/Draw/Skeleton.cpp
    ${CPP_DIR}/Daemon/Draw/Skeleton.hpp
    ${CPP_DIR}/Daemon/Draw/Weapon/NameGun.cpp
    ${CPP_DIR}/Daemon/Draw/Weapon/NameGun.h
    ${CPP_DIR}/Daemon/Offsets/Offsets.cpp
    ${CPP_DIR}/Daemon/Offsets/Offsets.hpp
    ${CPP_DIR}/Daemon/Unity/Unity.cpp
    ${CPP_DIR}/Daemon/Unity/Unity.hpp
    ${CPP_DIR}/Daemon/Unity/UTF/UTF8.cpp
    ${CPP_DIR}/Daemon/Unity/UTF/UTF8.hpp
    ${CPP_DIR}/Daemon/WebPanel/WebPanel.cpp
    ${CPP_DIR}/Daemon/WebPanel/WebPanel.hpp
    ${CPP_DIR}/External/Notify/Notify.cpp
    ${CPP_DIR}/External/Notify/Notify.hpp
)

# ==== Biblioteca do Painel ====
add_library(panel SHARED ${PANEL_SOURCES} ${SHARED_SOURCES} ${IMGUI_SOURCES})
target_include_directories(panel PRIVATE
    ${CPP_DIR}/Shared
    ${CPP_DIR}/Panel
    ${IMGUI_DIR}
    ${FREETYPE_INCLUDE_DIR}
    ${CPP_DIR}/External
)
target_link_libraries(panel
    ${log-lib}
    ${android-lib}
    ${EGL-lib}
    ${GLESv3-lib}
)

# ==== Biblioteca do Daemon ====
add_library(daemon SHARED ${DAEMON_SOURCES} ${SHARED_SOURCES})
target_include_directories(daemon PRIVATE
    ${CPP_DIR}/Shared
    ${CPP_DIR}/Daemon
    ${CPP_DIR}/External
)
target_link_libraries(daemon
    ${log-lib}
    ${android-lib}
)