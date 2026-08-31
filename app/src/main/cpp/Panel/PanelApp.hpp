#pragma once
#include <android/native_window.h>

namespace PanelApp {
    void Run(ANativeWindow* window);
    void OnResize(int width, int height);
    void RequestShutdown();
}
