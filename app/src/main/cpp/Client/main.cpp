#include <jni.h>
#include <android/native_window.h>
#include <android/native_window_jni.h>
#include <android/input.h>
#include "PanelApp.hpp"
#include "Interface/FloatingKeys.hpp"
#include <imgui.h>
#include <android/log.h>

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "StormNative", __VA_ARGS__)

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
    //  Devolve [x, y, w, h] do painel ImGui pro Java.
    //  O OverlayService chama isso a cada 16 ms pra mover a
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

    // ============================================================
    //  BOTÕES FLUTUANTES de keybind — ponte Java <-> C++
    //
    //  nativeGetFloatingKeys(): devolve array plano com o rect de
    //  cada botão EM COORDENADAS DA SURFACE:
    //      [vk0, x0, y0, w0, h0, vk1, x1, y1, w1, h1, ...]
    //  O OverlayService usa isso pra posicionar UMA JANELINHA DE
    //  TOQUE sobre cada botão (é isso que torna os botões
    //  clicáveis fora do painel).
    //
    //  nativeFloatingKeyTouch(): o Java repassa ACTION_DOWN/UP de
    //  cada janelinha; o FloatingKeys decide se é toggle
    //  (modo clique) ou hold (modo segurar).
    //
    //  nativeFloatingKeyDrag(): o toque virou arrasto (dedo
    //  deslizou) — cancela o efeito do toque.
    //
    //  nativeFloatingKeyMove(): delta do dedo durante o arrasto —
    //  move o botão (ele fica onde foi solto).
    // ============================================================
    JNIEXPORT jintArray JNICALL
    Java_com_stormcheats_OverlayService_nativeGetFloatingKeys(JNIEnv* env, jobject thiz) {
        /*
         * Os rects foram preenchidos pelo FloatingKeys::DrawTick no último
         * frame (PanelApp::Run chama a cada loop). Aqui só copiamos pro Java.
         */
        int bounds[FloatingKeys::kMaxKeys * 5] = { 0 };
        int count = 0;

        FloatingKeys::GetBounds(bounds, (int)(sizeof(bounds) / sizeof(int)), count);

        jintArray arr = env->NewIntArray(count);
        if (count > 0)
            env->SetIntArrayRegion(arr, 0, count, bounds);
        return arr;
    }

    JNIEXPORT void JNICALL
    Java_com_stormcheats_OverlayService_nativeFloatingKeyTouch(JNIEnv* env, jobject thiz,
        jint vk, jboolean down) {
        FloatingKeys::OnTouch((int)vk, down == JNI_TRUE);
    }

    JNIEXPORT void JNICALL
    Java_com_stormcheats_OverlayService_nativeFloatingKeyDrag(JNIEnv* env, jobject thiz,
        jint vk) {
        FloatingKeys::DragCancel((int)vk);
    }

    JNIEXPORT void JNICALL
    Java_com_stormcheats_OverlayService_nativeFloatingKeyMove(JNIEnv* env, jobject thiz,
        jint vk, jfloat dx, jfloat dy) {
        FloatingKeys::MoveBy((int)vk, (float)dx, (float)dy);
    }

}