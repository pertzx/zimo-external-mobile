#include "Silent.hpp"

#include <Globals.hpp>
#include <Memory/Memory.hpp>
#include <Offsets/Offsets.hpp>

#include <pthread.h>

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
     * Player::UGCStartFiring — campo do jogo que indica que o player local
     * esta atirando. Define a cadencia do loop: disparando, o RayDir e
     * reescrito agressivo (2ms); fora do disparo, ritmo morno (20ms) apenas
     * para manter a direcao quente para o primeiro tiro.
     *
     * Se o offset nao estiver preenchido (v8a ainda com TODO) devolve false
     * e o loop roda no ritmo idle — o silent continua funcionando, so sem o
     * boost de cadencia.
     */
    static bool ReadIsFiring(
        uintptr_t localPlayer
    )
    {
        if (
            localPlayer == 0 ||
            Offsets::Player::IsFiring == 0
        )
        {
            return false;
        }

        return g_FreeFireMemory.Read<bool>(
            localPlayer + Offsets::Player::IsFiring
        );
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

            const bool N32 =
                g_Globals.General.N32;

            const uintptr_t localPlayer =
                static_cast<uintptr_t>(
                    InterlockedCompareExchange64(
                        &g_LocalPlayer,
                        0,
                        0
                    )
                );

            const uintptr_t targetEntity =
                static_cast<uintptr_t>(
                    InterlockedCompareExchange64(
                        &g_TargetEntity,
                        0,
                        0
                    )
                );

            if (
                localPlayer == 0 ||
                targetEntity == 0
            )
            {
                /*
                 * Sem alvo: dorme em vez de girar em sched_yield —
                 * o spin queimava CPU do client sem fazer nada.
                 */
                Sleep(2);
                continue;
            }

            const uintptr_t weaponPtr =
                ReadPtr(
                    localPlayer +
                    Offsets::Player::m_LastAimingInfoFromWeapon,
                    N32
                );

            if (weaponPtr == 0)
            {
                Sleep(2);
                continue;
            }

            const uintptr_t directionVA =
                weaponPtr +
                Offsets::HitObjectInfo::RayDir;

            const uintptr_t startPosVA =
                weaponPtr +
                Offsets::HitObjectInfo::StartPosition;

            /*
             * CADENCIA — a ponte e um socket: cada operacao custa um
             * roundtrip. O burst de 28000 writes levava SEGUNDOS por
             * passada e a posicao do alvo ficava velha (o "silent
             * demorando muito"). Agora e um laco continuo e ritmado:
             *
             *  - direcao reescrita a cada 2ms atirando / 20ms parado
             *  - cabeca + origem relidos a cada 8ms atirando / 20ms parado
             *  - view matrix recopiada a cada 33ms (FovCheck honesto)
             *  - estado de disparo rechecado a cada 48ms
             *  - troca de alvo detectada em TODA iteracao (atomica, gratis)
             */
            Matrix4x4 cachedMatrix;

            EnterCriticalSection(&g_MatrixCS);
            cachedMatrix = g_ViewMatrix;
            LeaveCriticalSection(&g_MatrixCS);

            Vector3 cachedAimPos = {};
            Vector3 cachedShootOrigin = {};
            bool posValid = false;

            const LONGLONG startMs =
                static_cast<LONGLONG>(GetTickCount64());

            LONGLONG lastMatrixMs =
                startMs - MATRIX_REFRESH_MS;

            LONGLONG lastPosMs =
                startMs - POS_REFRESH_FIRING_MS;

            LONGLONG lastFireCheckMs =
                startMs - FIRE_CHECK_MS;

            bool firing = false;

            while (
                InterlockedCompareExchange(
                    &g_Running,
                    0,
                    0
                ) != 0 &&
                !g_Globals.General.ShutDown
            )
            {
                /*
                 * Troca de alvo / ClearTarget: sai NA HORA. Leitura
                 * atomica a cada iteracao — custo zero, reacao de um
                 * ciclo (no burst antigo era a cada 1024 writes).
                 */
                const uintptr_t curTarget =
                    static_cast<uintptr_t>(
                        InterlockedCompareExchange64(
                            &g_TargetEntity,
                            0,
                            0
                        )
                    );

                if (
                    curTarget != targetEntity ||
                    curTarget == 0
                )
                {
                    break;
                }

                const LONGLONG nowMs =
                    static_cast<LONGLONG>(GetTickCount64());

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

                if (
                    nowMs - lastFireCheckMs >=
                    FIRE_CHECK_MS
                )
                {
                    firing = ReadIsFiring(localPlayer);
                    lastFireCheckMs = nowMs;
                }

                const LONGLONG posRefreshMs =
                    firing
                        ? POS_REFRESH_FIRING_MS
                        : POS_REFRESH_IDLE_MS;

                if (
                    !posValid ||
                    nowMs - lastPosMs >= posRefreshMs
                )
                {
                    Vector3 newAimPos =
                        Transform::GetHeadPosition(
                            targetEntity,
                            N32
                        );

                    if (
                        newAimPos.X == 0.0f &&
                        newAimPos.Y == 0.0f &&
                        newAimPos.Z == 0.0f
                    )
                    {
                        posValid = false;
                        lastPosMs = nowMs;

                        Sleep(2);
                        continue;
                    }

                    if (!W2S::FovCheck(
                            cachedMatrix,
                            newAimPos,
                            static_cast<float>(
                                g_Globals.Silent.Fov
                            )
                        ))
                    {
                        break;
                    }

                    newAimPos.Y += 0.05f;
                    cachedAimPos = newAimPos;

                    cachedShootOrigin =
                        g_FreeFireMemory.Read<Vector3>(startPosVA);

                    if (
                        cachedShootOrigin.X == 0.0f &&
                        cachedShootOrigin.Y == 0.0f &&
                        cachedShootOrigin.Z == 0.0f
                    )
                    {
                        posValid = false;
                        lastPosMs = nowMs;

                        Sleep(2);
                        continue;
                    }

                    posValid = true;
                    lastPosMs = nowMs;
                }

                if (!posValid)
                    continue;

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

                g_FreeFireMemory.Write<Vector3>(
                    directionVA,
                    dir
                );

                Sleep(
                    firing
                        ? WRITE_PACE_FIRING_MS
                        : WRITE_PACE_IDLE_MS
                );
            }

            /*
             * Saiu do laco (alvo trocou/perdeu FOV/desligou): respira
             * 4ms antes de readquirir — evita girar a secao de aquisicao
             * (weaponPtr + cadeia de leitura) em spin contra a ponte.
             */
            Sleep(4);
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
