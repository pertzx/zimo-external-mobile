#include "BrutalSilent.hpp"

#include <Windows.h>
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <chrono>
#include <thread>

#include <src/Globals.hpp>
#include <EspLines/Memory/Memory.hpp>
#include <EspLines/Offsets.hpp>
#include <EspLines/Math/WordToScreen.hpp>
#include <EspLines/Math/Vector/Vector2.hpp>
#include <EspLines/Math/Vector/Vector3.hpp>

namespace BrutalSilentFunction {
    std::atomic<bool> BrutalSilent::running{ false };
    std::thread BrutalSilent::worker;
    uint32_t BrutalSilent::cachedWeaponId = 0;
    uintptr_t BrutalSilent::lastTargetAddress = 0;
    Vector3 BrutalSilent::lastTargetPosition = Vector3::Zero();
    Vector3 BrutalSilent::targetVelocity = Vector3::Zero();
    uint64_t BrutalSilent::lastTargetUpdate = 0;

    namespace {
        uint64_t NowMs() {
            return static_cast<uint64_t>(GetTickCount64());
        }
    }

    void BrutalSilent::ResetTargetState() {
        cachedWeaponId = 0;
        lastTargetAddress = 0;
        lastTargetPosition = Vector3::Zero();
        targetVelocity = Vector3::Zero();
        lastTargetUpdate = NowMs();
    }

    Vector3 BrutalSilent::CalculateBrutalLead(const Player& target) {
        const Vector3 currentPosition = target.Head;
        const uint64_t now = NowMs();
        const float deltaTime = lastTargetUpdate > 0
            ? static_cast<float>(now - lastTargetUpdate) / 1000.0f
            : 0.0f;

        if (deltaTime > 0.0f && lastTargetPosition != Vector3::Zero()) {
            targetVelocity = (currentPosition - lastTargetPosition) / deltaTime;
        }

        lastTargetPosition = currentPosition;
        lastTargetUpdate = now;

        constexpr float leadFactor = 2.5f;
        float speedMultiplier = 1.0f;
        const float speed = Vector3::Magnitude(targetVelocity);
        if (speed > 5.0f) {
            speedMultiplier = 1.8f;
        }
        else if (speed > 2.0f) {
            speedMultiplier = 1.3f;
        }

        Vector3 lead = targetVelocity * leadFactor * speedMultiplier * 0.06f;
        constexpr float maxLead = 3.0f;
        if (Vector3::Magnitude(lead) > maxLead) {
            lead = Vector3::Normalized(lead) * maxLead;
        }
        return lead;
    }
    // 111
    Vector3 BrutalSilent::CalculateBrutalAim(const Player& target) {
        int chestRate = g_Globals.AimBot.ChestRate;
        int headRate = g_Globals.AimBot.HeadRate;

        Vector3 headPos = target.Head;
        Vector3 chestPos = (target.LeftShoulder + target.RightShoulder) * 0.5f;
        if (chestPos == Vector3::Zero()) {
            chestPos = target.Neck;
        }

        Vector3 targetPosition;
        if (g_Globals.AimBot.BrutalSilentFullHeadshot) {
            targetPosition = headPos;
        }
        else {
            int total = headRate + chestRate;
            if (total <= 0) total = 1;
            static float s_acc = 0.0f;
            s_acc += (float)headRate / (float)total;
            bool isHead = false;
            if (s_acc >= 1.0f) {
                s_acc -= 1.0f;
                isHead = true;
            }
            if (headRate <= 0) isHead = false;
            if (chestRate <= 0) isHead = true;
            if (isHead) {
                targetPosition = headPos;
            } else {
                targetPosition = chestPos;
            }
        }

        targetPosition += CalculateBrutalLead(target);

        const float distance = Vector3::Distance(g_Globals.EspConfig.MainCamera, target.Head);
        if (distance < 30.0f) {
            targetPosition += Vector3(0.0f, 0.02f, 0.0f);
        }
        else if (distance < 80.0f) {
            targetPosition += Vector3(0.0f, 0.01f, 0.0f);
        }
        return targetPosition;
    }

    uintptr_t BrutalSilent::FindBestTargetAddress() {
        uintptr_t bestAddress = 0;
        float bestScore = FLT_MAX;
        const Vector2 screenCenter(
            g_Globals.EspConfig.Width / 2.0f,
            g_Globals.EspConfig.Height / 2.0f
        );

        for (auto& pair : g_Globals.EspConfig.Entities) {
            Player& entity = pair.second;
            if (!entity.IsKnown || entity.IsDead) continue;
            if (g_Globals.AimBot.BrutalSilentIgnoreKnocked && entity.IsKnocked) continue;
            if (entity.IsTeam == Player::Bool3::True) continue;
            if (g_Globals.AimBot.BrutalSilentIgnoreBots && entity.IsBot) continue;

            const float distance3D = Vector3::Distance(g_Globals.EspConfig.MainCamera, entity.Head);
            if (distance3D > static_cast<float>(g_Globals.AimBot.BrutalSilentMaxDistance) || distance3D > 600.0f) continue;

            const Vector2 head2D = W2S::WorldToScreen(
                g_Globals.EspConfig.ViewMatrix,
                entity.Head,
                g_Globals.EspConfig.Width,
                g_Globals.EspConfig.Height
            );
            if (head2D.X < 1.0f || head2D.Y < 1.0f ||
                head2D.X > g_Globals.EspConfig.Width - 1.0f ||
                head2D.Y > g_Globals.EspConfig.Height - 1.0f) continue;

            const float crosshairDistance = Vector2::Distance(screenCenter, head2D);
            if (crosshairDistance > static_cast<float>(g_Globals.AimBot.BrutalSilentFov)) continue;

            float score = crosshairDistance * 0.5f + distance3D * 0.2f;
            score -= Vector3::Magnitude(targetVelocity) * 5.0f;
            score -= (200.0f - static_cast<float>(entity.Health)) * 0.5f;

            if (score < bestScore) {
                bestScore = score;
                bestAddress = entity.Address;
            }
        }
        return bestAddress;
    }

    void BrutalSilent::Run() {
        ResetTargetState();
        while (running.load()) {
            if (!g_Globals.AimBot.BrutalSilent) {
                ResetTargetState();
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
                continue;
            }

            const bool isShooting = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
            if (!isShooting) {
                ResetTargetState();
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                continue;
            }

            if (g_Globals.EspConfig.Width <= 0 || g_Globals.EspConfig.Height <= 0 ||
                !g_Globals.EspConfig.Matrix || !g_Globals.EspConfig.LocalPlayer) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                continue;
            }

            uintptr_t targetAddr = FindBestTargetAddress();
            if (targetAddr == 0) {
                ResetTargetState();
                std::this_thread::yield();
                continue;
            }

            Player* target = nullptr;
            for (auto& pair : g_Globals.EspConfig.Entities) {
                if (pair.second.Address == targetAddr && !pair.second.IsDead) {
                    target = &pair.second;
                    break;
                }
            }

            if (!target) {
                ResetTargetState();
                std::this_thread::yield();
                continue;
            }

            lastTargetAddress = targetAddr;
            const Vector3 aimPosition = CalculateBrutalAim(*target);

            if (cachedWeaponId == 0) {
                Mem.ReadFast2<uint32_t>(
                    g_Globals.EspConfig.LocalPlayer + Offsets::BrutalSilentWeaponInfo,
                    &cachedWeaponId
                );
            }

            if (cachedWeaponId != 0) {
                Vector3 startPosition;
                if (Mem.ReadFast2<Vector3>(
                    cachedWeaponId + Offsets::BrutalSilentGunTipPosition,
                    &startPosition
                ) && startPosition != Vector3::Zero()) {
                    const Vector3 finalAim = aimPosition - startPosition;
                    Mem.Write<Vector3>(cachedWeaponId + Offsets::BrutalSilentBulletHit, finalAim);
                    Mem.Write<Vector3>(cachedWeaponId + Offsets::BrutalSilentBulletHit, finalAim);
                    Mem.Write<Vector3>(cachedWeaponId + Offsets::BrutalSilentBulletHit, finalAim);
                }
            }
            std::this_thread::yield();
        }
    }

    void BrutalSilent::Start() {
        if (running.exchange(true)) return;
        worker = std::thread(&BrutalSilent::Run);
    }

    void BrutalSilent::Stop() {
        if (!running.exchange(false)) return;
        if (worker.joinable()) worker.join();
        ResetTargetState();
    }
}
