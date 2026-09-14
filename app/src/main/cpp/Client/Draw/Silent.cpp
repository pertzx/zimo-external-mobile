#include "Silent.hpp"

#include "Draw.hpp"

#include <Globals.hpp>
#include <Memory/Memory.hpp>
#include <Offsets/Offsets.hpp>

#include <pthread.h>
#include <sched.h>

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>

#include <Memory/BridgeClient.hpp>
#include <Shared/Bridge/BridgeProtocol.hpp>

#include <android/log.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/un.h>
#include <unistd.h>

namespace Silent
{
    static volatile LONGLONG g_LocalPlayer = 0;
    static volatile LONGLONG g_TargetEntity = 0;

    volatile LONG g_Running = 0;

    /*
     * 2a fonte do estado de tiro (publicada pelo Draw via NotifyFiring):
     * o Draw ja le IsFiring pro aimbot; publicado aqui custa zero na
     * ponte e cobre falha do read proprio do writer no pico do combate.
     */
    static volatile LONG g_ExtFiring = 0;
    static volatile LONGLONG g_ExtFiringTick = 0;

    static CRITICAL_SECTION g_MatrixCS;
    static Matrix4x4 g_ViewMatrix = {};

    /*
     * Ultima cabeca boa por alvo (vinda do snapshot da ESP). Serve de
     * ponte quando o alvo saiu momentaneamente da lista (snapshot
     * engasgou) — nunca deixa o silent mirar em dado de OUTRO alvo.
     */
    static CRITICAL_SECTION g_HeadCS;
    static Vector3 s_CachedHead = {};
    static LONGLONG s_CachedHeadTick = 0;
    static uintptr_t s_CachedHeadTarget = 0;

    static bool g_CSInitialized = false;

    /* ====================================================================
     * CANAL DEDICADO DO SILENT (2a conexao com o daemon)
     * ====================================================================
     * O daemon aceita VARIAS conexoes simultaneas (1 thread por conexao,
     * ver no accept()) e o protocolo e stateless (cada pedido carrega o
     * pid). O canal UNICO compartilhado com a ESP era o gargalo real:
     * o ReadLoop da ESP enchia o mutex do socket e o write do silent
     * ficava parado na fila — no meio do combate o ritmo real de write
     * desabava (muito abaixo dos ~330/s nominais), o RayDir do jogo
     * ganhava a bala e o silent voltava a "demorar".
     *
     * Agora o silent tem socket PROPRIA: write/read dele nao disputam
     * mutex com a ESP (nem cliente, nem no daemon), e a ESP nunca mais
     * espera o silent. Falha de socket = fecha; o proximo pedido
     * reconecta (rate-limit de 750ms no SilConnect).
     * ====================================================================
     */
    static int g_SilSock = -1;
    static uint32_t g_SilSeq = 0;
    static LONGLONG g_SilNextConnectMs = 0;

    static void SilCloseSock()
    {
        if (g_SilSock >= 0)
        {
            close(g_SilSock);

            g_SilSock = -1;
        }
    }

    static bool SilSendAll(
        int fd,
        const void* buffer,
        size_t size
    )
    {
        const char* p =
            static_cast<const char*>(buffer);

        size_t total = 0;

        while (total < size)
        {
            const ssize_t n =
                send(
                    fd,
                    p + total,
                    size - total,
                    MSG_NOSIGNAL
                );

            if (n < 0)
            {
                if (errno == EINTR)
                    continue;

                return false;
            }

            if (n == 0)
                return false;

            total +=
                static_cast<size_t>(n);
        }

        return true;
    }

    static bool SilRecvAll(
        int fd,
        void* buffer,
        size_t size
    )
    {
        char* p =
            static_cast<char*>(buffer);

        size_t total = 0;

        while (total < size)
        {
            const ssize_t n =
                recv(
                    fd,
                    p + total,
                    size - total,
                    0
                );

            if (n < 0)
            {
                if (errno == EINTR)
                    continue;

                return false;
            }

            if (n == 0)
                return false;

            total +=
                static_cast<size_t>(n);
        }

        return true;
    }

    static bool SilConnect()
    {
        const LONGLONG nowMs =
            static_cast<LONGLONG>(GetTickCount64());

        if (nowMs < g_SilNextConnectMs)
            return false;

        g_SilNextConnectMs =
            nowMs + 750;

        SilCloseSock();

        const int fd =
            socket(AF_UNIX, SOCK_STREAM, 0);

        if (fd < 0)
            return false;

        sockaddr_un addr{};

        addr.sun_family = AF_UNIX;

        strncpy(
            addr.sun_path,
            BridgeClient::GetSocketPath(),
            sizeof(addr.sun_path) - 1
        );

        if (connect(
                fd,
                reinterpret_cast<sockaddr*>(&addr),
                sizeof(addr)
            ) < 0)
        {
            close(fd);

            /*
             * Socket sumiu/velho: invalida o caminho resolvido pra o
             * proximo connect re-ler o stormbridge.path (daemon pode
             * ter respawnado com outro nome aleatorio).
             */
            BridgeClient::InvalidateSocketPath();

            return false;
        }

        timeval tv{};

        tv.tv_sec  = 1;
        tv.tv_usec = 0;

        setsockopt(
            fd,
            SOL_SOCKET,
            SO_RCVTIMEO,
            &tv,
            sizeof(tv)
        );

        setsockopt(
            fd,
            SOL_SOCKET,
            SO_SNDTIMEO,
            &tv,
            sizeof(tv)
        );

        g_SilSock = fd;

        return true;
    }

    /* Read cru pela conexao dedicada (mesmo formato do BridgeClient). */
    static bool SilReadMem(
        uint32_t pid,
        uint64_t address,
        void* buffer,
        uint32_t size
    )
    {
        if (g_SilSock < 0 && !SilConnect())
            return false;

        BridgeRequest req{};

        req.Magic   = BRIDGE_MAGIC;
        req.Version = BRIDGE_PROTO_VERSION;
        req.Cmd     = BRIDGE_CMD_READ;
        req.Seq     = ++g_SilSeq;
        req.Pid     = pid;
        req.Address = address;
        req.Size    = size;

        BridgeResponse resp{};

        if (!SilSendAll(g_SilSock, &req, sizeof(req)) ||
            !SilRecvAll(g_SilSock, &resp, sizeof(resp)) ||
            resp.Magic != BRIDGE_MAGIC ||
            resp.Seq != req.Seq ||
            resp.Status != BRIDGE_OK ||
            resp.PayloadSize != size)
        {
            SilCloseSock();

            return false;
        }

        if (size > 0 &&
            !SilRecvAll(g_SilSock, buffer, size))
        {
            SilCloseSock();

            return false;
        }

        return true;
    }

    /* Write cru pela conexao dedicada. */
    static bool SilWriteMem(
        uint32_t pid,
        uint64_t address,
        const void* buffer,
        uint32_t size
    )
    {
        if (g_SilSock < 0 && !SilConnect())
            return false;

        BridgeRequest req{};

        req.Magic       = BRIDGE_MAGIC;
        req.Version     = BRIDGE_PROTO_VERSION;
        req.Cmd         = BRIDGE_CMD_WRITE;
        req.Seq         = ++g_SilSeq;
        req.Pid         = pid;
        req.PayloadSize = size;
        req.Address     = address;
        req.Size        = size;

        BridgeResponse resp{};

        if (!SilSendAll(g_SilSock, &req, sizeof(req)) ||
            !SilSendAll(g_SilSock, buffer, size) ||
            !SilRecvAll(g_SilSock, &resp, sizeof(resp)) ||
            resp.Magic != BRIDGE_MAGIC ||
            resp.Seq != req.Seq ||
            resp.Status != BRIDGE_OK)
        {
            SilCloseSock();

            return false;
        }

        return true;
    }

    /* Conveniencia de leitura por valor (igual Memory::Read<T>). */
    template<typename T>
    static T SilReadVal(
        uint64_t address
    )
    {
        T out{};

        SilReadMem(
            static_cast<uint32_t>(
                Memory::GetTargetPid()
            ),
            address,
            &out,
            static_cast<uint32_t>(sizeof(T))
        );

        return out;
    }

    static void EnsureCSInit()
    {
        if (!g_CSInitialized)
        {
            InitializeCriticalSection(&g_MatrixCS);
            InitializeCriticalSection(&g_HeadCS);
            g_CSInitialized = true;
        }
    }

    static uint32_t NextRandom(
        uint32_t& state
    )
    {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return state;
    }

    /*
     * Player::IsFiring (0x540 nos perfis v7a) — fallback do writer, com
     * throttle do chamador (FIRE_FALLBACK_MS). Read<T> devolve zero
     * quando a ponte falha: retry + ultimo valor bom por 500ms absorvem
     * o falso negativo no meio do spray.
     */
    static bool ReadIsFiring(
        uintptr_t localPlayer
    )
    {
        static LONGLONG s_LastGoodTickMs = 0;
        static bool s_LastGoodValue = false;

        if (
            localPlayer == 0 ||
            Offsets::Player::IsFiring == 0
        )
        {
            return false;
        }

        for (int attempt = 0; attempt < 2; ++attempt)
        {
            uint8_t value = 0;

            if (SilReadMem(
                    static_cast<uint32_t>(
                        Memory::GetTargetPid()
                    ),
                    localPlayer +
                        Offsets::Player::IsFiring,
                    &value,
                    1
                ))
            {
                s_LastGoodValue = (value != 0);
                s_LastGoodTickMs =
                    static_cast<LONGLONG>(GetTickCount64());

                return s_LastGoodValue;
            }

            Sleep(2);
        }

        return s_LastGoodValue &&
               (static_cast<LONGLONG>(GetTickCount64()) -
                   s_LastGoodTickMs) < 500;
    }

    /*
     * Busca a cabeca do alvo NO SNAPSHOT DA ESP (memoria do cliente,
     * ZERO roundtrip na ponte). O ReadLoop da ESP ja anda na cadeia de
     * ossos de todo player e guarda HeadWorld (mundo) + LastSeenTick.
     *
     * HeadWorld do ESP = GetHeadPosition + Up*0.20 (convencao visual).
     * O ponto de mira comprovado do silent era GetHeadPosition+0.05 —
     * por isso os 0.15 voltam aqui.
     *
     * Achou: devolve true e atualiza o cache do alvo.
     * Nao achou: devolve false (o caller usa o cache, se estiver fresco).
     */
    static bool LookupHeadFromSnapshot(
        uintptr_t target,
        Vector3& outHead,
        LONGLONG& outSeenTick
    )
    {
        std::lock_guard<std::mutex> lock(
            Data::GetMutex()
        );

        const std::vector<PlayerData>& players =
            Data::GetPlayers();

        for (size_t i = 0; i < players.size(); ++i)
        {
            if (players[i].Entity != target)
                continue;

            const Vector3& hw =
                players[i].HeadWorld;

            if (
                hw.X == 0.0f &&
                hw.Y == 0.0f &&
                hw.Z == 0.0f
            )
            {
                return false;
            }

            outHead = hw;
            outHead.Y -= 0.15f;
            outSeenTick =
                players[i].LastSeenTick;

            EnterCriticalSection(&g_HeadCS);
            s_CachedHead = outHead;
            s_CachedHeadTick =
                static_cast<LONGLONG>(
                    GetTickCount64()
                );
            s_CachedHeadTarget = target;
            LeaveCriticalSection(&g_HeadCS);

            return true;
        }

        return false;
    }

    /* Sobri a thread escritora uma unica vez (lazy-start, idempotente). */
    static void* WriteThread(
        void*
    );

    static void EnsureStarted()
    {
        EnsureCSInit();

        if (
            InterlockedCompareExchange(
                &g_Running,
                1,
                0
            ) != 0
        )
        {
            return; /* ja rodando */
        }

        pthread_t thread{};

        if (
            pthread_create(
                &thread,
                nullptr,
                WriteThread,
                nullptr
            ) != 0
        )
        {
            InterlockedExchange(
                &g_Running,
                0
            );

            return;
        }

        pthread_detach(thread);
    }

    /* ====================================================================
     * THREAD UNICA — ESCRITORA
     *
     * NAO anda na cadeia de ossos (a ESP ja leu). Le so:
     *   - weaponPtr: 1 read a cada 250ms (troca de arma/reload)
     *   - StartPosition: 1 read a cada 16ms (origem do tiro)
     *   - IsFiring: 1 read a cada 100ms, SO se a UI nao publicar
     * Total: dezenas de ops/s. Todo o resto e copia local (secao
     * critica / snapshot da ESP) — a ESP divide a ponte com um silent
     * dez vezes mais leve, e para de perder update.
     * ====================================================================
     */
    static void* WriteThread(
        void*
    )
    {
        const auto now =
            std::chrono::steady_clock::now();

        uint32_t rngState =
            static_cast<uint32_t>(
                now.time_since_epoch().count()
            );

        if (!rngState)
            rngState = 0xDEADBEEF;

        uint32_t writeFails = 0;
        uint32_t writeCount = 0;

        LONGLONG lastHeadMs = 0;

        LONGLONG lastMatrixMs = 0;
        LONGLONG lastWeaponMs = 0;
        LONGLONG lastOriginMs = 0;
        LONGLONG lastFireMs = 0;

        uintptr_t weaponPtr = 0;

        Vector3 origin = {};

        /*
         * Cabeca em cache do burst (refresh por tempo): entre um refresh
         * e outro o laco so escreve — zero mutex, zero socket.
         */
        Vector3 burstHead = {};
        bool burstHeadOk = false;

        /* Modo do ciclo anterior (define o throttle do refresh da cabeca). */
        bool burstFiring = false;

        Matrix4x4 cachedMatrix = {};

        while (
            InterlockedCompareExchange(
                &g_Running,
                0,
                0
            ) != 0 &&
            !g_Globals.General.ShutDown
        )
        {
            if (!g_Globals.Silent.Enabled)
            {
                Sleep(3);
                continue;
            }

            const uintptr_t localPlayer =
                static_cast<uintptr_t>(
                    InterlockedCompareExchange64(
                        &g_LocalPlayer,
                        0,
                        0
                    )
                );

            const uintptr_t target =
                static_cast<uintptr_t>(
                    InterlockedCompareExchange64(
                        &g_TargetEntity,
                        0,
                        0
                    )
                );

            if (
                target == 0 ||
                localPlayer == 0
            )
            {
                Sleep(4);
                continue;
            }

            const LONGLONG nowMs =
                static_cast<LONGLONG>(GetTickCount64());

            /*
             * CABECA — do snapshot da ESP (zero socket). Alvo novo =
             * cache zerado (nunca mira no dado do alvo anterior) e
             * PREDICAO zerada (velocidade do alvo anterior nunca
             * contamina o proximo).
             */
            static uintptr_t s_HeadForTarget = 0;

            /* Estado da predicacao (por alvo, resetado na troca). */
            static Vector3 s_ObsHead = {};
            static LONGLONG s_ObsTick = 0;
            static bool s_ObsValid = false;
            static Vector3 s_Vel = {};

            if (s_HeadForTarget != target)
            {
                s_HeadForTarget = target;

                EnterCriticalSection(&g_HeadCS);
                s_CachedHead = {};
                s_CachedHeadTick = 0;
                s_CachedHeadTarget = 0;
                LeaveCriticalSection(&g_HeadCS);

                burstHead = {};
                burstHeadOk = false;

                s_ObsHead = {};
                s_ObsTick = 0;
                s_ObsValid = false;
                s_Vel = {};
            }

            /*
             * Refresh POR TEMPO em cima de cache (nunca por write): no
             * burst o laco roda milhares de vezes por segundo — martelar
             * o mutex do snapshot a cada ciclo disputaria com o
             * ReadLoop/Draw. Entre um refresh e outro o laco SO ESCREVE.
             */
            if (
                !burstHeadOk ||
                nowMs - lastHeadMs >=
                    (burstFiring ? HEAD_REFRESH_FIRING_MS
                                 : HEAD_REFRESH_IDLE_MS)
            )
            {
                lastHeadMs = nowMs;

                Vector3 head = {};
                LONGLONG headTick = 0;
                bool headOk =
                    LookupHeadFromSnapshot(
                        target,
                        head,
                        headTick
                    );

                if (!headOk)
                {
                    /*
                     * Alvo saiu da lista (snapshot engasgou): usa a
                     * ultima boa do MESMO alvo dentro de
                     * HEAD_STALE_MAX_MS.
                     */
                    EnterCriticalSection(&g_HeadCS);
                    head = s_CachedHead;
                    headTick = s_CachedHeadTick;
                    const uintptr_t cachedFor =
                        s_CachedHeadTarget;
                    LeaveCriticalSection(&g_HeadCS);

                    headOk =
                        cachedFor == target &&
                        headTick != 0 &&
                        nowMs - headTick <=
                            HEAD_STALE_MAX_MS &&
                        (head.X != 0.0f ||
                         head.Y != 0.0f ||
                         head.Z != 0.0f);
                }
                else
                {
                    /*
                     * Achou na lista: mesmo assim respeita LastSeenTick
                     * — entrada antiga na lista nao vale mais que o
                     * keep-alive do cache.
                     */
                    if (nowMs - headTick > HEAD_STALE_MAX_MS)
                        headOk = false;
                }

                burstHeadOk = headOk;

                if (headOk)
                {
                    burstHead = head;

                    /*
                     * PREDICAO — observa a velocidade real do alvo. So
                     * incorpora quando a observacao e NOVA (LastSeenTick
                     * do snapshot avancou): refresh do proprio laco
                     * devolvendo o MESMO snapshot nao e movimento.
                     */
                    if (s_ObsValid && headTick > s_ObsTick)
                    {
                        const float dt =
                            static_cast<float>(headTick - s_ObsTick) /
                            1000.0f;

                        if (dt >= 0.004f)
                        {
                            Vector3 v;

                            v.X = (head.X - s_ObsHead.X) / dt;
                            v.Y = (head.Y - s_ObsHead.Y) / dt;
                            v.Z = (head.Z - s_ObsHead.Z) / dt;

                            const float spd =
                                std::sqrt(
                                    v.X * v.X +
                                    v.Y * v.Y +
                                    v.Z * v.Z
                                );

                            if (spd <= PRED_MAX_SPEED_MPS)
                            {
                                /* EMA: filtra o ruido de amostragem da
                                 * ESP sem atrasar a resposta. */
                                s_Vel.X += (v.X - s_Vel.X) * PRED_EMA_ALPHA;
                                s_Vel.Y += (v.Y - s_Vel.Y) * PRED_EMA_ALPHA;
                                s_Vel.Z += (v.Z - s_Vel.Z) * PRED_EMA_ALPHA;
                            }
                            else
                            {
                                /* Teleporte/reutilizacao de ponteiro:
                                 * velocidade nao e confiavel, zera. */
                                s_Vel = {};
                            }
                        }

                        s_ObsHead = head;
                        s_ObsTick = headTick;
                    }
                    else if (!s_ObsValid)
                    {
                        s_ObsHead = head;
                        s_ObsTick = headTick;
                        s_ObsValid = true;
                    }
                }
            }

            if (!burstHeadOk)
            {
                Sleep(2);
                continue;
            }

            /*
             * WEAPONPTR — 1 read a cada 250ms (o jogo pode trocar o
             * objeto da arma: reload, troca, re-aim).
             */
            const bool N32 =
                g_Globals.General.N32;

            if (
                weaponPtr == 0 ||
                nowMs - lastWeaponMs >=
                    WEAPON_REFRESH_MS
            )
            {
                const uintptr_t wp =
                    N32
                        ? SilReadVal<uint32_t>(
                              localPlayer +
                              Offsets::Player::m_LastAimingInfoFromWeapon
                          )
                        : SilReadVal<uint64_t>(
                              localPlayer +
                              Offsets::Player::m_LastAimingInfoFromWeapon
                          );
                
                lastWeaponMs = nowMs;

                if (wp != weaponPtr)
                {
                    weaponPtr = wp;

                    /* arma mudou: origem re-lida ja */
                    lastOriginMs = 0;
                }
            }

            if (weaponPtr == 0)
            {
                Sleep(4);
                continue;
            }

            /*
             * ORIGEM (boca da arma) — 1 read a cada 16ms.
             */
            if (
                (
                    origin.X == 0.0f &&
                    origin.Y == 0.0f &&
                    origin.Z == 0.0f
                ) ||
                nowMs - lastOriginMs >=
                    ORIGIN_REFRESH_MS
            )
            {
                origin =
                    SilReadVal<Vector3>(
                        weaponPtr +
                        Offsets::HitObjectInfo::StartPosition
                    );

                lastOriginMs = nowMs;
            }

            if (
                origin.X == 0.0f &&
                origin.Y == 0.0f &&
                origin.Z == 0.0f
            )
            {
                Sleep(4);
                continue;
            }

            /*
             * ESTADO DE TIRO — publicado pela UI (NotifyFiring, 250ms de
             * frescor) OU read proprio a cada FIRE_FALLBACK_MS.
             */
            const bool extFiring =
                InterlockedCompareExchange(&g_ExtFiring, 0, 0) != 0 &&
                nowMs - InterlockedCompareExchange64(
                            &g_ExtFiringTick,
                            0,
                            0
                        ) < 250;

            bool firing = extFiring;

            if (
                !firing &&
                nowMs - lastFireMs >=
                    FIRE_FALLBACK_MS
            )
            {
                lastFireMs = nowMs;

                firing = ReadIsFiring(localPlayer);
            }

            /*
             * View matrix: copia LOCAL (secao critica, zero read na
             * ponte) — usada so pelo FovCheck do write quente.
             */
            if (
                nowMs - lastMatrixMs >=
                MATRIX_REFRESH_MS
            )
            {
                EnterCriticalSection(&g_MatrixCS);
                cachedMatrix = g_ViewMatrix;
                LeaveCriticalSection(&g_MatrixCS);

                lastMatrixMs = nowMs;
            }

            /*
             * PREDICAO — ponto de mira: cabeca observada + velocidade
             * * (idade da observacao + latencia do write). Envelheceu
             * demais (ESP engasgada) = nao extrapola alem do teto, o
             * tiro cai no alvo observado em vez de voar pro nada.
             */
            LONGLONG predAgeMs = nowMs - s_ObsTick;

            if (predAgeMs < 0)
                predAgeMs = 0;

            if (predAgeMs > PRED_MAX_AGE_MS)
                predAgeMs = PRED_MAX_AGE_MS;

            const float predSec =
                static_cast<float>(predAgeMs + PRED_LATENCY_MS) / 1000.0f;

            Vector3 aimHead;

            aimHead.X = burstHead.X + s_Vel.X * predSec;
            aimHead.Y = burstHead.Y + s_Vel.Y * predSec;
            aimHead.Z = burstHead.Z + s_Vel.Z * predSec;

            /*
             * FovCheck (em PIXELS) so limita o write quente FORA do
             * tiro. ATIRANDO nao passa pelo FOV — quem validou o alvo
             * foi a selecao do Draw (Silent.Fov + MaxDistance), e o
             * recoil arrasta a mira pra fora do circulo no meio do
             * spray: barrar o write por isso deixava o silent fraco.
             */
            if (
                !firing &&
                !W2S::FovCheck(
                    cachedMatrix,
                    aimHead,
                    static_cast<float>(
                        g_Globals.Silent.Fov
                    )
                )
            )
            {
                Sleep(5);
                continue;
            }

            Vector3 dir;

            dir.X = aimHead.X - origin.X;
            dir.Y = aimHead.Y - origin.Y;
            dir.Z = aimHead.Z - origin.Z;

            if (
                dir.X == 0.0f &&
                dir.Y == 0.0f &&
                dir.Z == 0.0f
            )
            {
                Sleep(2);
                continue;
            }

            /*
             * ====================================================================
             * FORCA + HIT CHANCE (config do painel) + erro humano.
             * ====================================================================
             * 1) FORCA (0-100): escala o jitter. Quanto maior, menos o
             *    ray desvia da cabeca (100 = linha reta). O padrao 65
             *    ja sai mais forte que o jitter fixo antigo.
             *
             * 2) HIT CHANCE (0-100%): cada write rola o dado. Write de
             *    "erro" desloca o ray em 1.0-3.5% do proprio comprimento
             *    (erro humano que erra de verdade em qualquer distancia).
             *    Como o jogo consome o ULTIMO write antes do disparo, a
             *    chance da bala acertar ≈ HitChance. A DENSIDADE de
             *    write nao muda (stealth do ritmo intacto) e o servidor
             *    enxerga estatistica de jogador, nao de robo.
             * ====================================================================
             */

            int forcaCfg = g_Globals.Silent.Forca;

            if (forcaCfg < 0)
                forcaCfg = 0;

            if (forcaCfg > 100)
                forcaCfg = 100;

            const float forceT =
                static_cast<float>(forcaCfg) / 100.0f;

            /* Comprimento do ray (para o erro humano ser em % da distancia). */
            const float dirLen =
                sqrtf(
                    dir.X * dir.X +
                    dir.Y * dir.Y +
                    dir.Z * dir.Z
                );

            const float randomA =
                static_cast<float>(
                    NextRandom(rngState) &
                    0x00FFFFFF
                ) /
                static_cast<float>(0x01000000);

            const float randomB =
                static_cast<float>(
                    NextRandom(rngState) &
                    0x00FFFFFF
                ) /
                static_cast<float>(0x01000000);

            const float randomC =
                static_cast<float>(
                    NextRandom(rngState) &
                    0x00FFFFFF
                ) /
                static_cast<float>(0x01000000);

            /* Jitter base escalado pela forca (0 = reto, max = fraco). */
            const float jitterHi =
                (1.0f - forceT) *
                (SMOOTH_MAX * 2.0f);

            const float jitterLo = jitterHi * 0.35f;

            const float jitter =
                (randomA * (jitterHi - jitterLo)) +
                jitterLo;

            dir.X +=
                (randomB * jitter) -
                (jitter * 0.5f);

            dir.Y +=
                (randomC * jitter) -
                (jitter * 0.5f);

            dir.Z +=
                (randomA * jitter) -
                (jitter * 0.5f);

            /* HIT CHANCE: rola o dado por write. */
            int hitCfg = g_Globals.Silent.HitChance;

            if (hitCfg < 0)
                hitCfg = 0;

            if (hitCfg > 100)
                hitCfg = 100;

            const uint32_t hitRoll =
                NextRandom(rngState) % 100u;

            if (
                static_cast<int>(hitRoll) >= hitCfg &&
                dirLen > 1.0f
            )
            {
                const float missR =
                    static_cast<float>(
                        NextRandom(rngState) & 0xFFFF
                    ) / 65535.0f;

                /* Erro humano: 1.0%..3.5% da distancia (erra de verdade). */
                const float missMag =
                    (0.010f + 0.025f * missR) *
                    dirLen;

                dir.X +=
                    (randomB * 2.0f - 1.0f) * missMag;

                dir.Y +=
                    (randomC * 2.0f - 1.0f) * missMag;

                dir.Z +=
                    (randomA * 2.0f - 1.0f) * missMag * 0.4f;
            }

            const uintptr_t directionVA =
                weaponPtr +
                Offsets::HitObjectInfo::RayDir;

            if (
                SilWriteMem(
                    static_cast<uint32_t>(
                        Memory::GetTargetPid()
                    ),
                    directionVA,
                    &dir,
                    static_cast<uint32_t>(sizeof(Vector3))
                )
            )
            {
                writeFails = 0;
            }
            else
            {
                /*
                 * Ponte caiu/reconectando: respire pra nao girar em
                 * falso. Recuperados os writes, o ritmo volta sozinho.
                 */
                if (++writeFails > 8)
                {
                    Sleep(
                        static_cast<DWORD>(
                            WRITE_FAIL_BACKOFF_MS
                        )
                    );
                }
                else
                {
                    Sleep(2);
                }
            }

             /*
             * ============================================================
             * O NUCLEO DA CONSTANCIA (o core da versao antiga que "ia
             * quase sempre") — agora SEM medo, no canal DEDICADO:
             *
             * ATIRANDO: write em sequencia, SEM Sleep. O proprio
             * roundtrip da ponte da o ritmo (milhares por segundo). O
             * jogo reescreve o RayDir dele todo frame e consome no
             * instante do tiro; quem escreve POR ULTIMO ganha a bala.
             *
             * A ESP NAO sente: ela esta na OUTRA conexao (socket
             * propria) — este burst nao disputa nada com ela.
             * ============================================================
             */
            burstFiring = firing;

            if (firing)
            {
                ++writeCount;

                if ((writeCount & (BURST_YIELD_EVERY - 1)) == 0)
                    sched_yield();
            }
            else
            {
                /*
                 * Parado: write morno (mantem o ray apontado pro alvo
                 * pro primeiro tiro sair silent). JITTER de ritmo: o
                 * intervalo varia +-4ms por write — cadencia nunca
                 * fica metronomica (padrao regular e o que ferramenta
                 * de deteccao procura).
                 */
                Sleep(
                    static_cast<DWORD>(
                        WRITE_PACE_IDLE_MS +
                        static_cast<LONGLONG>(
                            NextRandom(rngState) % 5u
                        )
                    )
                );
            }
        }

        InterlockedExchange(
            &g_Running,
            0
        );

        return nullptr;
    }

    void Start()
    {
        EnsureStarted();
    }

    void Stop()
    {
        EnsureCSInit();

        SilCloseSock();

        InterlockedExchange(
            &g_Running,
            0
        );

        InterlockedExchange64(
            &g_LocalPlayer,
            0
        );

        InterlockedExchange64(
            &g_TargetEntity,
            0
        );

        InterlockedExchange(
            &g_ExtFiring,
            0
        );

        InterlockedExchange64(
            &g_ExtFiringTick,
            0
        );

        EnterCriticalSection(&g_HeadCS);
        s_CachedHead = {};
        s_CachedHeadTick = 0;
        s_CachedHeadTarget = 0;
        LeaveCriticalSection(&g_HeadCS);
    }

    void UpdateViewMatrix(
        const Matrix4x4& matrix
    )
    {
        EnsureCSInit();

        EnterCriticalSection(
            &g_MatrixCS
        );

        g_ViewMatrix = matrix;

        LeaveCriticalSection(
            &g_MatrixCS
        );
    }

    void SetTarget(
        uintptr_t localPlayer,
        uintptr_t targetEntity
    )
    {
        EnsureStarted();

        InterlockedExchange64(
            &g_LocalPlayer,
            static_cast<LONGLONG>(
                localPlayer
            )
        );

        InterlockedExchange64(
            &g_TargetEntity,
            static_cast<LONGLONG>(
                targetEntity
            )
        );
    }

    void NotifyFiring(bool firing)
    {
        InterlockedExchange(
            &g_ExtFiring,
            firing ? 1 : 0
        );

        InterlockedExchange64(
            &g_ExtFiringTick,
            static_cast<LONGLONG>(
                GetTickCount64()
            )
        );
    }

    void ClearTarget()
    {
        InterlockedExchange64(
            &g_TargetEntity,
            0
        );
    }
}
