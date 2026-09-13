#pragma once
#include <cstdint>
#include <cstring>

namespace Cheat {
    class Globals {
    public:
        struct AimBot
        {
            bool Enabled = false;
            int aimtype = 0;
            bool IgnoreKnocked = false;
            bool IgnoreBots = false;
            bool VisibleCheck = false;
            bool PraCima = false;
            int KeyBind = 0;
            bool aimmagnect = false;
            int MagKey = 0;
            bool ghost = false;
            int ghostkey = 0;
            int TPKey = 0;
            bool TP = false;
            int CityIndex = 0;
            int Fov = 360;
            int MaxDistance = 200;
            int Sleep = 0;
            float PraCimaValor = 0.50F;
            int PraCimaTempo = 50;
            int PeitosIndex = 0;
            int Target = 0;
        } AimBot;
        struct Silent
        {
            bool Enabled = false;
            int KeyBind = 0;
            int Fov = 30;
            int MaxDistance = 100;
        } Silent;
        struct Visuals
        {
            struct ESP
            {
                bool Enabled = false;
                bool ShowTeam = false;
                int RenderDistance = 240;
                float Thickness = 1.0f;
                float TextSize = 15.0f;
                bool Watermark = false;
                float WatermarkColor[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
                bool Enemy = false;
                float EnemyColor[4] = { 1.0f, 0.0f, 0.0f, 1.0f };
                float TeamColor[4] = { 0.2f, 0.6f, 1.0f, 1.0f };
                bool Weapon = false;
                bool ShowIcons = true;
                int WeaponStyle = 1;
                float WeaponColor[4] = { 1.f, 1.f, 1.f, 1.f };
                bool SnapLines = false;
                int SnapLinesPos = 1;
                float SnapLinesColor[4] = { 1.f, 1.f, 1.f, 1.f };
                bool HealthBar = false;
                int HealthBarStyle = 1;
                bool Box = false;
                bool BoxFilled = false;
                int BoxStyle = 1;
                float BoxColor[4] = { 1.f, 1.f, 1.f, 1.f };
                float FilledBoxColor[4] = { 1.f, 1.f, 1.f, 1.f };
                bool ShowName = false;
                float NameColor[4] = { 1.f, 1.f, 1.f, 1.f };
                bool Distance = false;
                float DistanceColor[4] = { 1.f, 1.f, 1.f, 1.f };
                bool Skeleton = false;
                bool SkeletonFingers = true;
                int SkeletonStyle = 0;
                float SkeletonColor[4] = { 1.f, 1.f, 1.f, 1.f };
            } ESP;
            struct Chams
            {
                bool Enabled = false;
                bool AggressiveMode = false;
                float NearColor[4] = { 0.18f, 1.0f, 0.0f, 1.0f };
                float FarColor[4] = { 1.0f, 0.0f, 0.0f, 1.0f };
            } Chams;
        } Visuals;
        struct Misc
        {
            struct Screen
            {
                bool ShowAimbotFov = false;
                float AimbotFovColor[4] = { 1.f, 1.f, 1.f, 1.f };
                float FilledFovColor[4] = { 0.f, 0.f, 0.f, 0.0f };
                bool ShowSilentFov = false;
                float SilentFovColor[4] = { 1.f, 1.f, 1.f, 1.f };
                float SilentFilledFovColor[4] = { 0.f, 0.f, 0.f, 0.0f };
            } Screen;
            struct Exploits
            {
                struct LocalPlayer
                {
                    bool AimLock2x = false;
                    bool NoRecoil = false;
                    bool Aimlock = false;
                    bool FastMedkit = false;
                    bool telaparada = false;
                    bool AtributarArma = false;
                    int AtributarArmaLevel = 0;
                    bool fly = false;
                    bool AimbotAwm = false;
                    bool MoreDamage = false;
                    bool SocoLonge = false;
                    bool FireDelay = false;
                    bool BugarPixel = false;
                    bool Precision = false;
                    bool BackJump = false;
                    bool SpinBot = false;
                    float SpinSpeed = 1.0f;
                    int RecoilControl = 100;
                } LocalPlayer;
            } Exploits;
        } Misc;
        struct General
        {
            int MenuKey = 0;
            int ThreadDelay = 240;
            bool CaptureBypass = true;
            bool WebRemote = false;
            bool ShutDown = false;
            bool EnableFuncs = false;

            /*
             * Modo dos BOTÕES FLUTUANTES de keybind (mobile):
             *   false = TOQUE SIMPLES : um toque liga, outro desliga
             *   true  = SEGURAR       : ligado só enquanto o dedo está no botão
             * Escolhido na aba Config.
             */
            bool FloatingKeysHold = false;

            /*
             * ============================================================
             * GameProfile — O TIPO DO JOGO, escolhido na aba Settings.
             * E a UNICA configuracao de jogo que existe agora. Essa escolha
             * define TUDO automaticamente:
             *
             *   0 = FF v7a build 75  -> offsets FFTHV7A75() + ptrs 4 bytes
             *   1 = FF v7a build 76  -> offsets FFTHV7A76() + ptrs 4 bytes
             *   2 = FF v8a           -> offsets FFTHV8A()   + ptrs 8 bytes
             *
             * Sem probe, sem validacao de versao, sem deteccao automatica
             * de arquitetura. A UNICA coisa que o app acha sozinho e a base
             * da libil2cpp.so (FindModuleBase no Memory::Initialize).
             *
             * Mudou aqui? Aperte "Apply + Restart" na Settings.
             * ============================================================
             */
            int GameProfile = 0;

            /*
             * Flags DERIVADAS do GameProfile (preenchidas pelo
             * Memory::Initialize). Usadas pelos templates de leitura do
             * ReadLoop/Silent/Skeleton — NAO EDITE A MAO.
             *
             *   N32 = true  -> jogo 32-bit (v7a): todo ponteiro lido com
             *                  Read<uint32_t> (4 bytes)
             *   N32 = false -> jogo 64-bit (v8a): ponteiros com
             *                  Read<uint64_t> (8 bytes)
             */
            bool N32 = true;
            bool V31 = false;
            bool NoAnogs = true;

            char Username[20] = {0};
            char Role[20] = {0};
            char PassWord[20] = {0};
            char License[255] = {0};
            char Local[256] = {0};
        } General;
    };
}
inline Cheat::Globals g_Globals;