#pragma once

#include <atomic>
#include <thread>
#include <chrono>
#include <memory>
#include "../memory/memory.hpp"
#include "../memory/il2cpp.hpp"
#include "../memory/offsets.hpp"
#include "../game/readloop.hpp"
#include "../ipc/server.hpp"

namespace ZmInternal {
    namespace Daemon {

        class Daemon {
        public:
            Daemon();
            ~Daemon();

            // Initialize the daemon
            bool Initialize(uintptr_t targetPid, const char* socketName = IPC_SOCKET_PATH);

            // Shutdown the daemon
            void Shutdown();

            // Main loop
            void Run();

            // Check if daemon is running
            bool IsRunning() const { return m_running.load(); }

        private:
            void ApplyConfig(const IpcMsgConfig& config);
            void HandleCommand(uint8_t commandId);

            std::atomic<bool> m_running{false};
            std::thread m_mainThread;

            // Components
            std::unique_ptr<Memory::MemoryManager> m_memory;
            std::unique_ptr<Game::ReadLoop> m_readLoop;
            std::unique_ptr<IPC::ServerIPC> m_ipcServer;

            // Configuration
            IpcMsgConfig m_currentConfig{};
            std::mutex m_configMutex;

            // Timing
            std::chrono::steady_clock::time_point m_lastSnapshotTime{};
            static constexpr int SNAPSHOT_INTERVAL_MS = 16; // ~60 FPS
        };

    } // namespace Daemon
} // namespace ZmInternal