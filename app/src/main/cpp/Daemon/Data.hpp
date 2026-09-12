#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <mutex>
// #include "./Draw/Draw.hpp"

#include <Unity/Unity.hpp>

enum PlayerType
{
    PLAYER_UNKNOWN = 0,
    PLAYER_NETWORK,
    PLAYER,
    PLAYER_TRAINING_HUMAN,
    PLAYER_TRAINING_STAND
};

struct PlayerData
{
    Vector3 HeadScreen{};
    Vector3 FeetScreen{};
    Vector3 ScreenPos{};

    Vector3 HeadWorld{};
    Vector3 FeetWorld{};

    float HealthPercent = 0.0f;

    bool IsKnocked = false;
    bool IsTeammate = false;
    bool IsVisible = false;
    bool IsBot = false;

    int WeaponID = 0;

    uintptr_t Entity = 0;
    uintptr_t UMAData = 0;

    std::string Name;
    std::string Weapon;

    float Distance = 0.0f;

    short CurrentHealth = 0;
    short MaxHealth = 0;

    std::vector<Vector3> Skeleton;

    std::int64_t LastSeenTick = 0;
};

struct GameContext
{
    uintptr_t LocalPlayer = 0;
    uintptr_t MatchGame = 0;
    uintptr_t Match = 0;
    uintptr_t MainCamera = 0;

    Matrix4x4 ViewMatrix{};

    bool IsObserving = false;

    float ClosestEnemyDist = 0.0f;
    float LocalYaw = 0.0f;
};

namespace Data
{
    void StartReadThread();
    void StopReadThread();

    GameContext GetContext();

    std::vector<PlayerData>& GetPlayers();
    GameContext& GetContextRef();
    std::mutex& GetMutex();

    void SetRunning(bool value);
    bool IsRunning();
}