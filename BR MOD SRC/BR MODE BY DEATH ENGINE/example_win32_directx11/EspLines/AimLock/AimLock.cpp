#include <imgui.h>
#include <imgui_internal.h>
#include <EspLines/Memory/Memory.hpp>
#include <EspLines/Math/Vector/Vector3.hpp>
#include <EspLines/Math/Vector/Vector2.hpp>
#include <EspLines/Aimbot/Aimbot.hpp>
#include <src/Globals.hpp>
#include <EspLines/Offsets.hpp>
#include <EspLines/Math/WordToScreen.hpp>
#include "AimLock.hpp"

Player* AimLockFunction::AimLock::lastTarget = nullptr;

void AimLockFunction::AimLock::Run() {
    if (!g_Globals.AimBot.AimLock) return;
    if (!g_Globals.EspConfig.Matrix || g_Globals.EspConfig.Width <= 0 || g_Globals.EspConfig.Height <= 0) return;

    uint32_t localPlayer = g_Globals.EspConfig.LocalPlayer;
    if (!localPlayer) return;

    auto weapon = Mem.Read<uintptr_t>(localPlayer + Offsets::Weapon);
    if (weapon != 0) {
        Mem.Write<float>(weapon + Offsets::NoRecoilFlag, 0.0f);
    }
}