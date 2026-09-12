#pragma once

#include <android/native_window.h>
#include <chrono>
#include "Globals.hpp"

static std::chrono::steady_clock::time_point g_lastTouchTime;
static bool g_touchActive = false;

namespace PanelApp {

    void Run(ANativeWindow* window);
    void OnResize(int width, int height);
    void RequestShutdown();

    // ===== NOVOS =====
    // Chamado pelo Interface::RenderGui() toda vez que o painel é desenhado
    void SetPanelBounds(float x, float y, float w, float h);

    // Lidos pelo JNI (nativeGetPanelBounds)
    float GetPanelX();
    float GetPanelY();
    float GetPanelW();
    float GetPanelH();
}