#include "DaemonApp.hpp"
#include "IPC/IPCServer.hpp"
#include "Memory/Memory.hpp"
#include "Draw/Draw.hpp"
#include <android/log.h>
#include <thread>

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "StormDaemon", __VA_ARGS__)

void DaemonApp::Run() {
    // Inicializar memória
    if (!Memory::Initialize()) {
        LOGE("Falha ao inicializar memória");
        return;
    }

    // Iniciar servidor IPC
    IPCServer::Start("/data/local/tmp/storm_daemon.sock");

    // Iniciar thread de leitura (mesmo padrão do original)
    Data::StartReadThread();

    // Loop principal do daemon
    while (!g_Globals.General.ShutDown) {
        // Processar comandos do painel
        IPCServer::ProcessCommands();

        // Sincronizar estado para o painel
        IPCServer::SyncState();

        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    // Cleanup
    Data::StopReadThread();
    IPCServer::Stop();
    Memory::Shutdown();
}