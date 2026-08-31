// dear imgui: Platform Backend for Android
// This needs to be used along with Renderer Backend (e.g. ImGui_ImplOpenGL3)

#pragma once
#include "imgui.h"
#include <android/native_window.h>  // <-- ADICIONAR
#include <android/input.h>          // <-- ADICIONAR

IMGUI_IMPL_API bool     ImGui_ImplAndroid_Init(ANativeWindow* window);
IMGUI_IMPL_API int32_t  ImGui_ImplAndroid_HandleInputEvent(const AInputEvent* input_event);
IMGUI_IMPL_API void     ImGui_ImplAndroid_Shutdown();
IMGUI_IMPL_API void     ImGui_ImplAndroid_NewFrame();
