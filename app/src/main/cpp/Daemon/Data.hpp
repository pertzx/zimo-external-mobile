#pragma once
#include "Draw.hpp"

namespace Data {
    void StartReadThread();
    void StopReadThread();
    GameContext GetContext();
    std::vector<PlayerData>& GetPlayers();
    GameContext& GetContextRef();
    std::mutex& GetMutex();
}