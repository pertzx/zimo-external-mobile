#include "aimbot.hpp"
#include <android/shared/IpcProtocol.h>
#include <imgui.h>
#include <cmath>
#include <limits>

namespace ZmInternal {
    namespace Logic {

        // Thread-local storage for aimbot state
        thread_local bool g_aimbotActive = false;
        thread_local uint64_t g_lastTargetId = 0;
        thread_local float g_lastAimbotTime = 0.0f;

        // Helper function for world to screen conversion (placeholder)
        bool WorldToScreen(float worldX, float worldY, float worldZ, float& screenX, float& screenY) {
            // Same placeholder as in ESP - should be implemented properly with game matrices
            return false;
        }

        // Calculate distance between two points in 3D space
        float CalculateDistance(float x1, float y1, float z1, float x2, float y2, float z2) {
            float dx = x2 - x1;
            float dy = y2 - y1;
            float dz = z2 - z1;
            return std::sqrt(dx*dx + dy*dy + dz*dz);
        }

        // Find the closest valid target from snapshot
        uint64_t FindBestTarget(const IpcMsgSnapshot* snapshot, const IpcMsgConfig* config) {
            if (!snapshot || !config) {
                return 0;
            }

            // In a real implementation, we would need the local player's position
            // For now, we'll use a placeholder position (0,0,0)
            float localPlayerX = 0.0f;
            float localPlayerY = 0.0f;
            float localPlayerZ = 0.0f;

            uint64_t bestTargetId = 0;
            float bestScore = std::numeric_limits<float>::max(); // Lower is better

            for (uint32_t i = 0; i < snapshot->count; i++) {
                const EntitySnapshot& entity = snapshot->entities[i];

                // Skip inactive entities
                if (!entity.isActive) {
                    continue;
                }

                // Skip if not an enemy (teamId == 0 for enemies based on IpcProtocol.h comment)
                if (entity.teamId != 0) {
                    continue;
                }

                // Skip knocked enemies if configured
                if (config->ignoreKnocked && entity.isKnocked) {
                    continue;
                }

                // Skip bots if configured
                // Note: We don't have isBot in snapshot, would need to check via other means
                // For now, we'll skip this check

                // Check distance
                float distance = CalculateDistance(
                    localPlayerX, localPlayerY, localPlayerZ,
                    entity.posX, entity.posY, entity.posZ
                );

                if (distance > config->maxDistance) {
                    continue;
                }

                // Check if we should only target visible entities
                // Note: We don't have visibility info in snapshot, would need to check via memory
                // For now, we'll skip this check

                // Simple scoring: closer is better
                float score = distance;

                if (score < bestScore) {
                    bestScore = score;
                    bestTargetId = entity.entityId;
                }
            }

            return bestTargetId;
        }

        void UpdateAimbot(const IpcMsgSnapshot* snapshot, const IpcMsgConfig* config) {
            if (!snapshot || !config || !config->aimbotEnabled) {
                g_aimbotActive = false;
                return;
            }

            // Check if aimbot key is pressed (would need input handling)
            // For now, we'll simulate with a placeholder - in real implementation,
            // this would come from input handling system
            bool aimbotKeyPressed = false; // Placeholder

            if (!aimbotKeyPressed) {
                g_aimbotActive = false;
                return;
            }

            // Find best target
            uint64_t targetId = FindBestTarget(snapshot, config);

            if (targetId == 0) {
                g_aimbotActive = false;
                g_lastTargetId = 0;
                return;
            }

            // Check if target is still valid (simple target locking)
            if (g_lastTargetId != targetId) {
                // Target changed, reset lock
                g_lastTargetId = targetId;
            }

            g_aimbotActive = true;
            g_lastAimbotTime = ImGui::GetTime();

            // In a real implementation, we would:
            // 1. Get the target's head position from snapshot
            // 2. Calculate aim angles
            // 3. Send those angles to the daemon to apply via memory write
            // 4. Or directly send a command to daemon to aim at target

            // For now, we just set the active flag
        }

        bool IsAimbotActive() {
            // Optional: add timeout to prevent permanent aimbot lock
            if (g_aimbotActive && (ImGui::GetTime() - g_lastAimbotTime > 5.0f)) {
                g_aimbotActive = false;
                g_lastTargetId = 0;
            }
            return g_aimbotActive;
        }
    }
}