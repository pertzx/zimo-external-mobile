#include "Data.hpp"

#include "Globals.hpp"

#include <android/log.h>

#include <atomic>
#include <chrono>
#include <thread>

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "StormDaemon", __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "StormDaemon", __VA_ARGS__)

namespace Data
{
    namespace
    {
        std::vector<PlayerData> g_Players;
        GameContext g_Context{};

        std::mutex g_Mutex;
        std::atomic<bool> g_Running{false};

        std::thread* g_ReadThread = nullptr;
    }

    static void ReadLoop(bool n32, bool v31)
    {
        LOGI(
            "[Data] ReadLoop started N32=%d V31=%d",
            n32 ? 1 : 0,
            v31 ? 1 : 0
        );

        while (g_Running.load(std::memory_order_acquire) &&
               !g_Globals.General.ShutDown)
        {
            /*
             * A leitura real do jogo será colocada aqui.
             *
             * Por enquanto mantemos o daemon funcional e compilável
             * sem utilizar a implementação Windows de Draw.hpp.
             */

            std::this_thread::sleep_for(
                std::chrono::milliseconds(16)
            );
        }

        LOGI("[Data] ReadLoop stopped");
    }

    void StartReadThread()
    {
        bool expected = false;

        if (!g_Running.compare_exchange_strong(
                expected,
                true,
                std::memory_order_acq_rel))
        {
            return;
        }

        const bool n32 = g_Globals.General.N32;
        const bool v31 = g_Globals.General.V31;

        LOGI(
            "[Data] Starting read thread N32=%d V31=%d",
            n32 ? 1 : 0,
            v31 ? 1 : 0
        );

        if (g_ReadThread != nullptr)
        {
            if (g_ReadThread->joinable())
                g_ReadThread->join();

            delete g_ReadThread;
            g_ReadThread = nullptr;
        }

        g_ReadThread = new std::thread(
            [n32, v31]()
            {
                ReadLoop(n32, v31);
            }
        );
    }

    void StopReadThread()
    {
        if (!g_Running.exchange(
                false,
                std::memory_order_acq_rel))
        {
            return;
        }

        LOGI("[Data] Stopping read thread");

        if (g_ReadThread != nullptr)
        {
            if (g_ReadThread->joinable())
                g_ReadThread->join();

            delete g_ReadThread;
            g_ReadThread = nullptr;
        }

        std::lock_guard<std::mutex> lock(g_Mutex);

        g_Players.clear();
        g_Context = GameContext{};
    }

    GameContext GetContext()
    {
        std::lock_guard<std::mutex> lock(g_Mutex);
        return g_Context;
    }

    std::vector<PlayerData>& GetPlayers()
    {
        return g_Players;
    }

    GameContext& GetContextRef()
    {
        return g_Context;
    }

    std::mutex& GetMutex()
    {
        return g_Mutex;
    }

    void SetRunning(bool value)
    {
        g_Running.store(
            value,
            std::memory_order_release
        );
    }

    bool IsRunning()
    {
        return g_Running.load(
            std::memory_order_acquire
        );
    }
}