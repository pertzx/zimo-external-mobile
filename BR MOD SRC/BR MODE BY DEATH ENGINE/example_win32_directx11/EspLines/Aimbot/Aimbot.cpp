#include <imgui.h>
#include <imgui_internal.h>
#include <EspLines\Memory\Memory.hpp>
#include <EspLines\Math\Vector\Vector3.hpp>
#include <EspLines\Math\Vector\Vector2.hpp>
#include <EspLines\Aimbot\Aimbot.hpp> 
#include <src\Globals.hpp>
#include <EspLines\Offsets.hpp>
#include <EspLines\Math\WordToScreen.hpp>

Vector3 GetHitBoxPosition(const Player& entity) {
    using HitBox = Config::HitBox;

    switch (g_Globals.AimBot.HitBox) {
    case HitBox::Neck: return entity.Neck;
    case HitBox::Chest: return entity.Hip;
    case HitBox::Head: return entity.Head;
    default:            return entity.Head;
    }
}

Player* FindClosestEnemy() {
    float closestDistance = FLT_MAX;
    Player* closestEntity = nullptr;

    Vector2 screenCenter(g_Globals.EspConfig.Width / 2.0f, g_Globals.EspConfig.Height / 2.0f);

    for (auto& pair : g_Globals.EspConfig.Entities) {
        Player* entity = &pair.second;

        if (entity->IsDead || (g_Globals.AimBot.IgnoreKnocked && entity->Pose == Offsets::XPose)) continue;

        Vector3 targetHitBox = GetHitBoxPosition(*entity);
        ImVec2 hitBox2D = W2S::WorldToScreenImVec2(g_Globals.EspConfig.ViewMatrix, targetHitBox, g_Globals.EspConfig.Width, g_Globals.EspConfig.Height);
        if (hitBox2D.x < 1 || hitBox2D.y < 1) continue;

        float distance = Vector3::Distance(g_Globals.EspConfig.MainCamera, targetHitBox);
        if (distance > g_Globals.AimBot.DistanceAim) continue;

        float crosshairDist = std::sqrt(std::pow(hitBox2D.x - screenCenter.X, 2) + std::pow(hitBox2D.y - screenCenter.Y, 2));
        if (crosshairDist >= closestDistance) continue;

        closestDistance = crosshairDist;
        closestEntity = entity;
    }

    return closestEntity;
}

static Player* lastTarget = nullptr;
static bool aimbotKeyPressed = false;
static bool aimbotToggled = false;
static std::chrono::steady_clock::time_point lastToggleTime = std::chrono::steady_clock::now();

void Aim::Aimbot::LegitAimbot()
{
    bool keyDown = (GetAsyncKeyState(g_Globals.AimBot.AimbotBind) & 0x8000) != 0;
    auto now = std::chrono::steady_clock::now();
    auto timeSinceLastToggle = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastToggleTime).count();

    // Lgica de toggle para todas as teclas (exceto mouse esquerdo)
    if (g_Globals.AimBot.AimbotBind != VK_LBUTTON) {
        if (keyDown && !aimbotKeyPressed && timeSinceLastToggle > 250) {
            aimbotToggled = !aimbotToggled;
            aimbotKeyPressed = true;
            lastToggleTime = now;
        }

        if (!keyDown) {
            aimbotKeyPressed = false;
        }
    }

    bool shouldAim = false;

    if (g_Globals.AimBot.AimbotBind == VK_LBUTTON) {
        // Mouse esquerdo: segurar para ativar
        shouldAim = keyDown && g_Globals.AimBot.Enabled;
    }
    else {
        // Outras teclas: toggle
        shouldAim = aimbotToggled && g_Globals.AimBot.Enabled;
    }

    if (shouldAim) {
        Player* target = FindClosestEnemy();
        if (!target || target->Address == 0) {
            std::this_thread::sleep_for(std::chrono::nanoseconds(5));
            return;
        }

        int total = g_Globals.AimBot.HeadRate + g_Globals.AimBot.ChestRate;
        if (total <= 0) total = 1;
        static float s_acc1 = 0.0f;
        s_acc1 += (float)g_Globals.AimBot.HeadRate / (float)total;
        bool isHead1 = false;
        if (s_acc1 >= 1.0f) { s_acc1 -= 1.0f; isHead1 = true; }
        if (g_Globals.AimBot.HeadRate <= 0) isHead1 = false;
        if (g_Globals.AimBot.ChestRate <= 0) isHead1 = true;
        if (!isHead1) {
            std::this_thread::sleep_for(std::chrono::milliseconds(15));
            return;
        }

        uintptr_t headColliderAddr = target->Address + Offsets::Collider;
        uintptr_t lockedAimAddr = target->Address + Offsets::LockedAimingCollider;

        uint32_t collider = 0;
        if (!Mem.ReadFast2<uint32_t>(headColliderAddr, &collider) || collider == 0)
            return;

        uint32_t current = 0;
        if (Mem.ReadFast2<uint32_t>(lockedAimAddr, &current) && current != collider)
        {
            Mem.Write<uint32_t>(lockedAimAddr, 0UL);
            Mem.Write<uint32_t>(lockedAimAddr, collider);
            std::this_thread::sleep_for(std::chrono::milliseconds(15));
            Mem.Write<uint32_t>(lockedAimAddr, 0UL);
        }
    }
}

static uint32_t lastPatchedAddr = 0;
static uint32_t lastOriginalValue = 0;
static bool isPatched = false;

void Aim::Aimbot::AimBotVisibleSafe()
{
    if (!g_Globals.AimBot.AimBotVisibleSafe) return;
    if (!g_Globals.EspConfig.Matrix || g_Globals.EspConfig.Width <= 0 || g_Globals.EspConfig.Height <= 0) return;

    uint32_t localPlayer = g_Globals.EspConfig.LocalPlayer;
    if (!localPlayer) return;

    bool keyDown = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;

    Player* bestTarget = nullptr;
    float closestDistSq = FLT_MAX;
    Vector2 screenCenter(g_Globals.EspConfig.Width / 2.0f, g_Globals.EspConfig.Height / 2.0f);
    const float fovSq = 120.0f * 120.0f;

    for (auto& pair : g_Globals.EspConfig.Entities) {
        Player* entity = &pair.second;
        if (!entity || entity->Address == 0) continue;
        if (entity->IsDead) continue;
        if (entity->IsTeam == Player::Bool3::True) continue;
        if (!entity->IsVisible) continue;

        Vector3 headPos = entity->Head;
        if (headPos == Vector3::Zero()) continue;

        ImVec2 head2D = W2S::WorldToScreenImVec2(g_Globals.EspConfig.ViewMatrix, headPos, g_Globals.EspConfig.Width, g_Globals.EspConfig.Height);
        if (head2D.x < 1 || head2D.y < 1) continue;

        float dx = head2D.x - screenCenter.X;
        float dy = head2D.y - screenCenter.Y;
        float distSq = dx * dx + dy * dy;

        if (distSq <= fovSq && distSq < closestDistSq) {
            closestDistSq = distSq;
            bestTarget = entity;
        }
    }

    if (bestTarget) {
        uintptr_t patchAddr = bestTarget->Address + Offsets::LockedAimingCollider;
        uint32_t headCollider = 0;

        if (keyDown) {
            int total = g_Globals.AimBot.HeadRate + g_Globals.AimBot.ChestRate;
            if (total <= 0) total = 1;
            static float s_acc2 = 0.0f;
            s_acc2 += (float)g_Globals.AimBot.HeadRate / (float)total;
            bool isHead2 = false;
            if (s_acc2 >= 1.0f) { s_acc2 -= 1.0f; isHead2 = true; }
            if (g_Globals.AimBot.HeadRate <= 0) isHead2 = false;
            if (g_Globals.AimBot.ChestRate <= 0) isHead2 = true;
            if (!isHead2) {
                goto restore_patch;
            }
            if (!Mem.ReadFast2<uint32_t>(bestTarget->Address + Offsets::HeadCollider, &headCollider) || headCollider == 0) {
                goto restore_patch;
            }

            if (!isPatched) {
                if (!Mem.ReadFast2<uint32_t>(patchAddr, &lastOriginalValue)) goto restore_patch;
                lastPatchedAddr = patchAddr;
            }

            Mem.Write<uint32_t>(patchAddr, headCollider);
            if (!isPatched) isPatched = true;
        }
        else {
        restore_patch:
            if (isPatched && lastPatchedAddr) {
                Mem.Write<uint32_t>(lastPatchedAddr, lastOriginalValue);
                isPatched = false;
                lastPatchedAddr = 0;
                lastOriginalValue = 0;
            }
        }
    }
    else {
        if (isPatched && lastPatchedAddr) {
            Mem.Write<uint32_t>(lastPatchedAddr, lastOriginalValue);
            isPatched = false;
            lastPatchedAddr = 0;
            lastOriginalValue = 0;
        }
    }

    if (g_Globals.AimBot.AimBotVisibleSafe) {
        bool isKeyPressed = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;

        if (isKeyPressed) {
            Player* target = FindClosestEnemy();
            if (target && target->Address != 0) {
                Vector3 headPos = target->Head;
                if (headPos != Vector3::Zero()) {
                    ImVec2 targetPos = W2S::WorldToScreenImVec2(g_Globals.EspConfig.ViewMatrix, headPos, g_Globals.EspConfig.Width, g_Globals.EspConfig.Height);
                    bool isOnScreen = targetPos.x > 0.0f && targetPos.y > 0.0f &&
                        targetPos.x < g_Globals.EspConfig.Width && targetPos.y < g_Globals.EspConfig.Height;

                    if (isOnScreen) {
                        uint32_t rHeadCollider = 0;
                        if (Mem.ReadFast2<uint32_t>(target->Address + Offsets::Collider, &rHeadCollider) && rHeadCollider) {
                            float randomValue = static_cast<float>(rand()) / RAND_MAX;
                            if (randomValue <= g_Globals.AimBot.AimBotVisibleSafeStrength) {
                                Mem.Write<uint32_t>(target->Address + Offsets::LockedAimingCollider, rHeadCollider);
                                int randomDelay = 15 + (rand() % 10);
                                std::this_thread::sleep_for(std::chrono::milliseconds(randomDelay));
                            }
                        }
                    }
                }
            }
        }
    }
}