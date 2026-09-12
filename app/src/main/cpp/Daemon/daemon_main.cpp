#include "DaemonApp.hpp"
#include "../Shared/Globals.hpp"

#include <android/log.h>
#include <jni.h>
#include <signal.h>

#define LOGI(...) \
    __android_log_print( \
        ANDROID_LOG_INFO, \
        "StormDaemon", \
        __VA_ARGS__ \
    )

#define LOGE(...) \
    __android_log_print( \
        ANDROID_LOG_ERROR, \
        "StormDaemon", \
        __VA_ARGS__ \
    )

static volatile bool g_DaemonRunning = false;

extern "C"
{

// ============================================================================
// RootDaemonMain
// ============================================================================

JNIEXPORT void JNICALL
Java_com_stormcheats_RootDaemonMain_nativeRunDaemon(
        JNIEnv* env,
        jclass clazz)
{
    (void)env;
    (void)clazz;

    LOGI(
        "========================================"
    );

    LOGI(
        "RootDaemonMain -> libdaemon.so"
    );

    LOGI(
        "Daemon Storm Cheats iniciado via JNI root"
    );

    signal(
        SIGPIPE,
        SIG_IGN
    );

    g_DaemonRunning = true;

    DaemonApp::Run();

    g_DaemonRunning = false;

    LOGI(
        "Daemon Storm Cheats finalizado"
    );
}

// ============================================================================
// Compatibilidade antiga - DaemonService
// ============================================================================

JNIEXPORT void JNICALL
Java_com_stormcheats_DaemonService_nativeRunDaemon(
        JNIEnv* env,
        jobject thiz)
{
    (void)env;
    (void)thiz;

    LOGI(
        "Daemon Storm Cheats iniciado via JNI antigo"
    );

    signal(
        SIGPIPE,
        SIG_IGN
    );

    g_DaemonRunning = true;

    DaemonApp::Run();

    g_DaemonRunning = false;
}

JNIEXPORT void JNICALL
Java_com_stormcheats_DaemonService_nativeStopDaemon(
        JNIEnv* env,
        jobject thiz)
{
    (void)env;
    (void)thiz;

    LOGI(
        "Daemon stop requested"
    );

    g_DaemonRunning = false;

    g_Globals.General.ShutDown =
        true;
}

}