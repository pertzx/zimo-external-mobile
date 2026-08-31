#pragma once
// ========== DEFINIR ANTES DE INCLUIR imgui.h ==========
// #define IMGUI_DEFINE_MATH_OPERATORS
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