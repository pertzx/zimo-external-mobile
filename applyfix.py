#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Aplica as 9 correções na cópia do repo e produz os arquivos COMPLETOS
prontos pra colar em /home/z/my-project/download/zimo-fix/

Cada substituição é validada (count == 1) — se algo não bater, falha alto.
"""
import os, sys, shutil

REPO = "C:\zimo-external-mobile"
OUT  = "C:\zimo-external-fix"

# (arquivo) -> lista de (old, new)
R = {}

# ═══════════════════════════════════════════════════════════════════
# ③ WRITE QUEBRADO — BridgeClient.cpp não setava req.PayloadSize.
# O daemon lia 0 bytes -> BRIDGE_ERR_INVALID silencioso + bytes órfãos
# dessincronizando o socket. É o motivo de ZERO writes no log do daemon.
# ═══════════════════════════════════════════════════════════════════
R["app/src/main/cpp/Client/Memory/BridgeClient.cpp"] = [
(
"""    req.Cmd =
        BRIDGE_CMD_WRITE;

    req.Pid = pid;
    req.Address = address;
    req.Size = size;

    BridgeResponse resp{};""",
"""    req.Cmd =
        BRIDGE_CMD_WRITE;

    req.Pid = pid;
    req.Address = address;
    req.Size = size;

    /*
     * FIX DO WRITE (canal morto): o header precisa declarar o tamanho
     * do payload. Antes req.PayloadSize ficava 0, então o daemon lia
     * ZERO bytes -> payloadIn.size() (0) != req.Size ->
     * BRIDGE_ERR_INVALID SILLENCIOSO (sem log nenhum), e os bytes do
     * valor ficavam órfãos no stream dessincronizando a conexão.
     * Era isso que deixava o log do daemon sem NENHUM write e todos
     * os exploits que escrevem sem funcionar.
     */
    req.PayloadSize = size;

    BridgeResponse resp{};"""
),
]

# ═══════════════════════════════════════════════════════════════════
# ①②⑧ Custom.hpp — assinatura do KeyBind ganha o bool da função
# ═══════════════════════════════════════════════════════════════════
R["app/src/main/External/ImGui/Custom.hpp"] = [
(
"""    bool KeyBind(const char* label, int* Key, bool IsBlockMouse = false);""",
"""    /*
     * ANDROID: FeatureFlag (opcional) liga o botão flutuante ao toggle
     * da função — sincronização bidirecional com o checkbox do painel.
     */
    bool KeyBind(const char* label, int* Key, bool IsBlockMouse = false, bool* FeatureFlag = nullptr);"""
),
]

# ═══════════════════════════════════════════════════════════════════
# ①②⑧ Custom.cpp — KeyBind Show/Hide + sync; ④ TouchScroll fixes
# ═══════════════════════════════════════════════════════════════════
R["app/src/main/External/ImGui/Custom.cpp"] = [
# assinatura
(
"""    bool KeyBind(const char* label, int* Key, bool IsBlockMouse) {""",
"""    bool KeyBind(const char* label, int* Key, bool IsBlockMouse, bool* FeatureFlag) {"""
),
# texto do box: Show/Hide em vez de F#/None
(
"""/*
 * ANDROID: não existe teclado físico. O KeyBind aqui é um ATALHO pra
 * criar/remover o BOTÃO FLUTUANTE da função (FloatingKeys). O box mostra
 * o nome do botão ("F1", "F2"...) ou "None". Tocar de novo no box com um
 * botão já criado remove ele da tela.
 */
const char* keyText = (*Key != 0) ? FloatingKeys::VkLabel(*Key) : "None";""",
"""/*
 * ANDROID: não existe teclado físico. O KeyBind aqui é um ATALHO pra
 * spawnar/remover o BOTÃO FLUTUANTE da função (FloatingKeys). O box
 * mostra "Show" (spawnar) ou "Hide" (remover) — sem numeração F1/F2/F3.
 * O botão fica SINCRONIZADO com o toggle da função (FeatureFlag):
 * tocar no botão liga/desliga a função e o checkbox do painel acompanha
 * na hora — e mudar o checkbox atualiza o botão também.
 */
const char* keyText = ((*Key != 0) && FloatingKeys::Exists(*Key)) ? "Hide" : "Show";"""
),
# handler de toque
(
"""        /*
         * ANDROID — criar/remover o botão flutuante:
         *   box "None" + toque = cria um botão com o label da função
         *   box "F#"  + toque = remove o botão da tela
         */
        if (pressed) {
            if (*Key == 0) {
                *Key = FloatingKeys::Acquire(label);
            } else {
                FloatingKeys::Release(*Key);
                *Key = 0;
            }
        }""",
"""        /*
         * ANDROID — spawnar/remover o botão flutuante + manter ele
         * sincronizado com o toggle da função (bidirecional).
         */
        if (FeatureFlag && *Key != 0)
            FloatingKeys::Bind(*Key, FeatureFlag);

        if (pressed) {
            if (*Key == 0 || !FloatingKeys::Exists(*Key)) {
                const int newVk = FloatingKeys::Acquire(label);
                if (newVk != 0) {
                    *Key = newVk;
                    if (FeatureFlag)
                        FloatingKeys::Bind(*Key, FeatureFlag);
                }
            } else {
                FloatingKeys::Release(*Key);
                *Key = 0;
            }
        }"""
),
# ④ threshold mais sensível
(
"""                    if (fabsf(dyTot) > 14.0f && fabsf(dyTot) > fabsf(dxTot) * 1.25f) {""",
"""                    if (fabsf(dyTot) > 10.0f && fabsf(dyTot) > fabsf(dxTot) * 1.15f) {"""
),
# ④ não abortar scroll quando o dedo sai do child + cancelar widget ativo
(
"""                if (g_TS.steal) {
                    child->Scroll.y -= dyFrame;

                    const long long now = TS_NowMs();
                    const float dt = (float)(now - g_TS.lastMs) / 1000.0f;
                    if (dt > 0.001f)
                        g_TS.vel = g_TS.vel * 0.65f + (-dyFrame / dt) * 0.35f;
                    g_TS.lastMs = now;
                }

                g_TS.lastY = io.MousePos.y;
            }
        } else {
            // Dedo soltou (ou saiu do child)
            if (g_TS.steal && g_TS.winId == child->ID && g_TS.vel != 0.0f)
                g_ScrollInertia[child->ID] = g_TS.vel;

            g_TS.decided = false;
            g_TS.steal = false;
            g_TS.vel = 0.0f;
        }""",
"""                if (g_TS.steal) {
                    // Mantém o widget da origem cancelado o gesto todo
                    // (evita o checkbox/slider "acordar" no meio do scroll)
                    if (g.ActiveId != 0 && g.ActiveId == g_TS.activeAtDown)
                        ClearActiveID();

                    child->Scroll.y -= dyFrame;

                    const long long now = TS_NowMs();
                    const float dt = (float)(now - g_TS.lastMs) / 1000.0f;
                    if (dt > 0.001f)
                        g_TS.vel = g_TS.vel * 0.65f + (-dyFrame / dt) * 0.35f;
                    g_TS.lastMs = now;
                }

                g_TS.lastY = io.MousePos.y;
            }
        } else if (mouseDown && g_TS.decided && g_TS.steal && g_TS.winId == child->ID) {
            /*
             * FIX DO SCROLL: o dedo saiu da área do child NO MEIO de uma
             * rolagem já confirmada. Antes o gesto era abortado na hora —
             * o scroll travava e as opções de baixo ficavam inalcançáveis.
             * Agora a rolagem continua no child original até o dedo subir.
             */
            const float dyOut = io.MousePos.y - g_TS.lastY;

            if (g.ActiveId != 0 && g.ActiveId == g_TS.activeAtDown)
                ClearActiveID();

            child->Scroll.y -= dyOut;

            const long long now = TS_NowMs();
            const float dtOut = (float)(now - g_TS.lastMs) / 1000.0f;
            if (dtOut > 0.001f)
                g_TS.vel = g_TS.vel * 0.65f + (-dyOut / dtOut) * 0.35f;
            g_TS.lastMs = now;
            g_TS.lastY = io.MousePos.y;
        } else {
            // Dedo soltou (ou gesto ainda não era rolagem)
            if (g_TS.steal && g_TS.winId == child->ID && g_TS.vel != 0.0f)
                g_ScrollInertia[child->ID] = g_TS.vel;

            g_TS.decided = false;
            g_TS.steal = false;
            g_TS.vel = 0.0f;
        }"""
),
]

# ═══════════════════════════════════════════════════════════════════
# Globals.hpp — ⑨ stealth de leitura (intervalo + jitter)
# ═══════════════════════════════════════════════════════════════════
R["app/src/main/cpp/Shared/Globals.hpp"] = [
(
"""            int MenuKey = 0;
            int ThreadDelay = 240;
            bool CaptureBypass = true;""",
"""            int MenuKey = 0;
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

            bool CaptureBypass = true;"""
),
]

# ═══════════════════════════════════════════════════════════════════
# Draw.cpp — ⑨ stealth (loop de leitura + loops de write) + throttle
# ═══════════════════════════════════════════════════════════════════
R["app/src/main/cpp/Client/Draw/Draw.cpp"] = [
# 1) sleep do ReadLoop
(
"""                std::this_thread::sleep_for( std::chrono::milliseconds( 1 ) );
        }
}

template void Data::ReadLoop<true, false>( );    // v24.1 32-bit""",
"""                /*
                 * STEALTH DE LEITURA — antes: 1ms (≈1000 varreduras/segundo,
                 * cada uma com ~20-25 round-trips de batch = volume gigante
                 * em cadência de metrônomo perfeito; era ISSO que o
                 * anti-cheat detectava em ~5 minutos). Agora: intervalo
                 * configurável (padrão 60ms ≈ 16Hz — ESP continua fluida)
                 * com jitter aleatório de ±20% quando StealthRead ligado,
                 * pra não formar padrão periódico.
                 */
                {
                        int readInterval = g_Globals.General.ReadIntervalMs;
                        if ( readInterval < 15 ) readInterval = 15;
                        if ( readInterval > 1000 ) readInterval = 1000;

                        if ( g_Globals.General.StealthRead )
                        {
                                static unsigned int s_JitterSeed = 12345u;
                                s_JitterSeed = s_JitterSeed * 1664525u + 1013904223u;
                                const int jitterPct = ( int )( ( s_JitterSeed >> 16 ) % 41u ) - 20;   // -20..+20
                                readInterval = readInterval * ( 100 + jitterPct ) / 100;
                        }

                        std::this_thread::sleep_for( std::chrono::milliseconds( readInterval ) );
                }
        }
}

template void Data::ReadLoop<true, false>( );    // v24.1 32-bit"""
),
# 2) magnet: 1µs -> 2ms
(
"""                                        while ( isHolding && lockedMatrixAddr )
                                        {
                                                g_FreeFireMemory.Write<Vector3>( lockedMatrixAddr + posWriteOffset, lockedRootPos );
                                                std::this_thread::sleep_for( std::chrono::microseconds( 1 ) );
                                        }""",
"""                                        while ( isHolding && lockedMatrixAddr )
                                        {
                                                g_FreeFireMemory.Write<Vector3>( lockedMatrixAddr + posWriteOffset, lockedRootPos );
                                                /*
                                                 * STEALTH DE ESCRITA: 1 microssegundo = até
                                                 * ~1 MILHÃO de writes/segundo agora que a
                                                 * ponte de write funciona — detecção
                                                 * instantânea. 2ms (500/s) segura o magnet
                                                 * com volume ~2000x menor.
                                                 */
                                                std::this_thread::sleep_for( std::chrono::milliseconds( 2 ) );
                                        }"""
),
# 3) rage flood: 1µs -> 2ms
(
"""                                                        g_FreeFireMemory.Write( localPlayer + Offsets::Player::m_AimRotation, playerLook );
                                                        std::this_thread::sleep_for( std::chrono::microseconds( 1 ) );""",
"""                                                        g_FreeFireMemory.Write( localPlayer + Offsets::Player::m_AimRotation, playerLook );
                                                        /* STEALTH DE ESCRITA: 1µs virou 2ms (ver magnet) */
                                                        std::this_thread::sleep_for( std::chrono::milliseconds( 2 ) );"""
),
# 4a) cabeçalho do throttle + SpinBot fora
(
"""        // ==================== Weapon Exploits ====================

        uintptr_t m_InventoryManager = ReadPtr( localPlayer + Offsets::Player::m_InventoryManager );""",
"""        // ==================== Weapon Exploits ====================

        /*
         * THROTTLE + STEALTH: este bloco lê e escreve na memória do jogo.
         * Rodando TODO frame (até 240 fps) gerava centenas de operações
         * por segundo em cadência de metrônomo — assinatura fácil de
         * detectar. Agora roda a cada 50ms (20 Hz), mais que suficiente
         * para NoRecoil/MedKit/Precision/FireDelay/etc.
         * O SpinBot ficou FORA do throttle (efeito visual contínuo).
         */
        static LONGLONG s_LastExploitTick = 0;
        const LONGLONG nowExploitTick = GetTickCount64( );
        const bool exploitTick = ( nowExploitTick - s_LastExploitTick ) >= 50;
        if ( exploitTick )
                s_LastExploitTick = nowExploitTick;

        // --- SpinBot (por frame, fora do throttle) ---
        if ( g_Globals.Misc.Exploits.LocalPlayer.SpinBot && !IsObserving )
        {
                SpinBot( localPlayer, N32 );
        }

        if ( exploitTick )
        {

        uintptr_t m_InventoryManager = ReadPtr( localPlayer + Offsets::Player::m_InventoryManager );"""
),
# 4b) remove o SpinBot antigo de dentro do bloco
(
"""        // --- SpinBot ---
        if ( g_Globals.Misc.Exploits.LocalPlayer.SpinBot && !IsObserving )
        {
                SpinBot( localPlayer, N32 );
        }

        // --- SocoLonge ---""",
"""        // --- SocoLonge ---"""
),
# 4c) fecha o throttle antes do catch
(
"""                        s_awmOriginals.clear( );
                }
        }
        }
        catch ( const std::exception& ex )""",
"""                        s_awmOriginals.clear( );
                }
        }

        } // if ( exploitTick )

        }
        catch ( const std::exception& ex )"""
),
]

# ═══════════════════════════════════════════════════════════════════
# Interface.cpp — ⑤ V31 sempre máximo ⑥ animação Unload ⑦ posição
#                 inicial ⑧ ícones maiores + controles de stealth
# ═══════════════════════════════════════════════════════════════════
R["app/src/main/cpp/Client/Interface/Interface.cpp"] = [
# J11 dock maior
(
"""        const float dockHeight = 56.0f;
        const float dockPadding = 12.0f;
        const float itemSize = 44.0f;
        const float itemSpacing = 14.0f;""",
"""        const float dockHeight = 62.0f;
        const float dockPadding = 12.0f;
        const float itemSize = 50.0f;    // ícones maiores (era 44)
        const float itemSpacing = 14.0f;"""
),
# J5 statics da animação de minimizar
(
"""static bool g_WantShutdown = false;
static float g_ShutdownProgress = 0.0f;
static float g_ShutdownScale = 1.0f;
static float g_ShutdownAlpha = 1.0f;
static float g_ShutdownRotation = 0.0f;""",
"""static bool g_WantShutdown = false;
static float g_ShutdownProgress = 0.0f;
static float g_ShutdownScale = 1.0f;
static float g_ShutdownAlpha = 1.0f;
static float g_ShutdownRotation = 0.0f;

/*
 * Animação de MINIMIZAR — replica exatamente o Unload:
 * encolhe (1.0 -> 0.7), sobe 30px e some com easeOut quadrático.
 */
static bool g_Minimizing = false;
static float g_MinimizeProgress = 0.0f;"""
),
# J6a FAB: maior + só aparece depois da animação
(
"""        static ImVec2 s_FabPos = ImVec2(-1, -1);
        static bool s_FabDragging = false;
        static bool s_FabWasDrag = false;
        static ImVec2 s_FabGrab(0, 0);
        static ImVec2 s_FabClickPos(0, 0);
        const float kFabSize = 58.0f;

        if (s_FabPos.x < 0)
                s_FabPos = ImVec2(18.0f, io.DisplaySize.y * 0.45f);

        if (!bIsMenuOpen && !g_WantShutdown)
        {""",
"""        static ImVec2 s_FabPos = ImVec2(-1, -1);
        static bool s_FabDragging = false;
        static bool s_FabWasDrag = false;
        static ImVec2 s_FabGrab(0, 0);
        static ImVec2 s_FabClickPos(0, 0);
        const float kFabSize = 72.0f;    // ícone maior (era 58)

        if (s_FabPos.x < 0)
                s_FabPos = ImVec2(18.0f, io.DisplaySize.y * 0.45f);

        /*
         * O FAB só aparece DEPOIS que a animação de minimizar termina —
         * durante a animação o painel ainda está desenhando (Unload).
         */
        const bool fabVisible = !bIsMenuOpen && !g_WantShutdown && !g_Minimizing;

        if (fabVisible)
        {"""
),
# J6b reabrir: volta posição inicial (painel + ícone)
(
"""                else if (s_FabDragging && !ImGui::IsMouseDown(0))
                {
                        s_FabDragging = false;
                        if (!s_FabWasDrag)
                                bIsMenuOpen = true;   // toque simples = reabrir
                }""",
"""                else if (s_FabDragging && !ImGui::IsMouseDown(0))
                {
                        s_FabDragging = false;
                        if (!s_FabWasDrag)
                        {
                                bIsMenuOpen = true;   // toque simples = reabrir

                                /*
                                 * VOLTA PRA POSIÇÃO INICIAL: o painel volta
                                 * pro centro (igual à primeira abertura) e o
                                 * ícone (FAB) também volta ao canto inicial.
                                 */
                                {
                                        const float maxW = io.DisplaySize.x * 0.95f;
                                        const float maxH = io.DisplaySize.y * 0.85f;
                                        const ImVec2 ws(ImMin(700.0f, maxW), ImMin(460.0f, maxH));

                                        g_WindowPos = ImVec2(
                                                (io.DisplaySize.x - ws.x) * 0.5f,
                                                (io.DisplaySize.y - ws.y) * 0.5f);
                                }

                                s_FabPos = ImVec2(18.0f, io.DisplaySize.y * 0.45f);
                        }
                }"""
),
# J7 animação de minimizar = curva do Unload
(
"""        float targetScale = bIsMenuOpen ? 1.0f : 0.95f;
        float targetAlpha = bIsMenuOpen ? 1.0f : 0.0f;

        float scaleSpeed = bIsMenuOpen ? 12.0f : 25.0f;
        float alphaSpeed = bIsMenuOpen ? 10.0f : 30.0f;

        g_WindowScale = smoothLerp(g_WindowScale, targetScale, scaleSpeed, dt);
        g_WindowAlpha = smoothLerp(g_WindowAlpha, targetAlpha, alphaSpeed, dt);
        g_ContentAlpha = smoothLerp(g_ContentAlpha, targetAlpha, alphaSpeed, dt);

        if (g_WantShutdown)
        {
                g_WindowScale *= g_ShutdownScale;
                g_WindowAlpha *= g_ShutdownAlpha;
                g_ContentAlpha *= g_ShutdownAlpha;
        }

        if (g_WindowAlpha < 0.01f && !g_WantShutdown) return;""",
"""        /*
         * ANIMAÇÃO DE MINIMIZAR = MESMA DO UNLOAD:
         * mesma curva easeOut do g_WantShutdown (progress += dt*3.5,
         * escala 1.0 -> 0.7, alpha 1.0 -> 0.0, sobe 30px).
         */
        if (g_Minimizing)
        {
                g_MinimizeProgress += dt * 3.5f;
                float t = g_MinimizeProgress;
                float easeOut = 1.0f - (1.0f - t) * (1.0f - t);

                g_WindowScale = 1.0f - easeOut * 0.3f;
                g_WindowAlpha = 1.0f - easeOut;
                g_ContentAlpha = g_WindowAlpha;

                if (g_MinimizeProgress >= 1.0f)
                {
                        g_MinimizeProgress = 0.0f;
                        g_Minimizing = false;
                }
        }
        else if (bIsMenuOpen)
        {
                g_WindowScale = smoothLerp(g_WindowScale, 1.0f, 12.0f, dt);
                g_WindowAlpha = smoothLerp(g_WindowAlpha, 1.0f, 10.0f, dt);
                g_ContentAlpha = smoothLerp(g_ContentAlpha, 1.0f, 10.0f, dt);
        }

        if (g_WantShutdown)
        {
                g_WindowScale *= g_ShutdownScale;
                g_WindowAlpha *= g_ShutdownAlpha;
                g_ContentAlpha *= g_ShutdownAlpha;
        }

        if (g_WindowAlpha < 0.01f && !g_WantShutdown && !g_Minimizing) return;"""
),
# J8 sobe 30px durante a minimização (igual Unload)
(
"""        if (g_WantShutdown)
        {
                float moveUp = g_ShutdownProgress * 30.0f;
                scaledPos.y -= moveUp;
        }""",
"""        if (g_WantShutdown)
        {
                float moveUp = g_ShutdownProgress * 30.0f;
                scaledPos.y -= moveUp;
        }
        else if (g_Minimizing)
        {
                // Mesmo movimento do Unload: sobe enquanto some.
                scaledPos.y -= g_MinimizeProgress * 30.0f;
        }"""
),
# J12a botão de minimizar maior
(
"""                                const float btnSize = 30.0f;""",
"""                                const float btnSize = 36.0f;    // ícone maior (era 30)"""
),
# J12b traço do menos maior
(
"""                                DrawList->AddLine(
                                        ImVec2(btnC.x - 7.0f, btnC.y),
                                        ImVec2(btnC.x + 7.0f, btnC.y),
                                        IM_COL32(235, 235, 240, 245), 2.2f);""",
"""                                DrawList->AddLine(
                                        ImVec2(btnC.x - 8.5f, btnC.y),
                                        ImVec2(btnC.x + 8.5f, btnC.y),
                                        IM_COL32(235, 235, 240, 245), 2.4f);"""
),
# J4 clicar em minimizar dispara a animação
(
"""                                if (btnHover && ImGui::IsMouseClicked(0))
                                {
                                        bIsMenuOpen = false;
                                        NotifyManager::Send(XorStr("Painel minimizado — toque no FAB pra reabrir"), 2500);
                                }""",
"""                                if (btnHover && ImGui::IsMouseClicked(0))
                                {
                                        bIsMenuOpen = false;
                                        g_Minimizing = true;          // mesma animação do Unload
                                        g_MinimizeProgress = 0.0f;
                                        NotifyManager::Send(XorStr("Painel minimizado — toque no FAB pra reabrir"), 2500);
                                }"""
),
# J1 AimKey/PullKey + máximo de funções (sem gate !V31)
(
"""                                                        Custom::Checkbox(XorStr("Aimbot"), &g_Globals.AimBot.Enabled);
                                                        Custom::KeyBind(XorStr("AimKey"), &g_Globals.AimBot.KeyBind);
                                                        if (!g_Globals.General.V31)
                                                        {
                                                                Custom::Checkbox(XorStr("Pull Player"), &g_Globals.AimBot.aimmagnect);
                                                                Custom::KeyBind(XorStr("PullKey"), &g_Globals.AimBot.MagKey);
                                                        }""",
"""                                                        Custom::Checkbox(XorStr("Aimbot"), &g_Globals.AimBot.Enabled);
                                                        Custom::KeyBind(XorStr("AimKey"), &g_Globals.AimBot.KeyBind, false, &g_Globals.AimBot.Enabled);

                                                        /*
                                                         * MÁXIMO DE FUNÇÕES SEMPRE: "Pull Player" antes
                                                         * só aparecia com !V31 — mas o Memory::Initialize
                                                         * FORÇA V31=true no perfil 32-bit, então nunca
                                                         * aparecia. Agora aparece sempre.
                                                         */
                                                        Custom::Checkbox(XorStr("Pull Player"), &g_Globals.AimBot.aimmagnect);
                                                        Custom::KeyBind(XorStr("PullKey"), &g_Globals.AimBot.MagKey, false, &g_Globals.AimBot.aimmagnect);"""
),
# J2 Spin Bot sem gate !V31
(
"""                                                        if (!g_Globals.General.V31)
                                                        {
                                                                Custom::Checkbox(XorStr("Spin Bot"), &g_Globals.Misc.Exploits.LocalPlayer.SpinBot);
                                                                if (g_Globals.Misc.Exploits.LocalPlayer.SpinBot)
                                                                {
                                                                        Custom::SliderFloat(XorStr("Spin Speed"), &g_Globals.Misc.Exploits.LocalPlayer.SpinSpeed, 1.0f, 5.0f, "%.1f");
                                                                }
                                                        }""",
"""                                                        /*
                                                         * MÁXIMO DE FUNÇÕES SEMPRE: Spin Bot aparecia
                                                         * só com !V31 (que nunca acontecia de verdade).
                                                         * Agora sempre visível, em qualquer Game Type.
                                                         */
                                                        Custom::Checkbox(XorStr("Spin Bot"), &g_Globals.Misc.Exploits.LocalPlayer.SpinBot);
                                                        if (g_Globals.Misc.Exploits.LocalPlayer.SpinBot)
                                                        {
                                                                Custom::SliderFloat(XorStr("Spin Speed"), &g_Globals.Misc.Exploits.LocalPlayer.SpinSpeed, 1.0f, 5.0f, "%.1f");
                                                        }"""
),
# J3a SilentKey com sync
(
"""                                                        Custom::KeyBind(XorStr("SilentKey"), &g_Globals.Silent.KeyBind);""",
"""                                                        Custom::KeyBind(XorStr("SilentKey"), &g_Globals.Silent.KeyBind, false, &g_Globals.Silent.Enabled);"""
),
# J3b GhostKey com sync
(
"""                                                        Custom::Checkbox(XorStr("Ghost"), &g_Globals.AimBot.ghost);
                                                        Custom::KeyBind(XorStr("GhostKey"), &g_Globals.AimBot.ghostkey);""",
"""                                                        Custom::Checkbox(XorStr("Ghost"), &g_Globals.AimBot.ghost);
                                                        Custom::KeyBind(XorStr("GhostKey"), &g_Globals.AimBot.ghostkey, false, &g_Globals.AimBot.ghost);"""
),
# J10 controles de stealth na aba Config
(
"""                                                        Custom::Checkbox(XorStr("Stream Mode"), &g_Globals.General.CaptureBypass);
                                                        Custom::SliderInt(XorStr("Frame Rate"), &g_Globals.General.ThreadDelay, 30, 240, "%dFPS");

                                                        ImGui::Dummy(ImVec2(0, 8));""",
"""                                                        Custom::Checkbox(XorStr("Stream Mode"), &g_Globals.General.CaptureBypass);
                                                        Custom::SliderInt(XorStr("Frame Rate"), &g_Globals.General.ThreadDelay, 30, 240, "%dFPS");

                                                        ImGui::Dummy(ImVec2(0, 8));

                                                        /*
                                                         * STEALTH DE LEITURA — o volume de reads era o
                                                         * que fazia o jogo detectar (~5 minutos).
                                                         * Intervalo entre varreduras do ESP (padrão
                                                         * 60ms ≈ 16Hz) + jitter aleatório pra quebrar
                                                         * a cadência. Valor maior = mais discreto.
                                                         */
                                                        Custom::SliderInt(XorStr("Read Interval"), &g_Globals.General.ReadIntervalMs, 15, 300, "%d ms");
                                                        Custom::Checkbox(XorStr("Stealth (jitter)"), &g_Globals.General.StealthRead);

                                                        ImGui::Dummy(ImVec2(0, 8));"""
),
# J9 tecla de menu (volume) usa a mesma regra
(
"""void Interface::HandleMenuKey()
{
    if (AndroidInput::IsKeyPressed(0x2F))
    {
        if (!MenuKeyDown)
        {
            MenuKeyDown = true;
            bIsMenuOpen = !bIsMenuOpen;
        }
    }
    else
    {
        MenuKeyDown = false;
    }
}""",
"""void Interface::HandleMenuKey()
{
    if (AndroidInput::IsKeyPressed(0x2F))
    {
        if (!MenuKeyDown)
        {
            MenuKeyDown = true;
            bIsMenuOpen = !bIsMenuOpen;

            /*
             * Mesma regra do FAB: fechar = animação do Unload;
             * abrir = painel de volta à posição inicial.
             */
            if (!bIsMenuOpen)
            {
                g_Minimizing = true;
                g_MinimizeProgress = 0.0f;
            }
            else
            {
                ImGuiIO& io = ImGui::GetIO();
                const float maxW = io.DisplaySize.x * 0.95f;
                const float maxH = io.DisplaySize.y * 0.85f;
                const ImVec2 ws(ImMin(700.0f, maxW), ImMin(460.0f, maxH));

                g_WindowPos = ImVec2(
                        (io.DisplaySize.x - ws.x) * 0.5f,
                        (io.DisplaySize.y - ws.y) * 0.5f);
            }
        }
    }
    else
    {
        MenuKeyDown = false;
    }
}"""
),
]

# ═══════════════════════════════════════════════════════════════════
# saveconfig.cpp — persistir ReadIntervalMs / StealthRead
# ═══════════════════════════════════════════════════════════════════
R["app/src/main/cpp/Client/Config/saveconfig.cpp"] = [
(
"""            wInt(ofs, "GameProfile", g_Globals.General.GameProfile);   // << NOVA
            ofs << "\\n";""",
"""            wInt(ofs, "GameProfile", g_Globals.General.GameProfile);   // << NOVA
            wInt(ofs, "ReadIntervalMs", g_Globals.General.ReadIntervalMs);
            wBool(ofs, "StealthRead", g_Globals.General.StealthRead);
            ofs << "\\n";"""
),
(
"""            gInt("General", "GameProfile", g_Globals.General.GameProfile);   // << NOVA


            return true;""",
"""            gInt("General", "GameProfile", g_Globals.General.GameProfile);   // << NOVA
            gInt("General", "ReadIntervalMs", g_Globals.General.ReadIntervalMs);
            gBool("General", "StealthRead", g_Globals.General.StealthRead);


            return true;"""
),
]


def main():
    ok = True
    for rel, edits in R.items():
        src = os.path.join(REPO, rel)
        dst = os.path.join(OUT, rel)
        os.makedirs(os.path.dirname(dst), exist_ok=True)

        with open(src, "r", encoding="utf-8") as f:
            content = f.read()

        for i, (old, new) in enumerate(edits):
            n = content.count(old)
            if n != 1:
                print(f"[ERRO] {rel} :: patch {i+1} ocorre {n}x (esperado 1x)")
                ok = False
                continue
            content = content.replace(old, new)
            print(f"[ok] {rel} :: patch {i+1}")

        if ok:
            with open(dst, "w", encoding="utf-8") as f:
                f.write(content)
            print(f"     -> {dst}  ({content.count(chr(10))+1} linhas)")

    if not ok:
        sys.exit(1)
    print("\\nTodos os patches aplicados com sucesso.")


if __name__ == "__main__":
    main()
