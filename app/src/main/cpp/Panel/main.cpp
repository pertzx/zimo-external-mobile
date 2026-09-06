
#include <jni.h>
#include <android/native_window.h>
#include <android/native_window_jni.h>
#include <android/input.h>
#include "PanelApp.hpp"
#include <imgui.h>
#include <android/log.h>

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "StormNative", __VA_ARGS__)

// Helper to create a motion event for ImGui
static AInputEvent* CreateTouchEvent(int action, float x, float y, int pointerId, int64_t eventTime) {
    // This is a simplified approach - we'll use ImGui IO directly instead
    return nullptr;
}

extern "C" {
    JNIEXPORT void JNICALL
    Java_com_stormcheats_OverlayService_nativeStartPanel(JNIEnv* env, jobject thiz, jobject surface) {
        ANativeWindow* window = ANativeWindow_fromSurface(env, surface);
        if (window) {
            LOGI("Iniciando painel nativo");
            PanelApp::Run(window);
            ANativeWindow_release(window);
        }
    }

    JNIEXPORT void JNICALL
    Java_com_stormcheats_OverlayService_nativeResize(JNIEnv* env, jobject thiz, jint width, jint height) {
        PanelApp::OnResize(width, height);
    }

    JNIEXPORT void JNICALL
    Java_com_stormcheats_OverlayService_nativeStopPanel(JNIEnv* env, jobject thiz) {
        PanelApp::RequestShutdown();
    }

    JNIEXPORT void JNICALL
    Java_com_stormcheats_OverlayService_nativeOnTouch(JNIEnv* env, jobject thiz,
            jint action, jfloat x, jfloat y, jint pointerId) {
        // Forward touch events directly to ImGui IO
        ImGuiIO& io = ImGui::GetIO();

        // Only process primary pointer (pointerId == 0) for simplicity
        if (pointerId != 0) return;

        int nativeAction = (int)action;
        float nativeX = (float)x;
        float nativeY = (float)y;

        switch (nativeAction) {
            case AMOTION_EVENT_ACTION_DOWN:
                io.AddMousePosEvent(nativeX, nativeY);
                io.AddMouseButtonEvent(0, true);
                break;
            case AMOTION_EVENT_ACTION_UP:
                io.AddMousePosEvent(nativeX, nativeY);
                io.AddMouseButtonEvent(0, false);
                break;
            case AMOTION_EVENT_ACTION_MOVE:
                io.AddMousePosEvent(nativeX, nativeY);
                break;
            case AMOTION_EVENT_ACTION_CANCEL:
                io.AddMouseButtonEvent(0, false);
                break;
        }
    }
}