#pragma once

#include <android/shared/IpcProtocol.h>
#include <thread>
#include <atomic>
#include <functional>
#include <mutex>
#include <condition_variable>
#include <vector>

namespace ZmInternal {
    namespace Daemon {
        namespace IPC {

            class ServerIPC {
            public:
                ServerIPC();
                ~ServerIPC();

                // Initialize the IPC server
                bool Initialize(const char* socketPath = IPC_SOCKET_PATH);

                // Shutdown the IPC server
                void Shutdown();

                // Set callbacks for received messages
                void SetConfigCallback(std::function<void(const IpcMsgConfig&)> callback);
                void SetCommandCallback(std::function<void(uint8_t commandId)> callback);

                // Send snapshot to connected clients
                bool SendSnapshot(const IpcMsgSnapshot& snapshot);

                // Send acknowledgment
                bool SendAck(uint32_t seq, bool success);

                // Send heartbeat
                bool SendHeartbeat();

            private:
                void AcceptThread();
                void ClientHandlerThread(int clientSocket);
                void ReceiveLoop(int clientSocket);

                int m_serverSocket = -1;
                std::thread m_acceptThread;
                std::vector<std::thread> m_clientThreads;
                std::atomic<bool> m_running{false};
                std::mutex m_mutex;
                std::condition_variable m_connectCV;

                std::function<void(const IpcMsgConfig&)> m_configCallback;
                std::function<void(uint8_t commandId)> m_commandCallback;

                // Connected clients
                std::vector<int> m_clientSockets;
                std::mutex m_clientMutex;
            };

        } // namespace IPC
    } // namespace Daemon
} // namespace ZmInternal