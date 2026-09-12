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
    ImGuiIO& io = ImGui::GetIO();
    if (pointerId != 0) return;

    switch ((int)action) {
        case AMOTION_EVENT_ACTION_DOWN:
            g_lastTouchTime = std::chrono::steady_clock::now();
            g_touchActive = true;
            // Se ficou preso do gesto anterior, solta antes de apertar de novo
            if (io.MouseDown[0]) {
                io.AddMouseButtonEvent(0, false);
                io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);
            }
            io.AddMousePosEvent(x, y);
            io.AddMouseButtonEvent(0, true);
            break;

        case AMOTION_EVENT_ACTION_UP:
            io.AddMousePosEvent(x, y);
            io.AddMouseButtonEvent(0, false);   // força soltar SEMPRE
            io.AddMouseButtonEvent(0, false);   // duas vezes pra garantir
            break;

        case AMOTION_EVENT_ACTION_MOVE:
            g_lastTouchTime = std::chrono::steady_clock::now();
            io.AddMousePosEvent(x, y);
            break;

        case AMOTION_EVENT_ACTION_CANCEL:
            g_touchActive = false;
            io.AddMouseButtonEvent(0, false);
            io.AddMouseButtonEvent(0, false);   // força soltar SEMPRE
            io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);
            break;
    }
}

    // ============================================================
    //  NOVA — devolve [x, y, w, h] do painel ImGui pro Java.
    //  O OverlayService chama isso a cada 50 ms pra mover a
    //  janela de toque (Janela B) em cima do painel.
    // ============================================================
    JNIEXPORT jintArray JNICALL
    Java_com_stormcheats_OverlayService_nativeGetPanelBounds(JNIEnv* env, jobject thiz) {
        jint values[4] = {
            (jint)PanelApp::GetPanelX(),
            (jint)PanelApp::GetPanelY(),
            (jint)PanelApp::GetPanelW(),
            (jint)PanelApp::GetPanelH()
        };
        jintArray arr = env->NewIntArray(4);
        env->SetIntArrayRegion(arr, 0, 4, values);
        return arr;
    }

}