#include "Silent.hpp"

#include <Globals.hpp>
#include <Memory/Memory.hpp>
#include <Offsets/Offsets.hpp>

#include <pthread.h>
#include <sched.h>

#include <chrono>
#include <cstdint>
#include <cstring>

namespace Silent
{
    static volatile LONGLONG g_LocalPlayer = 0;
    static volatile LONGLONG g_TargetEntity = 0;

    volatile LONG g_Running = 0;

    static CRITICAL_SECTION g_MatrixCS;
    static Matrix4x4 g_ViewMatrix = {};
    static bool g_CSInitialized = false;

    static void EnsureCSInit()
    {
        if (!g_CSInitialized)
        {
            InitializeCriticalSection(&g_MatrixCS);
            g_CSInitialized = true;
        }
    }

    static uintptr_t ReadPtr(
        uintptr_t addr,
        bool N32
    )
    {
        return N32
            ? g_FreeFireMemory.Read<uint32_t>(addr)
            : g_FreeFireMemory.Read<uint64_t>(addr);
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
     * Player::IsFiring (0x540 nos perfis v7a) — campo do jogo que indica
     * que o player local esta atirando. Define o MODO do laco: atirando,
     * burst de escrita sem pausa; parado, write morno a cada 20ms.
     *
     * Read<T> devolve zero quando a ponte falha. Aqui o falso negativo e
     * absorvido com retry + ultimo valor bom por 500ms — uma falha
     * transitoria no pico do combate nao derruba o burst no meio do spray.
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

            if (g_FreeFireMemory.Read(
                    localPlayer + Offsets::Player::IsFiring,
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

    static void* ThreadProc(void*)
    {
        const auto now =
            std::chrono::steady_clock::now();

        uint32_t rngState =
            static_cast<uint32_t>(
                now.time_since_epoch().count()
            );

        if (!rngState)
            rngState = 0xDEADBEEF;

        /*
         * Estado VIVO do laco — sobrevive a troca de alvo. Nao existe mais
         * laco interno/externo: trocar de alvo agora so invalida a cache de
         * posicao e o burst continua. Antes cada troca derrubava o laco,
         * pagava re-aquisicao (weaponPtr + cadeia de leitura) e o silent
         * passava fome de write exatamente durante o combate.
         */
        uintptr_t myLocal = 0;
        uintptr_t myTarget = 0;
        uintptr_t weaponPtr = 0;
        uintptr_t directionVA = 0;
        uintptr_t startPosVA = 0;

        const bool N32 = g_Globals.General.N32;

        Vector3 cachedAimPos = {};
        Vector3 cachedShootOrigin = {};
        bool posValid = false;
        LONGLONG posGoodMs = 0;

        bool firing = false;

        LONGLONG lastFireMs = 0;
        LONGLONG lastPosMs = 0;
        LONGLONG lastMatrixMs = 0;
        LONGLONG lastWeaponMs = 0;

        Matrix4x4 cachedMatrix = {};

        uint32_t writeCount = 0;
        uint32_t writeFails = 0;

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
                myLocal = 0;
                myTarget = 0;
                weaponPtr = 0;
                posValid = false;
                firing = false;

                Sleep(3);
                continue;
            }

            const LONGLONG nowMs =
                static_cast<LONGLONG>(GetTickCount64());

            /*
             * Local player (atomico). Troca de partida/jogador zera toda a
             * cache de arma e posicao.
             */
            const uintptr_t lp =
                static_cast<uintptr_t>(
                    InterlockedCompareExchange64(
                        &g_LocalPlayer,
                        0,
                        0
                    )
                );

            if (lp == 0)
            {
                myLocal = 0;
                myTarget = 0;
                weaponPtr = 0;
                posValid = false;
                firing = false;

                Sleep(10);
                continue;
            }

            if (lp != myLocal)
            {
                myLocal = lp;
                weaponPtr = 0;
                posValid = false;
            }

            /*
             * Alvo (atomico, custo zero): troca INLINE. So invalida a cache
             * de posicao — a nova cabeca chega no proximo refresh e o burst
             * NUNCA para por causa de troca de alvo.
             */
            const uintptr_t t =
                static_cast<uintptr_t>(
                    InterlockedCompareExchange64(
                        &g_TargetEntity,
                        0,
                        0
                    )
                );

            if (t != myTarget)
            {
                myTarget = t;
                posValid = false;
            }

            if (myTarget == 0)
            {
                /*
                 * Sem alvo: nada pra escrever. Zera o estado (nao faz
                 * sentido "atirando" sem direcao) e dorme barato — ZERO
                 * reads neste caminho, a ponte fica livre pra ESP.
                 */
                firing = false;
                posValid = false;

                Sleep(5);
                continue;
            }

            /*
             * FIX "ESCREVIA NO OBJETO MORTO": m_LastAimingInfoFromWeapon
             * aponta pro hit-object da arma. O jogo pode TROCAR esse objeto
             * (reload, troca de arma, re-aim). Antes o weaponPtr era lido
             * UMA vez por passada e o burst inteiro escrevia no endereco
             * velho — o silent parava de valer sem nenhum sinal. Agora e
             * re-lido por tempo (1 read a cada WEAPON_REFRESH_MS) e a
             * troca de ponteiro reinvalida so a posicao.
             */
            if (
                weaponPtr == 0 ||
                nowMs - lastWeaponMs >= WEAPON_REFRESH_MS
            )
            {
                const uintptr_t wp =
                    ReadPtr(
                        myLocal +
                        Offsets::Player::m_LastAimingInfoFromWeapon,
                        N32
                    );

                lastWeaponMs = nowMs;

                if (wp == 0)
                {
                    weaponPtr = 0;
                    posValid = false;

                    Sleep(3);
                    continue;
                }

                if (wp != weaponPtr)
                {
                    weaponPtr = wp;
                    directionVA =
                        wp + Offsets::HitObjectInfo::RayDir;
                    startPosVA =
                        wp + Offsets::HitObjectInfo::StartPosition;
                    posValid = false;
                }
            }

            /*
             * View matrix: copia LOCAL (secao critica, zero read na
             * ponte) — usada so pelo FovCheck do write "quente".
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
             * Estado de disparo: 1 read por pool (8ms atirando / 20ms
             * parado). Fora do caminho do write.
             */
            if (
                nowMs - lastFireMs >=
                (firing ? FIRE_CHECK_FIRING_MS : FIRE_CHECK_IDLE_MS)
            )
            {
                firing = ReadIsFiring(myLocal);
                lastFireMs = nowMs;
            }

            /*
             * Cabeca + origem: a cadeia da cabeca custa 7-15 roundtrips
             * (e o fallback do bone walker custa muito mais) — por isso e
             * refresh POR TEMPO em cima de CACHE, nunca por write.
             */
            if (
                nowMs - lastPosMs >=
                (firing ? POS_REFRESH_FIRING_MS : POS_REFRESH_IDLE_MS)
            )
            {
                lastPosMs = nowMs;

                Vector3 head =
                    Transform::GetHeadPosition(
                        myTarget,
                        N32
                    );

                if (
                    head.X != 0.0f ||
                    head.Y != 0.0f ||
                    head.Z != 0.0f
                )
                {
                    /*
                     * FIX "FOV DERRUBAVA O SILENT NO MEIO DO SPRAY": o
                     * FovCheck (em PIXELS) agora so limita o write "quente"
                     * FORA do tiro. ATIRANDO nao passa pelo FOV — quem ja
                     * validou o alvo foi a selecao do Draw (Silent.Fov +
                     * Silent.MaxDistance), e o recoil arrasta a mira pra
                     * fora do circulo durante o spray: barrar o write por
                     * isso deixava o silent intermitente.
                     */
                    if (
                        firing ||
                        W2S::FovCheck(
                            cachedMatrix,
                            head,
                            static_cast<float>(
                                g_Globals.Silent.Fov
                            )
                        )
                    )
                    {
                        head.Y += 0.05f;

                        const Vector3 org =
                            g_FreeFireMemory.Read<Vector3>(
                                startPosVA
                            );

                        if (
                            org.X != 0.0f ||
                            org.Y != 0.0f ||
                            org.Z != 0.0f
                        )
                        {
                            cachedAimPos = head;
                            cachedShootOrigin = org;
                            posValid = true;
                            posGoodMs = nowMs;
                        }
                    }
                }

                /*
                 * Posicao/origem velhas demais (alvo sumiu/leitura morreu):
                 * para de escrever. Ate esse limite ESCREVE com a cache —
                 * posicao de 200ms atras erra menos do que nao escrever
                 * nada (o jogo sobrescreve o ray e a bala vai no vazio).
                 */
                if (
                    posValid &&
                    nowMs - posGoodMs >= POS_STALE_MAX_MS
                )
                {
                    posValid = false;
                }
            }

            if (!posValid)
            {
                Sleep(firing ? 2 : 10);
                continue;
            }

            Vector3 dir;

            dir.X =
                cachedAimPos.X -
                cachedShootOrigin.X;

            dir.Y =
                cachedAimPos.Y -
                cachedShootOrigin.Y;

            dir.Z =
                cachedAimPos.Z -
                cachedShootOrigin.Z;

            if (
                dir.X == 0.0f &&
                dir.Y == 0.0f &&
                dir.Z == 0.0f
            )
            {
                Sleep(2);
                continue;
            }

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

            const float jitter =
                (
                    randomA *
                    (SMOOTH_MAX - SMOOTH_MIN)
                ) +
                SMOOTH_MIN;

            dir.X +=
                (randomB * jitter) -
                (jitter * 0.5f);

            dir.Y +=
                (randomC * jitter) -
                (jitter * 0.5f);

            dir.Z +=
                (randomA * jitter) -
                (jitter * 0.5f);

            /*
             * ============================================================
             * WRITE — O CORACAO DO FIX.
             *
             * ATIRANDO: write em sequencia, SEM Sleep (o proprio roundtrip
             * da ponte da o ritmo, milhares por segundo). O jogo reescreve
             * o RayDir dele todo frame e consome no instante do tiro; quem
             * escreve POR ULTIMO ganha a bala. Com densidade alta o silent
             * fica CONSTANTE do primeiro ao ultimo tiro do pente — o mesmo
             * motivo pelo qual o antigo flood de 28000 "ia", so que agora
             * sem travar o resto nem perder troca de alvo.
             * ============================================================
             */
            if (g_FreeFireMemory.Write<Vector3>(
                    directionVA,
                    dir
                ))
            {
                writeFails = 0;
            }
            else
            {
                /*
                 * Ponte caiu/reconectando: respire pra nao girar em falso
                 * (write falho retorna na hora — sem backoff viraria spin
                 * de CPU). Recuperados os writes, o ritmo volta sozinho.
                 */
                if (++writeFails > 32)
                {
                    Sleep(20);
                }
                else
                {
                    Sleep(2);
                }
            }

            if (firing)
            {
                /*
                 * No burst NAO dorme. So cede o timestamp a cada
                 * YIELD_EVERY_WRITES pra thread de leitura (ESP/snapshot)
                 * conseguir o socket e o jogo continuar fluindo.
                 */
                ++writeCount;

                if ((writeCount & (YIELD_EVERY_WRITES - 1)) == 0)
                    sched_yield();
            }
            else
            {
                /*
                 * Parado: write morno (mantem o ray apontado pro alvo pro
                 * primeiro tiro sair silent) — 1 write a cada 20ms.
                 */
                Sleep(WRITE_PACE_IDLE_MS);
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
        EnsureCSInit();

        if (
            InterlockedExchange(
                &g_Running,
                1
            ) != 0
        )
        {
            return;
        }

        pthread_t thread{};

        const int result =
            pthread_create(
                &thread,
                nullptr,
                ThreadProc,
                nullptr
            );

        if (result != 0)
        {
            InterlockedExchange(
                &g_Running,
                0
            );

            return;
        }

        pthread_detach(thread);
    }

    void Stop()
    {
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

    void ClearTarget()
    {
        InterlockedExchange64(
            &g_TargetEntity,
            0
        );
    }
}
