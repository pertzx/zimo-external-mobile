#pragma once
// ========== DEFINIR ANTES DE INCLUIR imgui.h ==========
#define IMGUI_DEFINE_MATH_OPERATORS
#include <imgui.h>

namespace Overlay {
    inline ImVec2 GetTargetWindowSize() {
        ImGuiIO& io = ImGui::GetIO();
        return io.DisplaySize;
    }
}