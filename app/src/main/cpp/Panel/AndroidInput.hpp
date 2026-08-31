#pragma once
#include <android/input.h>

namespace AndroidInput {
    bool IsKeyPressed(int keyCode);
    int32_t HandleInputEvent(AInputEvent* event);
    void SetMenuKeyPressed(bool pressed);
    void HandleKeyEvent(int32_t keyCode, bool down);
    void ProcessEvents();
}
