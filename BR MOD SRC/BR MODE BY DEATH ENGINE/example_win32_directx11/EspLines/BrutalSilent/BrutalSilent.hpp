#pragma once

#include <atomic>
#include <cstdint>
#include <thread>
#include <EspLines/Player.h>

namespace BrutalSilentFunction {
    class BrutalSilent {
    public:
        static void Start();
        static void Stop();

    private:
        static void Run();
        static uintptr_t FindBestTargetAddress();
        static Vector3 CalculateBrutalAim(const Player& target);
        static Vector3 CalculateBrutalLead(const Player& target);
        static void ResetTargetState();

        static std::atomic<bool> running;
        static std::thread worker;
        static uint32_t cachedWeaponId;
        static uintptr_t lastTargetAddress;
        static Vector3 lastTargetPosition;
        static Vector3 targetVelocity;
        static uint64_t lastTargetUpdate;
    };
}
