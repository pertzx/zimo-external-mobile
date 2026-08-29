#pragma once

#include <android/shared/IpcProtocol.h>
#include <thread>
#include <atomic>
#include <functional>
#include <mutex>
#include <condition_variable>

namespace ZmInternal {
    namespace IPC {

        class ClientIPC {
        public:
            ClientIPC();
            ~ClientIPC();

            // Initialize the IPC client
            bool Initialize(const char* socketPath = IPC_SOCKET_PATH);

            // Shutdown the IPC client
            void Shutdown();

            // Send configuration to daemon
            bool SendConfig(const IpcMsgConfig& config);

            // Send command to daemon
            bool SendCommand(uint8_t commandId);

            // Set callbacks for received messages
            void SetSnapshotCallback(std::function<void(const IpcMsgSnapshot&)> callback);
            void SetAckCallback(std::function<void(const IpcMsgAck&)> callback);
            void SetHeartbeatCallback(std::function<void(const IpcMsgHeartbeat&)> callback);

        private:
            void ReceiveThread();
            void ReconnectLoop();

            int m_socket = -1;
            std::thread m_receiveThread;
            std::thread m_reconnectThread;
            std::atomic<bool> m_running{false};
            std::atomic<bool> m_connected{false};
            std::mutex m_mutex;
            std::condition_variable m_connectCV;

            std::function<void(const IpcMsgSnapshot&)> m_snapshotCallback;
            std::function<void(const IpcMsgAck&)> m_ackCallback;
            std::function<void(const IpcMsgHeartbeat&)> m_heartbeatCallback;
        };

    } // namespace IPC
} // namespace ZmInternal