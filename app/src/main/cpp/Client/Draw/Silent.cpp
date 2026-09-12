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
                sched_yield();
                continue;
            }

            Memory::FlushTLB();

            const uintptr_t weaponPtr =
                ReadPtr(
                    localPlayer +
                    Offsets::Player::m_LastAimingInfoFromWeapon,
                    N32
                );

            if (weaponPtr == 0)
            {
                sched_yield();
                continue;
            }

            const uintptr_t directionVA =
                weaponPtr +
                Offsets::HitObjectInfo::RayDir;

            const uintptr_t startPosVA =
                weaponPtr +
                Offsets::HitObjectInfo::StartPosition;

            uintptr_t dirPhys = 0;

            if (!g_FreeFireMemory.TranslateVA(
                    directionVA,
                    dirPhys
                ))
            {
                sched_yield();
                continue;
            }

            uintptr_t startPhys = 0;

            g_FreeFireMemory.TranslateVA(
                startPosVA,
                startPhys
            );

            // PageMapping writeMap;

            // void* dirHostPtr =
            //     writeMap.ResolveWrite(dirPhys);

            // if (!dirHostPtr)
            // {
            //     sched_yield();
            //     continue;
            // }

            Matrix4x4 cachedMatrix;

            EnterCriticalSection(&g_MatrixCS);
            cachedMatrix = g_ViewMatrix;
            LeaveCriticalSection(&g_MatrixCS);

            Vector3 cachedAimPos = {};
            Vector3 cachedShootOrigin = {};
            bool posValid = false;

            for (int i = 0;
                 i < WRITE_LOOP_COUNT;
                 ++i)
            {
                if ((i & 1023) == 0)
                {
                    if (
                        InterlockedCompareExchange(
                            &g_Running,
                            0,
                            0
                        ) == 0 ||
                        g_Globals.General.ShutDown
                    )
                    {
                        break;
                    }

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
                }

                if ((i & 127) == 0)
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

                    cachedShootOrigin = g_FreeFireMemory.Read<Vector3>(startPosVA);

                    if (
                        cachedShootOrigin.X == 0.0f &&
                        cachedShootOrigin.Y == 0.0f &&
                        cachedShootOrigin.Z == 0.0f
                    )
                    {
                        posValid = false;
                        continue;
                    }

                    posValid = true;

                    if (directionVA == 0)
                    break;
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

                // if ((i & 2047) == 0 && i > 0)
                // {
                //     writeMap.Release();

                //     sched_yield();

                //     if (directionVA == 0)
                //     break;
                // }
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