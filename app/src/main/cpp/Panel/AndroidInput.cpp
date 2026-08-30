
#include "AndroidInput.hpp"
#include <android/input.h>
#include <imgui.h>
#include <imgui_impl_android.h>

namespace AndroidInput {
    static bool s_MenuKeyPressed = false;
    static int s_MenuKeyCode = AKEYCODE_INSERT; // Mapear para botão volume ou gesture

    bool IsKeyPressed(int keyCode) {
        // Mapear códigos de tecla Android para os do painel
        if (keyCode == 0x2F) return s_MenuKeyPressed; // INSERT mapeado
        return false;
    }

    int32_t HandleInputEvent(AInputEvent* event) {
        return ImGui_ImplAndroid_HandleInputEvent(event);
    }

    void SetMenuKeyPressed(bool pressed) {
        s_MenuKeyPressed = pressed;
    }

    // Mapear botão de volume para menu (comum em cheats mobile)
    void HandleKeyEvent(int32_t keyCode, bool down) {
        if (keyCode == AKEYCODE_VOLUME_UP) {
            s_MenuKeyPressed = down;
        }
    }
}
