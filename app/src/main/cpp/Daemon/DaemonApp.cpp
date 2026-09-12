#include "DaemonApp.hpp"
#include "IPC/IPCServer.hpp"
#include "Memory/Memory.hpp"
#include "Data.hpp"

#include <android/log.h>

#include <thread>
#include <chrono>
#include <cmath>
#include <cstring>

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

namespace DaemonApp
{

void Run()
{
    g_Globals.General.ShutDown = false;

    LOGI("Daemon Storm Cheats iniciado");

    /*
     * PRIMEIRO:
     * encontra PID + ELF + libil2cpp + offsets.
     */
    if (!g_FreeFireMemory.Initialize())
    {
        LOGE(
            "Memory.Initialize() falhou"
        );

        return;
    }

    LOGI(
        "PID alvo: %d",
        g_FreeFireMemory.GetTargetPid()
    );

    LOGI(
        "libil2cpp: 0x%lX",
        static_cast<unsigned long>(
            g_FreeFireMemory.GetLibIl2Cpp()
        )
    );

    /*
     * IPC.
     */
    if (!IPCServer::Start(
            "/data/local/tmp/storm_daemon.sock"
        ))
    {
        LOGE(
            "Falha iniciando IPC"
        );

        g_FreeFireMemory.Shutdown();
        return;
    }

    /*
     * Agora o leitor pode usar:
     *
     * Offsets::LibIl2Cpp
     * g_Globals.General.N32
     * offsets FF v7a
     */
    Data::StartReadThread();

    uint32_t sequence = 0;

    while (!g_Globals.General.ShutDown)
    {
        IPCServer::ProcessCommands();

        IPC_GAME_STATE state{};
        state.Magic =
            IPC_MAGIC_PLAYERS;

        state.Seq =
            ++sequence;

        state.PlayerCount = 0;
        state.MaxPlayers = 64;

        {
            std::lock_guard<std::mutex> lock(
                Data::GetMutex()
            );

            const auto& players =
                Data::GetPlayers();

            state.PlayerCount =
                static_cast<uint32_t>(
                    std::min<size_t>(
                        players.size(),
                        64
                    )
                );

            for (
                uint32_t i = 0;
                i < state.PlayerCount;
                ++i
            )
            {
                const PlayerData& src =
                    players[i];

                IPC_PLAYER_DATA& dst =
                    state.Players[i];

                dst.ScreenPos[0] =
                    src.HeadScreen.X;

                dst.ScreenPos[1] =
                    src.HeadScreen.Y;

                float left =
                    src.HeadScreen.X -
                    std::fabs(
                        src.FeetScreen.Y -
                        src.HeadScreen.Y
                    ) *
                    0.25f;

                float top =
                    src.HeadScreen.Y;

                float height =
                    std::fabs(
                        src.FeetScreen.Y -
                        src.HeadScreen.Y
                    );

                if (height < 2.0f)
                    height = 2.0f;

                float width =
                    height * 0.5f;

                dst.Box[0] = left;
                dst.Box[1] = top;
                dst.Box[2] = width;
                dst.Box[3] = height;

                dst.Health =
                    static_cast<float>(
                        src.CurrentHealth
                    );

                dst.MaxHealth =
                    static_cast<float>(
                        src.MaxHealth
                    );

                dst.IsTeam =
                    src.IsTeammate;

                dst.IsVisible =
                    src.IsVisible;

                dst.IsKnocked =
                    src.IsKnocked;

                dst.IsBot =
                    src.IsBot;

                dst.Distance =
                    src.Distance;

                std::memset(
                    dst.Name,
                    0,
                    sizeof(dst.Name)
                );

                std::strncpy(
                    dst.Name,
                    src.Name.c_str(),
                    sizeof(dst.Name) - 1
                );

                std::memset(
                    dst.Weapon,
                    0,
                    sizeof(dst.Weapon)
                );

                std::strncpy(
                    dst.Weapon,
                    src.Weapon.c_str(),
                    sizeof(dst.Weapon) - 1
                );

                dst.SkeletonPointCount = 0;
            }
        }

        GameContext context =
            Data::GetContext();

        state.ClosestEnemyDist =
            context.ClosestEnemyDist;

        state.LocalYaw =
            context.LocalYaw;

        IPCServer::UpdateGameState(
            state
        );

        IPCServer::SyncState();

        std::this_thread::sleep_for(
            std::chrono::milliseconds(2)
        );
    }

    Data::StopReadThread();

    IPCServer::Stop();

    g_FreeFireMemory.Shutdown();

    LOGI(
        "Daemon encerrado"
    );
}

}