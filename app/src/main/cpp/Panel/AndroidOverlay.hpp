#pragma once
#include <imgui.h>
#include <android/native_window.h>

namespace Overlay {
    bool Setup(ANativeWindow* window);
    bool Initialize();
    void ShutDown();
    void glRefresh();
    void glClearTransparent();
    ImVec2 GetTargetWindowSize();
    bool IsInitialized();
}