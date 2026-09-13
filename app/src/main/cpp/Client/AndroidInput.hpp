#pragma once

#pragma once
#include <cstdint>
#include <android/input.h>

/*
 * ============================================================================
 * AndroidInput.hpp
 * ============================================================================
 * Input do Android. Duas fontes:
 *   1. Tecla de menu (VOLUME UP) -> abre/fecha o painel.
 *   2. BOTÕES FLUTUANTES de keybind (FloatingKeys) -> todo vk na faixa
 *      0x7000+ é um botão de tela; o toque chega via JNI
 *      (OverlayService -> nativeFloatingKeyTouch -> FloatingKeys::OnTouch).
 *
 * GetAsyncKeyState() no Android é um stub que sempre devolve 0, então
 * TODAS as consultas de keybind do gameplay passam por IsKeyPressed() aqui.
 * ============================================================================
 */

namespace AndroidInput {
    bool IsKeyPressed(int keyCode);

    int32_t HandleInputEvent(AInputEvent* event);

    void SetMenuKeyPressed(bool pressed);

    void HandleKeyEvent(int32_t keyCode, bool down);

    void ProcessEvents();
}
