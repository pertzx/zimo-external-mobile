#pragma once
#include <android/native_window.h>
#include "Globals.hpp"

namespace PanelApp {
    void Run(ANativeWindow* window);
    void OnResize(int width, int height);
    void RequestShutdown();
}
