#include "DaemonApp.hpp"
#include "IPC/IPCServer.hpp"

#include <Globals.hpp>

#include <android/log.h>

#include <chrono>
#include <thread>

#ifndef LOGI
#define LOGI(...) \
    __android_log_print( \
        ANDROID_LOG_INFO, \
        "StormDaemon", \
        __VA_ARGS__ \
    )
#endif

#ifndef LOGE
#define LOGE(...) \
    __android_log_print( \
        ANDROID_LOG_ERROR, \
        "StormDaemon", \
        __VA_ARGS__ \
    )
#endif

namespace DaemonApp {

void Run()
{
    g_Globals.General.ShutDown = false;

    LOGI("Daemon backend iniciado");

    if (!IPCServer::Start("/data/local/tmp/storm_daemon.sock")) {
        LOGE("Falha iniciando IPC");
        return;
    }

    while (!g_Globals.General.ShutDown && IPCServer::IsRunning()) {
        IPCServer::ProcessCommands();
        IPCServer::SyncState();
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }

    IPCServer::Stop();
    LOGI("Daemon backend encerrado");
}

} // namespace DaemonApp
