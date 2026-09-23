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
            /*
             * FOV em PIXELS (cabeca vs centro da tela). 30 era colado na
             * mira: o recoil arrastava a cabeca pra fora do circulo no
             * meio do spray e a selecao derrubava o alvo (silent
             * intermitente). 150+ mantem o alvo selecionado o spray todo.
             */
            int Fov = 150;
            int MaxDistance = 100;

            /*
             * ====================================================================
             * SILENT FINO (Task 12) — forca, hit chance e filtros proprios.
             * ====================================================================
             * Forca (0-100): quanto MAIOR, menos jitter o write leva e
             * mais reto o tiro vai pra cabeca. 100 = linha reta (forca
             * maxima). Padrao 65 ja sai mais forte que o jitter fixo antigo.
             *
             * HitChance (0-100): % de writes que miram de verdade. Os
             * outros levam um desvio humano (em % da distancia) que ERRa
             * de verdade — e o desempenho no servidor vira estatistica de
             * jogador, nao de robo (anti-ban comportamental).
             *
             * VisibleCheck / IgnoreKnocked / IgnoreBots: filtros da
             * selecao de alvo do silent (Draw.cpp), gemeos dos do aimbot
             * e INDEPENDENTES. Visivel confirmado pelo jogo ganha sempre;
             * sem confirmacao nenhuma, ninguem e cortado (o silent nunca
             * morre por falta de sinal do oraculo); derrubado e bot saem
             * da candidatura.
             * ====================================================================
             */
            int  Forca         = 70;   // 0-100
            int  HitChance     = 60;   // 0-100 (%)
            bool VisibleCheck  = true;
            /*
             * VisibleCheckFov ("FOV Real"): visibilidade REAL por
             * entidade — mesh renderizado + cabeca na frente da
             * camera + dentro do FOV. NAO depende de mirar no alvo
             * (nao usa o oraculo do auto-lock). Independente do
             * VisibleCheck classico: os dois podem ficar ligados
             * juntos (vale o mais restritivo).
             */
            bool VisibleCheckFov = false;
            bool IgnoreKnocked = false;
            bool IgnoreBots    = false;
        } Silent;
        struct Visuals
        {
            struct ESP
            {
                bool Enabled = true;
                bool ShowTeam = false;
                int RenderDistance = 240;
                float Thickness = 1.5f;
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

            /*
             * ============================================================
             * (V9) MODS — portas do BR MOD (Death Engine) que nao existiam
             * no painel. Moram na aba "Mods" (tecla/icone novo no dock).
             * KeyBinds usam o MESMO sistema de botoes flutuantes do painel
             * (Custom::KeyBind + FloatingKeys, vk >= 0x7000).
             * ============================================================
             */
            struct Mods
            {
                /*
                 * SPEED LITE (SpeedTimer do BR MOD): reescreve o
                 * TimeService.m_FixedDeltaTime do jogo (0.033 padrao ->
                 * lerp ate 0.055 no nivel 10). Keybind = liga/desliga.
                 */
                bool SpeedLite = false;
                int SpeedLiteLevel = 3;      // 0..10
                int SpeedLiteKey = 0;

                /*
                 * TELE KILL: teleporta o inimigo mais proximo (ate 10 m)
                 * para uma posicao a TeleKeepDist metros na sua frente.
                 * Keybind = liga/desliga; enquanto ligado aplica continuo.
                 */
                bool TeleKill = false;
                float TeleKeepDist = 1.0f;   // 0.1..5
                int TeleKillKey = 0;

                /*
                 * TELEPORT MARK: teleporta para a marca do mapa
                 * (cadeia de UI: BaseGame.m_UIScene -> BigMapCtrl ->
                 * MapContentCtrl -> LocalMapMarkController.m_pos).
                 * Keybind = dispara (levita 5 m por 1 s + desce 0.5 s).
                 */
                bool TeleportMark = false;
                int TeleportMarkKey = 0;

                /*
                 * DOWN PLAYER: afunda o player local 0.9 m e congela a
                 * posicao ali; ao desligar restaura a posicao original.
                 */
                bool DownPlayer = false;
                int DownPlayerKey = 0;       // botao flutuante: toggle on/off

                /*
                 * UP PLAYER (hold): enquanto a tecla esta pressionada,
                 * levanta o inimigo mais proximo +2.5 m (write continuo).
                 */
                bool UpPlayer = false;
                int UpPlayerKey = 0;

                /*
                 * FLY (adaptacao do NoGravityFly): write de posicao no
                 * root do player local. Botao flutuante UP sobe, DOWN
                 * desce, movimento horizontal continua pelo joystick.
                 */
                bool Fly = false;
                float FlySpeed = 6.0f;       // m/s
                int FlyUpKey = 0;
                int FlyDownKey = 0;

                /*
                 * VISION HACK (FOV): escreve FOVOffset no FollowCamera do
                 * player local (padrao BR MOD = 75.0).
                 */
                bool VisionHack = false;
                float VisionFov = 75.0f;

                /*
                 * NO RELOAD: ReloadNoConsumeAmmoclip + ShootNoReload
                 * (PlayerAttributes) — pente nao consome / atira sem
                 * recarregar.
                 */
                bool NoReload = false;
            } Mods;

            /*
             * (V9) SKIN CHANGER — estado da aba Skin (a logica pesada e o
             * estado de aplicacao ficam no modulo Skin::ClothChanger).
             */
            struct Skin
            {
                int Category = 0;             // indice da categoria ativa
                char Search[48] = { 0 };      // filtro por nome
            } Skin;
        } Misc;
        struct General
        {
            int MenuKey = 0;
            int ThreadDelay = 240;

            /*
             * ============================================================
             * STEALTH DE LEITURA (anti-detecção) — o volume de reads era
             * o que fazia o FreeFire detectar em ~5 minutos.
             *
             *   ReadIntervalMs : intervalo entre varreduras completas do
             *                    ESP na thread de leitura. Antes = 1ms
             *                    (~1000 varreduras/seg — metrônomo
             *                    perfeito). Padrão agora = 60ms (~16 Hz,
             *                    ESP continua fluida, volume cai ~60x).
             *   StealthRead    : jitter aleatório de ±20% no intervalo,
             *                    pra quebrar a cadência periódica
             *                    (assinatura de automação).
             * Ajustáveis na aba Config.
             * ============================================================
             */
            int ReadIntervalMs = 60;
            bool StealthRead = true;

            /*
             * (V8.7) Overlay PERF no canto superior-esquerdo do jogo:
             * Hz real da varredura do ESP (responde ao Read Interval),
             * tempo/ondas/enderecos por frame, funil de entidades
             * (lista -> players -> desenhados) e PROVA DE BYPASS
             * (pread64 direto vs fallback process_vm do daemon).
             * Padrao ligado; desligue na aba Config se incomodar.
             */
            bool ShowPerfOverlay = true;

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

            /*
             * ============================================================
             * (V9.4 SLOTFIX) RVAs ALTERNATIVOS do slot GameFacade_TypeInfo.
             *
             * O offsetdumper (modulo Zygisk) so enxerga o slot se ele ja
             * estava RESOLVIDO no momento do scan. No il2cpp novo do v8a os
             * slots sao LAZY: cada um so vira ponteiro quando o codigo que
             * o referencia roda. Se o RVA principal caiu no slot de outra
             * classe (lib mudou de build), o token (0x2xxxxxxx) fica la pra
             * sempre e a cadeia morre.
             *
             * Aqui voce cola até 3 RVAs de UMA NOVA corrida do offsetdumper
             * na build atual (arquivo offsets_dump.txt). O cliente tenta:
             *   principal (Offsets.cpp) -> alt1 -> alt2 -> alt3
             * e usa o primeiro que voltar com ponteiro de heap valido.
             *
             * No arquivo de config ([Chain.Fix]) aceita HEX com prefixo 0x:
             *   TypeInfoAlt1=0xAC1E768
             * 0 = desativado. Mudou? Apply + Restart.
             * ============================================================
             */
            int TypeInfoAlt1 = 0;
            int TypeInfoAlt2 = 0;
            int TypeInfoAlt3 = 0;

            char Username[20] = {0};
            char Role[20] = {0};
            char PassWord[20] = {0};
            char License[255] = {0};
            char Local[256] = {0};
        } General;
    };
}
inline Cheat::Globals g_Globals;