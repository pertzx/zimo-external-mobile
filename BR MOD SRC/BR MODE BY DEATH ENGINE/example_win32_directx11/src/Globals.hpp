#pragma once
#include <Windows.h>
#include <EspLines/Player.h>
#include <EspLines/Math/Matrix4v4.hpp>
#include <unordered_map>
#include <mutex>

namespace Config {
    static float CornerHeight = 20.0f;

    enum class HitBox {
        Head,
        Neck,
        Chest
    };

    enum class Priority {
        High,
        Normal,
        Low
    };
}

class Globals {
public:
    struct AimBot {
        bool Enabled;
        bool AimLockActive = false;
        bool AimLock;
        bool AimShoulder;
        bool NoRecoil;
        bool FastReload;
        bool FastFire;
        bool AimbotFFMAX;
        bool IgnoreKnocked;
        bool IgnoreBots;
        int DistanceAim = 50;
        float Fov = 20.0f;
        // Silent Brutal convertido do módulo C# enviado.
        bool BrutalSilent = false;
        bool BrutalSilentIgnoreKnocked = false;
        bool BrutalSilentIgnoreBots = false;
        bool BrutalSilentFullHeadshot = false;
        bool BrutalSilentKillInChest = false;
        int BrutalSilentMaxDistance = 250;
        int BrutalSilentFov = 400;
        int ChestRate = 6;
        int HeadRate = 3;
        float Fillcolor[4] = { 0.f, 0.f, 0.f, 0.2f };
        int AimbotBind = VK_LBUTTON;
        int AimLockBind = VK_LBUTTON;
        int UpdateInterval = 11;
        int ShotDelay = 100;
        Config::HitBox HitBox = Config::HitBox::Head;
        bool AimBotVisibleSafe = false;
        float AimBotVisibleSafeStrength = 0.8f;
        bool NoGravityFly = false;
        bool VisionHack = false;
    };

    struct Exploits {
        bool UpPlayer = false;
        bool SpeedHack = false;
        bool WeaponAttributes = false;
        int WeaponAttributesLevel = 1;
        bool SpinBot = false;
        int SpinBotSpeed = 360;
        bool TeleportMark = false;
        int TeleportMarkBind = 0;
        bool SpeedTimer = false;
        int SpeedTimerBind = 0;
        int UpPlayerBind = 0;
        bool TeleKill = false;
        float TeleKillKeepDistance = 1.0f;
        bool GodMode = false;
        int TeleKillBind = 0;
        bool HighJump = false;
        bool DownPlayer = false;
        bool CameraHack = false;
        bool CameraHack1 = false;
        bool CameraHack2 = false;
        bool CameraHack3 = false;
        bool UnderCam = false;
        bool SpeedHack1 = false;
        bool SpeedHack2 = false;
        bool InfiniteAmmo = false;
        bool FastMedkit = false;
        bool FastSwitch = false;
        bool DamageBoost = false;
        bool FlyHack = false;
        bool Parachute = false;
        int FlyHackBind = 0;
        float VisionHack = 0.1f;
        int VisionHackOffset = 0x44;
        int UnderCamBind = 0;

        bool SpeedHackActive = false;
        bool InfiniteAmmoActive = false;
        bool FastMedkitActive = false;
        bool CameraHackActive = false;
        bool UnderCamActive = false;
        bool HighJumpActive = false;
        bool ParachuteActive = false;
        bool FlyHackActive = false;

        bool UmpXm8Vel2 = false;
        bool SalvarAmigoInsta = false;
        bool AntiTatu = false;
        bool PullEnemy = false;
        int PullEnemyBind = 0;
        float PullEnemyDistance = 250.0f;
        float PullEnemyFov = 1200.0f;
        int PullEnemyTickMs = 6;
        bool BackJump = false;
        bool TelaParada = false;
        bool SpeedLite = false;
        int SpeedLiteLevel = 0; // 0 = normal game speed, 10 = max Speed Lite
        bool SpeedRiskBan = false;
    };

    struct Visuals
    {
        float x, y;

        float LeftKneeOffset = -0.25f;
        float RightKneeOffset = -0.35f;
        float TextSize = 15.0f;
        float LogoScale = 0.75f;
        float Thickness = 1.0f;
        int DistanceEsp = 60;
        bool Enable = true;
        bool Watermark = true;
        bool Enemy = true;
        bool Lines = true;
        int EspLines = 1;
        float LinesColor[4] = { 1.f, 1.f, 1.f, 1.f };
        float KnockedColor[4] = { 1.f, 0.f, 0.f, 1.f };
        bool FilledBox = false;
        float Filledboxcolor[4] = { 0.f, 0.f, 0.f, 0.4f };

        bool Box = true;
        int players_box = 0;
        float BoxColor[4] = { 1.f, 1.f, 1.f, 1.f };

        bool ESPHealthTEXT;
        bool HealthBar = true;
        int players_healthbar = 1;
        float texthColor[4] = { 1.f, 1.f, 1.f, 1.f };

        bool ESPWeapon = false;
        bool ESPWeaponIcon = false;
        float ESPWeaponColor[4] = { 1.f, 1.f, 1.f, 1.f };

        bool Skeleton = true;
        float SkeletonColor[4] = { 1.f, 1.f, 1.f, 1.f };

        bool Alvo = true;
        float AlvoColor[4] = { 0.f, 1.f, 0.f, 1.f };

        bool Name = true;
        float NameColor[4] = { 1.f, 1.f, 1.f, 1.f };

        bool Distance = true;
        float DistColor[4] = { 0.7f, 0.7f, 0.7f, 1.f };

        bool ESPGranada = false;
        float ESPGranadaColor[4] = { 252.f / 255.f, 186.f / 255.f, 3.f / 255.f, 1.f };

        bool RadarHack = false;

        float WatermarkColor[4] = { 252.f / 255.f, 186.f / 255.f, 3.f / 255.f, 1.f };
        float EnemyColor[4] = { 252.f / 255.f, 186.f / 255.f, 3.f / 255.f, 1.f };

        bool Debug = false;
        float HipHeightOffset = 0.7f;
        float HipWidthOffset = 0.33f;
        float HipWidthScale = 0.19f;
        float LeftHipHeightOffset = 0.82f;
        float RightHipHeightOffset = 0.98f;

        float LeftKneeHeightOffset = 0.5f;
        float RightKneeHeightOffset = 0.5f;
        float KneeWidthScale = 0.5f;
        float LeftKneeWidthOffset = 0.1f;
        float RightKneeWidthOffset = 0.1f;
        float LeftKneeFlexionOffset = 0.1f;
        float RightKneeFlexionOffset = 0.1f;
    };

    struct Misc
    {
        bool ShowAimbotFov = false;
        float AimbotFovColor[4] = { 252.f / 255.f, 186.f / 255.f, 3.f / 255.f, 1.f };
        bool SniperScope = false;
        int SniperScopeMode = 1;
        float SniperFov = 500.0f;
        bool SniperSwitch = false;
        bool WallHack = false;
    };

    struct General
    {
        char Username[255] = "";
        char Password[255] = "";
        char Key[255] = "";
        bool ShutDown = false;
        bool Capture = false;
        Config::Priority Priority = Config::Priority::Low;
        int Delay = 0;
        int MenuKey = VK_INSERT;
        bool ShowKeybinds = true;
        bool ShowPlayerCounter = true;
        bool ShowCounterTimeCS = false;
        bool MenuKeyCapturing = false;
    };

    struct Esp {
        std::unordered_map<long, Player> Entities;
        std::mutex EntitiesMutex;
        Matrix4x4 ViewMatrix{};
        Vector3 MainCamera{};
        uint32_t LocalPlayer = 0;
        uint32_t previousCount = 0;
        uint32_t LastMatchId = 0;
        bool Matrix = false;
        int Width = 0;
        int Height = 0;
    };

    AimBot AimBot;
    Exploits Exploits;
    Visuals Visuals;
    Misc Misc;
    General General;
    Esp EspConfig;
};

inline Globals g_Globals;