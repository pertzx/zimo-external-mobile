#include "AndroidInput.hpp"
#include <android/input.h>
// ========== DEFINIR ANTES DE INCLUIR imgui.h ==========
// #define IMGUI_DEFINE_MATH_OPERATORS
#include <imgui.h>
#include <imgui_impl_android.h>
#include <Interface/FloatingKeys.hpp>

namespace AndroidInput {
    static bool s_MenuKeyPressed = false;
    static int s_MenuKeyCode = AKEYCODE_INSERT;

    bool IsKeyPressed(int keyCode) {
        if (keyCode == 0x2F) return s_MenuKeyPressed;

        /*
         * Keybinds no Android: um "key" nunca vem de teclado físico —
         * ou é 0 (nenhum bind) ou é um BOTÃO FLUTUANTE (vk 0x7000+).
         * O estado dele vive no FloatingKeys (tap = toggle / segurar).
         */
        return FloatingKeys::IsDown(keyCode);
    }

    int32_t HandleInputEvent(AInputEvent* event) {
        return ImGui_ImplAndroid_HandleInputEvent(event);
    }

    void SetMenuKeyPressed(bool pressed) {
        s_MenuKeyPressed = pressed;
    }

    void HandleKeyEvent(int32_t keyCode, bool down) {
        if (keyCode == AKEYCODE_VOLUME_UP) {
            s_MenuKeyPressed = down;
        }
    }

    void ProcessEvents() {
        // Input events sao processados via SurfaceView callback no Java
        // e repassados para ImGui_ImplAndroid_HandleInputEvent.
        // Nada a fazer aqui no loop nativo.
    }
}
