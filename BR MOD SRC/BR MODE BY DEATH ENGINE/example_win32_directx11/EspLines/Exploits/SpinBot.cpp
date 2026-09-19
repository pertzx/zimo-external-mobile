#include "SpinBot.hpp"

#include <chrono>
#include <cmath>

#include <EspLines/Memory/Memory.hpp>
#include <EspLines/Math/Quaternion.hpp>
#include <EspLines/Offsets.hpp>
#include <src/Globals.hpp>

namespace {
    constexpr float kPi = 3.14159265358979323846f;
    constexpr uint32_t kMinValidPtr = 0x10000;

    float currentYaw = 0.0f;
    std::chrono::steady_clock::time_point lastTick = std::chrono::steady_clock::now();

    bool IsValidPtr(uint32_t value) {
        return value >= kMinValidPtr && value != static_cast<uint32_t>(-1);
    }

    float ClampFloat(float value, float minValue, float maxValue) {
        if (value < minValue) return minValue;
        if (value > maxValue) return maxValue;
        return value;
    }
}

void SpinBot::Reset() {
    currentYaw = 0.0f;
    lastTick = std::chrono::steady_clock::now();
}

void SpinBot::Run() {
    const auto now = std::chrono::steady_clock::now();
    float deltaTime = std::chrono::duration<float>(now - lastTick).count();
    lastTick = now;
    deltaTime = ClampFloat(deltaTime, 0.0f, 0.05f);

    if (!g_Globals.Exploits.SpinBot || !g_Globals.EspConfig.LocalPlayer) {
        Reset();
        return;
    }

    const uint32_t localPlayer = g_Globals.EspConfig.LocalPlayer;
    uint32_t entityRootBone = 0;
    if (!Mem.ReadFast2<uint32_t>(localPlayer + Offsets::Bones::Root, &entityRootBone) ||
        !IsValidPtr(entityRootBone)) {
        return;
    }

    uint32_t transformValueRoot = 0;
    if (!Mem.ReadFast2<uint32_t>(entityRootBone + 0x8, &transformValueRoot) ||
        !IsValidPtr(transformValueRoot)) {
        return;
    }

    uint32_t rootBoneClass = 0;
    if (!Mem.ReadFast2<uint32_t>(transformValueRoot + 0x8, &rootBoneClass) ||
        !IsValidPtr(rootBoneClass)) {
        return;
    }

    uint32_t rootMatrixValueList = 0;
    if (!Mem.ReadFast2<uint32_t>(rootBoneClass + 0x20, &rootMatrixValueList) ||
        !IsValidPtr(rootMatrixValueList)) {
        return;
    }

    const float speed = ClampFloat(
        static_cast<float>(g_Globals.Exploits.SpinBotSpeed),
        1.0f,
        1000.0f
    );

    currentYaw = std::fmod(currentYaw + speed * deltaTime, 360.0f);
    if (currentYaw < 0.0f) {
        currentYaw += 360.0f;
    }

    const float angle = currentYaw * (kPi / 180.0f);
    const Quaternion spin(
        0.0f,
        std::sin(angle * 0.5f),
        0.0f,
        std::cos(angle * 0.5f)
    );

    Mem.Write<Quaternion>(rootMatrixValueList + 0x70, spin);
}
