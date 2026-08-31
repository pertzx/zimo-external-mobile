#pragma once
// ========== DEFINIR ANTES DE INCLUIR imgui.h ==========
// ❌ Antes
// #define IMGUI_DEFINE_MATH_OPERATORS

// ✅ Depois
#ifndef IMGUI_DEFINE_MATH_OPERATORS
#define IMGUI_DEFINE_MATH_OPERATORS
#endif

#include <imgui.h>

namespace Overlay {
    inline ImVec2 GetTargetWindowSize() {
        ImGuiIO& io = ImGui::GetIO();
        return io.DisplaySize;
    }
}