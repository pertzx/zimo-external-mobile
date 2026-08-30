
#include <jni.h>
#include <android/native_window.h>
#include <android/native_window_jni.h>
#include "PanelApp.hpp"
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
}