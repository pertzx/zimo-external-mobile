#pragma once
#include "IPC/IPCProtocol.hpp"

namespace IPCClient {
    bool Connect(const char* socketPath);
    void Disconnect();
    void SyncConfigToDaemon();
    void SyncStateFromDaemon();
    void RenderESP();
    bool IsConnected();
    const IPC_GAME_STATE& GetLastState();
}
