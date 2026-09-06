#include "DaemonApp.hpp"
#include "IPC/IPCServer.hpp"
#include "Memory/Memory.hpp"
#include "Draw/Draw.hpp"
#include <android/log.h>
#include <thread>
#include <chrono>

// LOGI/LOGE are already defined in Shared/Includes.hpp
// Only define if not already defined
#ifndef LOGI
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "StormDaemon", __VA_ARGS__)
#endif
#ifndef LOGE
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "StormDaemon", __VA_ARGS__)
#endif

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

        // Preencher dados dos jogadores a partir de Data::GetPlayers()
        {
            std::lock_guard<std::mutex> lock(Data::GetMutex());
            auto& players = Data::GetPlayers();
            state.PlayerCount = 0;
            for (size_t i = 0; i < players.size() && state.PlayerCount < state.MaxPlayers; i++) {
                const PlayerData& src = players[i];
                IPC_PLAYER_DATA& dst = state.Players[state.PlayerCount];

                dst.ScreenPos[0] = src.ScreenPos.X;
                dst.ScreenPos[1] = src.ScreenPos.Y;
                dst.Box[0] = src.HeadScreen.X;  // Using HeadScreen as Box.x
                dst.Box[1] = src.HeadScreen.Y;  // Using HeadScreen as Box.y
                dst.Box[2] = src.FeetScreen.X;  // Using FeetScreen as Box.z
                dst.Box[3] = src.FeetScreen.Y;  // Using FeetScreen as Box.w
                dst.Health = src.CurrentHealth;
                dst.MaxHealth = src.MaxHealth;
                dst.IsTeam = src.IsTeammate;
                dst.IsVisible = src.IsVisible;
                dst.IsKnocked = src.IsKnocked;
                dst.IsBot = src.IsBot;
                dst.Distance = src.Distance;
                strncpy(dst.Name, src.Name.c_str(), sizeof(dst.Name) - 1);
                strncpy(dst.Weapon, src.Weapon.c_str(), sizeof(dst.Weapon) - 1);

                // Skeleton simplificado
                dst.SkeletonPointCount = 0;
                for (size_t s = 0; s < src.Skeleton.size() && dst.SkeletonPointCount < 20; s++) {
                    dst.SkeletonPoints[dst.SkeletonPointCount][0] = src.Skeleton[s].X;
                    dst.SkeletonPoints[dst.SkeletonPointCount][1] = src.Skeleton[s].Y;
                    dst.SkeletonPointCount++;
                }

                state.PlayerCount++;
            }
        }

        // Info geral
        auto& context = Data::GetContextRef();
        state.ClosestEnemyDist = context.ClosestEnemyDist;
        state.LocalYaw = context.LocalYaw;

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
