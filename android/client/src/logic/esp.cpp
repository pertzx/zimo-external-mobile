#include "esp.hpp"
#include <android/shared/IpcProtocol.h>
#include <imgui.h>
#include <cmath>
#include <string>

namespace ZmInternal {
    namespace Logic {

        // Helper function for world to screen conversion (placeholder - should be implemented properly)
        bool WorldToScreen(float worldX, float worldY, float worldZ, float& screenX, float& screenY) {
            // This is a simplified placeholder - in a real implementation,
            // we would need the view-projection matrix from the game
            // For now, we'll use a simple orthographic projection as example
            // In practice, this should come from the game's camera matrices

            // Placeholder implementation - returns false indicating not implemented
            return false;
        }

        void RenderESP(const IpcMsgSnapshot* snapshot, ImDrawList* draw_list) {
            if (!snapshot || !draw_list) {
                return;
            }

            // Get screen dimensions (should ideally come from daemon via IPC or JNI)
            // For now, we'll use ImGui's display size
            ImVec2 display_size = ImGui::GetIO().DisplaySize;
            float screen_width = display_size.x;
            float screen_height = display_size.y;

            // Render each entity in the snapshot
            for (uint32_t i = 0; i < snapshot->count; i++) {
                const EntitySnapshot& entity = snapshot->entities[i];

                // Skip inactive entities
                if (!entity.isActive) {
                    continue;
                }

                // Skip local player if desired (optional)
                if (entity.isLocalPlayer) {
                    continue;
                }

                // Convert world position to screen position
                float screenX, screenY;
                if (!WorldToScreen(entity.posX, entity.posY, entity.posZ, screenX, screenY)) {
                    // If W2S fails, skip this entity
                    continue;
                }

                // Convert head position to screen
                float headScreenX, headScreenY;
                if (!WorldToScreen(entity.headX, entity.headY, entity.headZ, headScreenX, headScreenY)) {
                    continue;
                }

                // Calculate height for box
                float boxHeight = std::abs(headScreenY - screenY);
                float boxWidth = boxHeight * 0.5f; // Approximate width

                // Calculate box coordinates
                float boxLeft = screenX - boxWidth * 0.5f;
                float boxTop = headScreenY;
                float boxRight = screenX + boxWidth * 0.5f;
                float boxBottom = screenY;

                // Determine color based on team (0 = enemy, 1+ = teammate)
                ImU32 boxColor = (entity.teamId == 0) ? IM_COL32(255, 0, 0, 200) : IM_COL32(0, 255, 0, 200);
                ImU32 healthColor;

                // Health bar color (green to red based on health %)
                float healthPercent = static_cast<float>(entity.health) / static_cast<float>(entity.maxHealth);
                if (healthPercent > 0.6f) {
                    healthColor = IM_COL32(0, 255, 0, 200);
                } else if (healthPercent > 0.3f) {
                    healthColor = IM_COL32(255, 255, 0, 200);
                } else {
                    healthColor = IM_COL32(255, 0, 0, 200);
                }

                // Draw bounding box
                draw_list->AddRect(
                    ImVec2(boxLeft, boxTop),
                    ImVec2(boxRight, boxBottom),
                    boxColor,
                    0.0f,  // rounding
                    0,     // rounding corners
                    1.0f   // thickness
                );

                // Draw health bar
                float healthBarWidth = boxWidth * healthPercent;
                float healthBarHeight = 3.0f;
                float healthBarX = boxLeft;
                float healthBarY = boxTop - 5.0f;

                // Health bar background
                draw_list->AddRectFilled(
                    ImVec2(healthBarX, healthBarY),
                    ImVec2(healthBarX + boxWidth, healthBarY + healthBarHeight),
                    IM_COL32(0, 0, 0, 120)
                );

                // Health bar foreground
                draw_list->AddRectFilled(
                    ImVec2(healthBarX, healthBarY),
                    ImVec2(healthBarX + healthBarWidth, healthBarY + healthBarHeight),
                    healthColor
                );

                // Draw player name
                std::string playerName(entity.name);
                draw_list->AddText(
                    ImVec2(screenX - ImGui::CalcTextSize(playerName.c_str()).x * 0.5f, boxBottom + 2.0f),
                    IM_COL32(255, 255, 255, 200),
                    playerName.c_str()
                );

                // Draw distance (simplified - would need local player position for accurate calc)
                char distanceText[32];
                snprintf(distanceText, sizeof(distanceText), "%.0fm", 0.0f); // Placeholder
                draw_list->AddText(
                    ImVec2(boxRight + 2.0f, boxTop),
                    IM_COL32(255, 255, 255, 180),
                    distanceText
                );

                // Draw skeleton (simplified - would need bone positions from snapshot)
                // For now, draw a simple line from head to feet
                draw_list->AddLine(
                    ImVec2(headScreenX, headScreenY),
                    ImVec2(screenX, screenY),
                    IM_COL32(255, 255, 255, 120),
                    1.0f
                );
            }
        }
    }
}