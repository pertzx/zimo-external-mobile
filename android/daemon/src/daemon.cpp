#include "daemon.hpp"
#include <android/log.h>
#include <thread>
#include <chrono>

#define LOG_TAG "ZmInternal-Daemon"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

namespace ZmInternal {
    namespace Daemon {

        Daemon::Daemon() {
            // Constructor
        }

        Daemon::~Daemon() {
            Shutdown();
        }

        bool Daemon::Initialize(uintptr_t targetPid, const char* socketName) {
            LOGI("Initializing daemon with target PID: %ju", targetPid);

            // Initialize memory manager
            m_memory = std::make_unique<Memory::MemoryManager>();
            if (!m_memory->Initialize(targetPid)) {
                LOGE("Failed to initialize memory manager");
                return false;
            }

            // Initialize read loop
            m_readLoop = std::make_unique<Game::ReadLoop>();
            if (!m_readLoop->Start()) {
                LOGE("Failed to start read loop");
                return false;
            }

            // Initialize IPC server
            m_ipcServer = std::make_unique<IPC::ServerIPC>();
            if (!m_ipcServer->Initialize(socketName)) {
                LOGE("Failed to initialize IPC server");
                return false;
            }

            // Set up IPC callbacks
            m_ipcServer->SetConfigCallback([this](const IpcMsgConfig& config) {
                ApplyConfig(config);
            });

            m_ipcServer->SetCommandCallback([this](uint8_t commandId) {
                HandleCommand(commandId);
            });

            m_running = true;
            m_mainThread = std::thread(&Daemon::Run, this);
            LOGI("Daemon initialized successfully");
            return true;
        }

        void Daemon::Shutdown() {
            LOGI("Shutting down daemon...");
            m_running = false;

            // Shutdown components
            if (m_mainThread.joinable()) {
                m_mainThread.join();
            }

            if (m_ipcServer) {
                m_ipcServer->Shutdown();
            }

            if (m_readLoop) {
                m_readLoop->Stop();
            }

            if (m_memory) {
                m_memory->Shutdown();
            }

            LOGI("Daemon shut down");
        }

        void Daemon::Run() {
            LOGI("Daemon main loop started");

            while (m_running.load()) {
                auto startTime = std::chrono::steady_clock::now();

                // Get latest snapshot from read loop
                EntitySnapshot snapshots[256]; // Max entities
                uint32_t count = 0;
                if (m_readLoop->GetLatestSnapshot(snapshots, count)) {
                    // Create IPC message snapshot
                    IpcMsgSnapshot ipcSnapshot{};
                    ipcSnapshot.count = count;

                    // Copy entity data
                    for (uint32_t i = 0; i < count && i < 256; i++) {
                        ipcSnapshot.entities[i] = snapshots[i];
                    }

                    // Send snapshot to clients
                    m_ipcServer->SendSnapshot(ipcSnapshot);
                }

                // Send heartbeat periodically
                static auto lastHeartbeat = std::chrono::steady_clock::now();
                auto now = std::chrono::steady_clock::now();
                if (std::chrono::duration_cast<std::chrono::seconds>(now - lastHeartbeat).count() >= 1) {
                    m_ipcServer->SendHeartbeat();
                    lastHeartbeat = now;
                }

                // Sleep to maintain frame rate
                auto endTime = std::chrono::steady_clock::now();
                auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime).count();
                if (elapsed < SNAPSHOT_INTERVAL_MS) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(SNAPSHOT_INTERVAL_MS - elapsed));
                }
            }

            LOGI("Daemon main loop ended");
        }

        void Daemon::ApplyConfig(const IpcMsgConfig& config) {
            std::lock_guard<std::mutex> lock(m_configMutex);
            m_currentConfig = config;
            LOGI("Config updated: aimbot=%d, esp=%d", config.aimbotEnabled, config.espEnabled);
            // In a real implementation, we would apply aimbot, ESP, etc. settings here
            // For now, we just store the config
        }

        void Daemon::HandleCommand(uint8_t commandId) {
            LOGI("Received command: %d", commandId);
            // Handle commands from client (restart, save config, etc.)
            // For now, just log it
        }

    } // namespace Daemon
} // namespace ZmInternal