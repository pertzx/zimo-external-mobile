#pragma once

#include <cstdint>
#include <thread>
#include <atomic>
#include <chrono>
#include <vector>
#include <mutex>
#include "entity.hpp"
#include "../memory/memory.hpp"
#include "../memory/il2cpp.hpp"
#include "../memory/offsets.hpp"

namespace ZmInternal {
    namespace Daemon {
        namespace Game {

            class ReadLoop {
            public:
                ReadLoop();
                ~ReadLoop();

                // Start the read loop thread
                bool Start();

                // Stop the read loop thread
                void Stop();

                // Get the latest snapshot (thread-safe)
                bool GetLatestSnapshot(EntitySnapshot* snapshot, uint32_t& count);

                // Check if the read loop is running
                bool IsRunning() const { return m_running.load(); }

            private:
                // Main read loop function
                void ReadLoopThread();

                // Read entities from game memory
                bool ReadEntities(std::vector<EntitySnapshot>& entities);

                // Read a single entity
                bool ReadEntity(uintptr_t entityPtr, EntitySnapshot& entity);

                // Thread control
                std::atomic<bool> m_running{false};
                std::thread m_thread;

                // Snapshot data (double-buffered for thread safety)
                std::vector<EntitySnapshot> m_snapshot1;
                std::vector<EntitySnapshot> m_snapshot2;
                std::vector<EntitySnapshot>* m_currentSnapshot = &m_snapshot1;
                std::vector<EntitySnapshot>* m_readySnapshot = &m_snapshot2;
                std::mutex m_snapshotMutex;

                // Timing
                std::chrono::steady_clock::time_point m_lastReadTime{};
                static constexpr int READ_INTERVAL_MS = 16; // ~60 FPS

                // Anti-flicker / carry-over
                std::vector<EntitySnapshot> m_previousSnapshot{};
                static constexpr int CARRY_OVER_FRAMES = 3;

                // Watchdog
                std::chrono::steady_clock::time_point m_lastValidRead{};
                static constexpr int WATCHDOG_TIMEOUT_MS = 1000; // 1 second
            };

        } // namespace Game
    } // namespace Daemon
} // namespace ZmInternal