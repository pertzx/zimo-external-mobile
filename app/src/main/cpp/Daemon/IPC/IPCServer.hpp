#pragma once
#include "IPC/IPCProtocol.hpp"

namespace IPCServer {
    bool Start(const char* socketPath);
    void Stop();
    void ProcessCommands();
    void SyncState();
    bool IsRunning();
    void UpdateGameState(const IPC_GAME_STATE& state);
}
