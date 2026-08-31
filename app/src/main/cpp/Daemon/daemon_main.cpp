#include "DaemonApp.hpp"
#include "../Shared/Globals.hpp"
#include <android/log.h>
#include <jni.h>
#include <signal.h>

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "StormDaemon", __VA_ARGS__)

static volatile bool g_DaemonRunning = false;

extern "C" {

JNIEXPORT void JNICALL
Java_com_stormcheats_DaemonService_nativeRunDaemon(JNIEnv* env, jobject thiz) {
    LOGI("Daemon Storm Cheats iniciado via JNI");
    signal(SIGPIPE, SIG_IGN);
    g_DaemonRunning = true;
    DaemonApp::Run();
    g_DaemonRunning = false;
}

JNIEXPORT void JNICALL
Java_com_stormcheats_DaemonService_nativeStopDaemon(JNIEnv* env, jobject thiz) {
    LOGI("Daemon stop requested");
    g_DaemonRunning = false;
    g_Globals.General.ShutDown = true;
}

} // extern "C"