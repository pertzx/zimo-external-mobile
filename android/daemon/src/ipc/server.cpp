#include "server.hpp"
#include <android/shared/IpcProtocol.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <cstring>
#include <chrono>
#include <thread>
#include <iostream>
#include <vector>
#include <algorithm>

namespace ZmInternal {
    namespace Daemon {
        namespace IPC {

            ServerIPC::ServerIPC() {
                // Constructor
            }

            ServerIPC::~ServerIPC() {
                Shutdown();
            }

            bool ServerIPC::Initialize(const char* socketPath) {
                if (m_running) {
                    return true; // Already initialized
                }

                m_running = true;

                // Start accept thread
                m_acceptThread = std::thread(&ServerIPC::AcceptThread, this, socketPath);
                LOGI("IPC server initialized");
                return true;
            }

            void ServerIPC::Shutdown() {
                if (!m_running) {
                    return;
                }

                m_running = false;

                // Notify threads to wake up
                {
                    std::lock_guard<std::mutex> lock(m_mutex);
                    m_connectCV.notify_all();
                }

                // Close server socket
                if (m_serverSocket >= 0) {
                    close(m_serverSocket);
                    m_serverSocket = -1;
                }

                // Close all client sockets
                {
                    std::lock_guard<std::mutex> lock(m_clientMutex);
                    for (int clientSocket : m_clientSockets) {
                        if (clientSocket >= 0) {
                            close(clientSocket);
                        }
                    }
                    m_clientSockets.clear();
                }

                // Wait for accept thread to finish
                if (m_acceptThread.joinable()) {
                    m_acceptThread.join();
                }

                // Wait for client handler threads to finish
                for (auto& thread : m_clientThreads) {
                    if (thread.joinable()) {
                        thread.join();
                    }
                }
                m_clientThreads.clear();

                LOGI("IPC server shut down");
            }

            void ServerIPC::SetConfigCallback(std::function<void(const IpcMsgConfig&)> callback) {
                m_configCallback = callback;
            }

            void ServerIPC::SetCommandCallback(std::function<void(uint8_t commandId)> callback) {
                m_commandCallback = callback;
            }

            bool ServerIPC::SendSnapshot(const IpcMsgSnapshot& snapshot) {
                // Calculate actual size of snapshot (variable length)
                size_t payloadSize = sizeof(uint32_t) + (snapshot.count - 1) * sizeof(EntitySnapshot) + sizeof(EntitySnapshot);
                // Actually, it's simpler: the size field in header should be sizeof(IpcMsgSnapshot) - sizeof(EntitySnapshot[0]) + (count * sizeof(EntitySnapshot))
                // But since we defined entities[1], the total size is: sizeof(IpcMsgHeader) + sizeof(uint32_t) + count * sizeof(EntitySnapshot)

                // For simplicity in this implementation, we'll assume a fixed max size
                // In a real implementation, we'd calculate the exact size

                // Create header
                IpcHeader header;
                header.magic = IPC_MAGIC;
                header.version = IPC_VERSION;
                header.type = IPC_MSG_SNAPSHOT;
                header.size = sizeof(IpcMsgSnapshot) - sizeof(EntitySnapshot) + (snapshot.count * sizeof(EntitySnapshot)); // Correct calculation
                header.seq = 0; // In a real implementation, this would be incremented per message
                header.checksum = 0; // Will be calculated below

                // Calculate checksum (only on payload)
                header.checksum = calculateChecksum(&snapshot, header.size);

                bool success = true;

                // Send to all connected clients
                std::lock_guard<std::mutex> lock(m_clientMutex);
                for (int clientSocket : m_clientSockets) {
                    if (clientSocket < 0) continue;

                    // Send header
                    ssize_t sent = send(clientSocket, &header, sizeof(header), 0);
                    if (sent != static_cast<ssize_t>(sizeof(header))) {
                        success = false;
                        continue;
                    }

                    // Send payload
                    sent = send(clientSocket, &snapshot, header.size, 0);
                    if (sent != static_cast<ssize_t>(header.size)) {
                        success = false;
                        continue;
                    }
                }

                return success;
            }

            bool ServerIPC::SendAck(uint32_t seq, bool success) {
                // Create header
                IpcHeader header;
                header.magic = IPC_MAGIC;
                header.version = IPC_VERSION;
                header.type = IPC_MSG_ACK;
                header.size = sizeof(IpcMsgAck);
                header.seq = seq;
                header.checksum = 0; // Will be calculated below

                // Create payload
                IpcMsgAck ack;
                ack.seq = seq;
                ack.success = success;

                // Calculate checksum
                header.checksum = calculateChecksum(&ack, sizeof(ack));

                bool ret = true;

                // Send to all connected clients
                std::lock_guard<std::mutex> lock(m_clientMutex);
                for (int clientSocket : m_clientSockets) {
                    if (clientSocket < 0) continue;

                    // Send header
                    ssize_t sent = send(clientSocket, &header, sizeof(header), 0);
                    if (sent != static_cast<ssize_t>(sizeof(header))) {
                        ret = false;
                        continue;
                    }

                    // Send payload
                    sent = send(clientSocket, &ack, sizeof(ack), 0);
                    if (sent != static_cast<ssize_t>(sizeof(ack))) {
                        ret = false;
                        continue;
                    }
                }

                return ret;
            }

            bool ServerIPC::SendHeartbeat() {
                // Create header
                IpcHeader header;
                header.magic = IPC_MAGIC;
                header.version = IPC_VERSION;
                header.type = IPC_MSG_HEARTBEAT;
                header.size = sizeof(IpcMsgHeartbeat);
                header.seq = 0; // In a real implementation, this would be incremented
                header.checksum = 0; // Will be calculated below

                // Create payload
                IpcMsgHeartbeat heartbeat;
                heartbeat.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::system_clock::now().time_since_epoch()
                ).count();

                // Calculate checksum
                header.checksum = calculateChecksum(&heartbeat, sizeof(heartbeat));

                bool ret = true;

                // Send to all connected clients
                std::lock_guard<std::mutex> lock(m_clientMutex);
                for (int clientSocket : m_clientSockets) {
                    if (clientSocket < 0) continue;

                    // Send header
                    ssize_t sent = send(clientSocket, &header, sizeof(header), 0);
                    if (sent != static_cast<ssize_t>(sizeof(header))) {
                        ret = false;
                        continue;
                    }

                    // Send payload
                    sent = send(clientSocket, &heartbeat, sizeof(heartbeat), 0);
                    if (sent != static_cast<ssize_t>(sizeof(heartbeat))) {
                        ret = false;
                        continue;
                    }
                }

                return ret;
            }

            void ServerIPC::AcceptThread(const char* socketPath) {
                LOGI("Accept thread started");

                // Create socket
                m_serverSocket = socket(AF_UNIX, SOCK_SEQPACKET, 0);
                if (m_serverSocket < 0) {
                    LOGE("Failed to create socket");
                    return;
                }

                // Set up address
                struct sockaddr_un addr;
                memset(&addr, 0, sizeof(addr));
                addr.sun_family = AF_UNIX;
                strncpy(addr.sun_path + 1, socketPath + 1, sizeof(addr.sun_path) - 2); // Skip leading \0 for abstract socket

                // Bind socket
                if (bind(m_serverSocket, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
                    LOGE("Failed to bind socket");
                    close(m_serverSocket);
                    m_serverSocket = -1;
                    return;
                }

                // Listen for connections
                if (listen(m_serverSocket, 5) < 0) {
                    LOGE("Failed to listen on socket");
                    close(m_serverSocket);
                    m_serverSocket = -1;
                    return;
                }

                LOGI("IPC server listening on %s", socketPath);

                // Accept connections
                while (m_running) {
                    struct sockaddr_un clientAddr;
                    socklen_t clientAddrLen = sizeof(clientAddr);
                    int clientSocket = accept(m_serverSocket, reinterpret_cast<struct sockaddr*>(&clientAddr), &clientAddrLen);

                    if (clientSocket < 0) {
                        if (!m_running) {
                            break; // Shutting down
                        }
                        LOGE("Failed to accept connection");
                        std::this_thread::sleep_for(std::chrono::milliseconds(100));
                        continue;
                    }

                    LOGI("New client connected");

                    // Add to client list
                    {
                        std::lock_guard<std::mutex> lock(m_clientMutex);
                        m_clientSockets.push_back(clientSocket);
                    }

                    // Start handler thread for this client
                    m_clientThreads.emplace_back(&ServerIPC::ClientHandlerThread, this, clientSocket);
                }

                LOGI("Accept thread ended");
            }

            void ServerIPC::ClientHandlerThread(int clientSocket) {
                LOGI("Client handler thread started for socket %d", clientSocket);

                ReceiveLoop(clientSocket);

                // Client disconnected, clean up
                LOGI("Client disconnected, cleaning up socket %d", clientSocket);
                close(clientSocket);

                // Remove from client list
                {
                    std::lock_guard<std::mutex> lock(m_clientMutex);
                    auto it = std::find(m_clientSockets.begin(), m_clientSockets.end(), clientSocket);
                    if (it != m_clientSockets.end()) {
                        m_clientSockets.erase(it);
                    }
                }

                LOGI("Client handler thread ended for socket %d", clientSocket);
            }

            void ServerIPC::ReceiveLoop(int clientSocket) {
                while (m_running) {
                    // Receive header
                    IpcHeader header;
                    ssize_t received = recv(clientSocket, &header, sizeof(header), 0);
                    if (received != static_cast<ssize_t>(sizeof(header))) {
                        // Connection likely closed or error
                        break;
                    }

                    // Validate header
                    if (!validateIpcHeader(&header)) {
                        LOGE("Invalid IPC header received");
                        continue;
                    }

                    // Receive payload based on type
                    if (header.type == IPC_MSG_CONFIG) {
                        IpcMsgConfig config;
                        received = recv(clientSocket, &config, sizeof(config), 0);
                        if (received == static_cast<ssize_t>(sizeof(config)) && m_configCallback) {
                            // Verify checksum
                            uint32_t expectedChecksum = header.checksum;
                            header.checksum = 0; // Zero out for calculation
                            uint32_t calculatedChecksum = calculateChecksum(&config, sizeof(config));
                            header.checksum = expectedChecksum; // Restore

                            if (expectedChecksum == calculatedChecksum) {
                                m_configCallback(config);
                            } else {
                                LOGE("Invalid checksum for CONFIG message");
                            }
                        }
                    }
                    else if (header.type == IPC_MSG_COMMAND) {
                        IpcMsgCommand command;
                        received = recv(clientSocket, &command, sizeof(command), 0);
                        if (received == static_cast<ssize_t>(sizeof(command)) && m_commandCallback) {
                            // Verify checksum
                            uint32_t expectedChecksum = header.checksum;
                            header.checksum = 0; // Zero out for calculation
                            uint32_t calculatedChecksum = calculateChecksum(&command, sizeof(command));
                            header.checksum = expectedChecksum; // Restore

                            if (expectedChecksum == calculatedChecksum) {
                                m_commandCallback(command.commandId);
                            } else {
                                LOGE("Invalid checksum for COMMAND message");
                            }
                        }
                    }
                    else {
                        // Unknown or unhandled message type, skip payload
                        // In a real implementation, we might want to log this
                        if (lseek(clientSocket, header.size, SEEK_CUR) == (off_t)-1) {
                            // lseek failed, connection might be bad
                            break;
                        }
                    }
                }
            }

        } // namespace IPC
    } // namespace Daemon
} // namespace ZmInternal