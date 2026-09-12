#pragma once

#include <cstdint>
#include <cstddef>

static constexpr uint32_t IPC_MAGIC_STATE   = 0x53544154;
static constexpr uint32_t IPC_MAGIC_COMMAND = 0x434D444E;
static constexpr uint32_t IPC_MAGIC_PLAYERS = 0x504C4159;

enum class IPC_CMD_TYPE : uint32_t
{
    NONE = 0,
    UPDATE_CONFIG,
    RESTART_MEMORY,
    SAVE_CONFIG,
    LOAD_CONFIG,
};

struct IPC_CONFIG_STATE
{
    uint32_t Magic = IPC_MAGIC_STATE;
    uint32_t Seq = 0;

    /*
     * NOVO:
     * dimensoes reais do overlay.
     */
    int ScreenWidth = 0;
    int ScreenHeight = 0;

    bool AimBot_Enabled = false;
    int AimBot_Fov = 360;
    int AimBot_MaxDistance = 200;
    int AimBot_Target = 0;
    bool AimBot_IgnoreBots = false;
    bool AimBot_IgnoreKnocked = false;
    bool AimBot_VisibleCheck = false;
    bool AimBot_Pull = false;
    bool AimBot_Ghost = false;
    bool AimBot_ShowFov = false;
    int AimBot_KeyBind = 0;

    bool Silent_Enabled = false;
    int Silent_Fov = 30;
    int Silent_MaxDistance = 100;
    bool Silent_ShowFov = false;

    bool ESP_Enemy = false;
    bool ESP_ShowTeam = false;
    bool ESP_Watermark = false;
    bool ESP_Box = false;
    bool ESP_BoxFilled = false;
    bool ESP_ShowName = false;
    bool ESP_HealthBar = false;
    bool ESP_Distance = false;
    bool ESP_Skeleton = false;
    bool ESP_Weapon = false;
    bool ESP_SnapLines = false;
    int ESP_RenderDistance = 240;
    float ESP_Thickness = 1.0f;
    float ESP_TextSize = 15.0f;
    int ESP_BoxStyle = 1;
    int ESP_HealthBarStyle = 1;
    int ESP_WeaponStyle = 1;
    int ESP_SnapLinesPos = 1;

    bool Chams_Enabled = false;
    bool Chams_AggressiveMode = false;

    bool Exp_AimLock2x = false;
    bool Exp_AimbotAwm = false;
    bool Exp_NoRecoil = false;
    int Exp_RecoilControl = 100;
    bool Exp_FastMedkit = false;
    bool Exp_TelaParada = false;
    bool Exp_AtributarArma = false;
    int Exp_AtributarArmaLevel = 0;
    bool Exp_Aimlock = false;
    bool Exp_MoreDamage = false;
    bool Exp_FireDelay = false;
    bool Exp_BugarPixel = false;
    bool Exp_Precision = false;
    bool Exp_BackJump = false;
    bool Exp_SocoLonge = false;
    bool Exp_SpinBot = false;
    float Exp_SpinSpeed = 1.0f;

    int Gen_ThreadDelay = 240;
    bool Gen_CaptureBypass = true;
    bool Gen_ShutDown = false;
    bool Gen_N32 = false;
    bool Gen_V31 = false;

    float EnemyColor[4] = {
        1.0f, 0.0f, 0.0f, 1.0f
    };

    float TeamColor[4] = {
        0.2f, 0.6f, 1.0f, 1.0f
    };

    float BoxColor[4] = {
        1.0f, 1.0f, 1.0f, 1.0f
    };

    float SkeletonColor[4] = {
        1.0f, 1.0f, 1.0f, 1.0f
    };
};

struct IPC_COMMAND
{
    uint32_t Magic = IPC_MAGIC_COMMAND;
    IPC_CMD_TYPE Type = IPC_CMD_TYPE::NONE;
    uint32_t Seq = 0;
    uint32_t Padding = 0;
};

struct IPC_PLAYER_DATA
{
    float ScreenPos[2];
    float Box[4];
    float Health;
    float MaxHealth;
    bool IsTeam;
    bool IsVisible;
    bool IsKnocked;
    bool IsBot;
    float Distance;
    char Name[32];
    char Weapon[32];
    float SkeletonPoints[20][2];
    int SkeletonPointCount;
};

struct IPC_GAME_STATE
{
    uint32_t Magic = IPC_MAGIC_PLAYERS;
    uint32_t Seq = 0;
    uint32_t PlayerCount = 0;
    uint32_t MaxPlayers = 64;

    float LocalYaw;
    float ClosestEnemyDist;

    bool IsAimLocked;
    bool IsSilentActive;

    IPC_PLAYER_DATA Players[64];
};