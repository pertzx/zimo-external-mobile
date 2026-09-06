#include "Draw/Draw.hpp"
#include "Globals.hpp"
#include <thread>
#include <chrono>
#include <android/log.h>

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "StormDaemon", __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "StormDaemon", __VA_ARGS__)

// Static members
std::vector<PlayerData> Data::m_Players;
GameContext Data::m_Context{ };
std::mutex Data::m_Mutex;
std::atomic<bool> Data::m_Running{ false };
void* Data::m_ThreadHandle = nullptr;
bool Data::m_SnapshotFresh = false;
std::atomic<LONGLONG> Data::m_LastFreshTick{ 0 };

void DataReadLoop(bool N32, bool V31)
{
    // Android implementation - placeholder
    // In a real implementation, this would read game memory
    LOGI("[Data] ReadLoop started (N32=%d, V31=%d)", N32, V31);

    while (Data::IsRunning() && !g_Globals.General.ShutDown)
    {
        // Placeholder - actual memory reading would go here
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }

    LOGI("[Data] ReadLoop stopped");
}

void Data::StartReadThread()
{
    if (m_Running.exchange(true))
        return;

    LOGI("[Data] Starting read thread");

    // Detect N32/V31 from globals
    bool N32 = g_Globals.General.N32;
    bool V31 = g_Globals.General.V31;

    m_ThreadHandle = (void*)new std::thread([=]() { DataReadLoop(N32, V31); });

    LOGI("[Data] Read thread started");
}

void Data::StopReadThread()
{
    if (!m_Running.exchange(false))
        return;

    LOGI("[Data] Stopping read thread");

    if (m_ThreadHandle)
    {
        std::thread* thread = static_cast<std::thread*>(m_ThreadHandle);
        if (thread->joinable())
            thread->join();
        delete thread;
        m_ThreadHandle = nullptr;
    }

    LOGI("[Data] Read thread stopped");
}

GameContext Data::GetContext()
{
    std::lock_guard<std::mutex> lock(m_Mutex);
    return m_Context;
}

void Data::SetRunning(bool value)
{
    m_Running.store(value);
}

bool Data::IsRunning()
{
    return m_Running.load();
}