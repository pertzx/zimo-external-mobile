#include "DaemonApp.hpp"
#include "IPC/IPCServer.hpp"
#include "Memory/Memory.hpp"
#include "Draw/Draw.hpp"
#include <android/log.h>
#include <thread>
#include <chrono>

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "StormDaemon", __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "StormDaemon", __VA_ARGS__)

namespace DaemonApp {

void Run() {
    LOGI("Daemon Storm Cheats iniciado");

    // Inicializar memoria
    if (!g_FreeFireMemory.Initialize()) {
        LOGE("Falha ao inicializar memoria");
        return;
    }

    // Iniciar servidor IPC
    IPCServer::Start("/data/local/tmp/storm_daemon.sock");

    // Iniciar thread de leitura de dados do jogo (mesmo padrao do original)
    Data::StartReadThread();

    // Loop principal do daemon
    while (!g_Globals.General.ShutDown) {
        // Processar comandos do painel (configuracoes)
        IPCServer::ProcessCommands();

        // Preparar estado do jogo para enviar ao painel
        IPC_GAME_STATE state;
        memset(&state, 0, sizeof(state));
        state.Magic = IPC_MAGIC_PLAYERS;
        state.Seq++;

        // Preencher dados dos jogadores a partir de Data::m_Players
        {
            std::lock_guard<std::mutex> lock(Data::m_Mutex);
            state.PlayerCount = 0;
            for (size_t i = 0; i < Data::m_Players.size() && state.PlayerCount < state.MaxPlayers; i++) {
                const PlayerData& src = Data::m_Players[i];
                IPC_PLAYER_DATA& dst = state.Players[state.PlayerCount];

                dst.ScreenPos[0] = src.ScreenPos.x;
                dst.ScreenPos[1] = src.ScreenPos.y;
                dst.Box[0] = src.Box.x;
                dst.Box[1] = src.Box.y;
                dst.Box[2] = src.Box.z;
                dst.Box[3] = src.Box.w;
                dst.Health = src.Health;
                dst.MaxHealth = src.MaxHealth;
                dst.IsTeam = src.IsTeam;
                dst.IsVisible = src.IsVisible;
                dst.IsKnocked = src.IsKnocked;
                dst.IsBot = src.IsBot;
                dst.Distance = src.Distance;
                strncpy(dst.Name, src.Name.c_str(), sizeof(dst.Name) - 1);
                strncpy(dst.Weapon, src.Weapon.c_str(), sizeof(dst.Weapon) - 1);

                // Skeleton simplificado
                dst.SkeletonPointCount = 0;
                for (size_t s = 0; s < src.Skeleton.size() && dst.SkeletonPointCount < 20; s++) {
                    dst.SkeletonPoints[dst.SkeletonPointCount][0] = src.Skeleton[s].x;
                    dst.SkeletonPoints[dst.SkeletonPointCount][1] = src.Skeleton[s].y;
                    dst.SkeletonPointCount++;
                }

                state.PlayerCount++;
            }
        }

        // Info geral
        state.ClosestEnemyDist = Data::m_Context.ClosestEnemyDist;
        state.LocalYaw = Data::m_Context.LocalYaw;

        IPCServer::UpdateGameState(state);
        IPCServer::SyncState();

        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    // Cleanup
    Data::StopReadThread();
    IPCServer::Stop();
    g_FreeFireMemory.Shutdown();
}

} // namespace DaemonApp
