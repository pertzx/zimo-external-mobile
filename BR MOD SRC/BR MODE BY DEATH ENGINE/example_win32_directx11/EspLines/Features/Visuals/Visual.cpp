#include "Visual.hpp"
#include <src/Globals.hpp>
#include <EspLines/Math/WordToScreen.hpp>
#include <EspLines/Math/Vector/Vector2.hpp>
#include <Windows.h>
#include <imgui.h>
#include <cmath>
#include <iostream>
#include <unordered_map>
#include <vector>
#include <algorithm>
#include <cstdio>
#include <chrono>
#include <imgui_internal.h>
#include <src/Fonts/Fonts.hpp>
#include "EspLines\Math\Vector\Vector3.hpp"
#include "NameGun.h"
#include <EspLines/Memory/Memory.hpp>
#include <EspLines/Offsets.hpp>
#include <src/Fonts/FontAwesome.hpp>

struct HealthBarState {
    float AnimatedHealth = 1.0f;
    float LastUpdateTime = 0.0f;
};

static std::unordered_map<int, HealthBarState> healthBarStates;



void DrawVerticalHealthBar(int entityId, short CurrentHealth, short MaxHealth, ImVec2 Position, float TotalHeight) {
    if (MaxHealth <= 0) return;

    ImDrawList* DrawList = ImGui::GetBackgroundDrawList();

    const float BarWidth = 6.0f;
    const float Rounding = 3.0f;
    const float AnimationSpeed = 15.0f;
    const float BorderThickness = 1.0f;

    float HealthPercentage = (std::max)(0.0f, (std::min)(1.0f, static_cast<float>(CurrentHealth) / MaxHealth));

    static std::unordered_map<int, float> animatedHealthValues;
    float& AnimatedHealth = animatedHealthValues[entityId];
    float deltaTime = ImGui::GetIO().DeltaTime;
    float lerpFactor = (std::min)(1.0f, deltaTime * AnimationSpeed);
    AnimatedHealth = ImLerp(AnimatedHealth, HealthPercentage, lerpFactor);

    ImVec2 innerPos = ImVec2(Position.x + BorderThickness, Position.y + BorderThickness);
    float innerWidth = BarWidth - 2 * BorderThickness;
    float innerHeight = TotalHeight - 2 * BorderThickness;
    float FilledBarHeight = innerHeight * AnimatedHealth;

    DrawList->AddRectFilled(
        Position,
        ImVec2(Position.x + BarWidth, Position.y + TotalHeight),
        IM_COL32(40, 40, 40, 130),
        Rounding
    );

    ImU32 HealthBarColor;
    if (HealthPercentage > 0.6f) {
        HealthBarColor = IM_COL32(50, 255, 50, 255);
    }
    else if (HealthPercentage > 0.3f) {
        HealthBarColor = IM_COL32(255, 255, 50, 255);
    }
    else {
        HealthBarColor = IM_COL32(255, 50, 50, 255);
    }

    if (FilledBarHeight > 0.0f) {
        DrawList->AddRectFilled(
            ImVec2(innerPos.x, innerPos.y + innerHeight - FilledBarHeight),
            ImVec2(innerPos.x + innerWidth, innerPos.y + innerHeight),
            HealthBarColor,
            Rounding - BorderThickness,
            ImDrawFlags_RoundCornersBottom
        );
    }

}

void DrawHorizontalHealthBar(int entityId, short CurrentHealth, short MaxHealth, ImVec2 Position, float TotalWidth) {
    if (MaxHealth <= 0) return;

    ImDrawList* DrawList = ImGui::GetBackgroundDrawList();

    const float BarHeight = 6.0f;
    const float Rounding = 3.0f;
    const float AnimationSpeed = 15.0f;
    const float BorderThickness = 1.0f;

    float HealthPercentage = (std::max)(0.0f, (std::min)(1.0f, static_cast<float>(CurrentHealth) / MaxHealth));
    
    static std::unordered_map<int, float> animatedHealthValues;
    float& AnimatedHealth = animatedHealthValues[entityId];
    float deltaTime = ImGui::GetIO().DeltaTime;
    float lerpFactor = (std::min)(1.0f, deltaTime * AnimationSpeed);
    AnimatedHealth = ImLerp(AnimatedHealth, HealthPercentage, lerpFactor);

    ImVec2 innerPos = ImVec2(Position.x + BorderThickness, Position.y + BorderThickness);
    float innerWidth = TotalWidth - 2 * BorderThickness;
    float innerHeight = BarHeight - 2 * BorderThickness;
    float FilledBarWidth = innerWidth * AnimatedHealth;

    DrawList->AddRectFilled(
        Position,
        ImVec2(Position.x + TotalWidth, Position.y + BarHeight),
        IM_COL32(40, 40, 40, 130),
        Rounding
    );

    ImU32 HealthBarColor;
    if (HealthPercentage > 0.6f) {
        HealthBarColor = IM_COL32(50, 255, 50, 255);
    }
    else if (HealthPercentage > 0.3f) {
        HealthBarColor = IM_COL32(255, 255, 50, 255);
    }
    else {
        HealthBarColor = IM_COL32(255, 50, 50, 255);
    }

    if (FilledBarWidth > 0.0f) {
        DrawList->AddRectFilled(
            ImVec2(innerPos.x, innerPos.y),
            ImVec2(innerPos.x + FilledBarWidth, innerPos.y + innerHeight),
            HealthBarColor,
            Rounding - BorderThickness,
            ImDrawFlags_RoundCornersRight
        );
    }

}

void DrawCorneredBox(float x, float y, float w, float h, ImColor color, float thickness) {
    auto drawList = ImGui::GetBackgroundDrawList();

    float lineW = w / 3.0f;
    float lineH = h / 3.0f;

    float shadowPad = 2.0f;
    float shadowThickness = 10.0f;
    ImVec2 shadowOffset = ImVec2(1.5f, 1.5f);
    ImU32 boxColor = ImGui::ColorConvertFloat4ToU32(color);

    auto AddShadowLineH = [&](float x_start, float y_pos, float len) {
        drawList->AddLine(ImVec2(x_start, y_pos), ImVec2(x_start + len, y_pos), color, g_Globals.Visuals.Thickness);
        };

    auto AddShadowLineV = [&](float x_pos, float y_start, float len) {
        drawList->AddLine(ImVec2(x_pos, y_start), ImVec2(x_pos, y_start + len), color, g_Globals.Visuals.Thickness);
        };

    AddShadowLineV(x, y - thickness / 2, lineH);
    AddShadowLineH(x - thickness / 2, y, lineW);
    AddShadowLineH(x + w - lineW, y, lineW);
    AddShadowLineV(x + w, y - thickness / 2, lineH);

    AddShadowLineV(x, y + h - lineH, lineH);
    AddShadowLineH(x - thickness / 2, y + h, lineW);

    AddShadowLineH(x + w - lineW, y + h, lineW);
    AddShadowLineV(x + w, y + h - lineH, lineH);
}

void DrawFullBox(float x, float y, float w, float h, ImColor color, float thickness) {
    auto drawList = ImGui::GetBackgroundDrawList();

    float shadowPad = 2.0f;
    float shadowThickness = 10.0f;
    ImVec2 shadowOffset = ImVec2(1.5f, 1.5f);
    ImU32 boxColor = ImGui::ColorConvertFloat4ToU32(color);

    drawList->AddRect(ImVec2(x, y), ImVec2(x + w, y + h), color, 0.0f, ImDrawFlags_None, g_Globals.Visuals.Thickness);
}

void ESP::PlayerCounter() {
    if (!g_Globals.General.ShowPlayerCounter)
        return;

    // Data::Work e o renderizador são executados no mesmo loop principal.
    // A lista é lida diretamente aqui para não introduzir bloqueio durante o frame.
    const int enemyCount = static_cast<int>(std::count_if(
        g_Globals.EspConfig.Entities.begin(),
        g_Globals.EspConfig.Entities.end(),
        [](const auto& pair) {
            const Player& player = pair.second;
            return !player.IsDead && player.Distance <= g_Globals.Visuals.DistanceEsp;
        }
    ));

    ImGuiIO& io = ImGui::GetIO();
    ImDrawList* drawList = ImGui::GetBackgroundDrawList();

    const float width = 53.0f;
    const float height = 36.0f;
    const float x = (io.DisplaySize.x - width) * 0.5f;
    const float y = 12.0f;
    const ImVec2 counterMin = ImVec2(x, y);
    const ImVec2 counterMax = ImVec2(x + width, y + height);

    drawList->AddRectFilled(counterMin, counterMax, IM_COL32(8, 12, 16, 235), 8.0f);
    // Barrinha fina e quase da altura total, como na referência.
    drawList->AddRectFilled(
        ImVec2(x + 3.0f, y + 5.5f),
        ImVec2(x + 6.0f, y + height - 5.5f),
        IM_COL32(10, 145, 245, 255),
        1.0f
    );

    // O bonequinho da referência é o glyph de usuário do FontAwesome.
    // Só usamos a fonte quando ela foi carregada; isso evita o crash anterior.
    const ImU32 playerIconColor = IM_COL32(10, 145, 245, 255);
    ImFont* playerIconFont = FWork::Fonts::FontAwesomeSolid;
    if (playerIconFont != nullptr) {
        const char* playerIcon = ICON_FA_USER;
        const float iconSize = 14.0f;
        // Coordenada fixa: o menu pode trocar a fonte ativa, mas o boneco não sobe.
        drawList->AddText(
            playerIconFont,
            iconSize,
            ImVec2(x + 12.5f, y + 10.0f),
            playerIconColor,
            playerIcon
        );
    }

    char countText[16];
    snprintf(countText, sizeof(countText), "%d", enemyCount);
    // Usar a mesma fonte regular do menu evita o número escuro do overlay.
    ImFont* counterFont = FWork::Fonts::InterRegular
        ? FWork::Fonts::InterRegular
        : ImGui::GetFont();
    // Tamanho fixo para a fonte; a quantidade de inimigos não altera a escala.
    const float counterFontSize = 16.0f;
    const ImVec2 countSize = counterFont->CalcTextSizeA(counterFontSize, 1000.0f, 0.0f, countText);
    const ImU32 counterTextColor = IM_COL32(242, 237, 244, 255);
    drawList->AddText(
        counterFont,
        counterFontSize,
        ImVec2(x + width - countSize.x - 10.0f, y + 9.0f),
        counterTextColor,
        countText
    );
}

void ESP::CounterTime() {
    if (!g_Globals.General.ShowCounterTimeCS)
        return;

    using Clock = std::chrono::steady_clock;
    static uint32_t trackedMatch = 0;
    static Clock::time_point matchStart = Clock::now();

    const uint32_t currentMatch = g_Globals.EspConfig.LastMatchId;
    if (currentMatch == 0) {
        trackedMatch = 0;
        return;
    }
    if (trackedMatch != currentMatch) {
        trackedMatch = currentMatch;
        matchStart = Clock::now();
    }

    const auto elapsedSeconds = std::chrono::duration_cast<std::chrono::seconds>(
        Clock::now() - matchStart
    ).count();
    const int totalSeconds = static_cast<int>((std::max)(0LL, elapsedSeconds));
    const int minutes = totalSeconds / 60;
    const int seconds = totalSeconds % 60;

    char timeText[16];
    snprintf(timeText, sizeof(timeText), "%02d:%02d", minutes, seconds);

    ImGuiIO& io = ImGui::GetIO();
    ImDrawList* drawList = ImGui::GetBackgroundDrawList();
    const float width = 79.0f;
    const float height = 34.0f;
    const float x = 12.0f;
    const float y = 55.0f;
    const ImVec2 boxMin = ImVec2(x, y);
    const ImVec2 boxMax = ImVec2(x + width, y + height);

    drawList->AddRectFilled(boxMin, boxMax, IM_COL32(8, 12, 16, 235), 8.0f);

    drawList->AddRectFilled(
        ImVec2(x + 3.0f, y + 5.5f),
        ImVec2(x + 6.0f, y + height - 5.5f),
        IM_COL32(10, 145, 245, 255),
        1.0f
    );

    const ImU32 blue = IM_COL32(10, 145, 245, 255);
    ImFont* iconFont = FWork::Fonts::FontAwesomeSolid;
    if (iconFont != nullptr) {
        drawList->AddText(
            iconFont,
            15.0f,
            ImVec2(x + 12.0f, y + 9.0f),
            blue,
            ICON_FA_CLOCK
        );
    }

    ImFont* timeFont = FWork::Fonts::InterRegular
        ? FWork::Fonts::InterRegular
        : ImGui::GetFont();
    const float timeFontSize = 15.0f;
    const ImVec2 timeSize = timeFont->CalcTextSizeA(timeFontSize, 1000.0f, 0.0f, timeText);
    drawList->AddText(
        timeFont,
        timeFontSize,
        ImVec2(x + width - timeSize.x - 10.0f, y + 8.0f),
        IM_COL32(242, 237, 244, 255),
        timeText
    );
}

void ESP::Players() {
    if (!g_Globals.EspConfig.Matrix) return;

    for (auto& [entityID, player] : g_Globals.EspConfig.Entities) {

        // A existência de nome não é requisito para desenhar uma entidade;
        // bots frequentemente não fornecem nome válido.
        if (player.IsDead) {
            continue;
        }

        float dist = player.Distance;
        if (dist > g_Globals.Visuals.DistanceEsp) {
            continue;
        }

        auto IsOffScreen = [](const ImVec2& pos) { return pos.x < 1 || pos.y < 1; };
        const std::vector<ImVec2> Bones = {
            W2S::WorldToScreenImVec2(g_Globals.EspConfig.ViewMatrix, player.Head, g_Globals.EspConfig.Width, g_Globals.EspConfig.Height),
            W2S::WorldToScreenImVec2(g_Globals.EspConfig.ViewMatrix, player.Neck, g_Globals.EspConfig.Width, g_Globals.EspConfig.Height),
            W2S::WorldToScreenImVec2(g_Globals.EspConfig.ViewMatrix, player.LeftShoulder, g_Globals.EspConfig.Width, g_Globals.EspConfig.Height),
            W2S::WorldToScreenImVec2(g_Globals.EspConfig.ViewMatrix, player.RightShoulder, g_Globals.EspConfig.Width, g_Globals.EspConfig.Height),
            W2S::WorldToScreenImVec2(g_Globals.EspConfig.ViewMatrix, player.LeftElbow, g_Globals.EspConfig.Width, g_Globals.EspConfig.Height),
            W2S::WorldToScreenImVec2(g_Globals.EspConfig.ViewMatrix, player.RightElbow, g_Globals.EspConfig.Width, g_Globals.EspConfig.Height),
            W2S::WorldToScreenImVec2(g_Globals.EspConfig.ViewMatrix, player.LeftWrist, g_Globals.EspConfig.Width, g_Globals.EspConfig.Height),
            W2S::WorldToScreenImVec2(g_Globals.EspConfig.ViewMatrix, player.RightWrist, g_Globals.EspConfig.Width, g_Globals.EspConfig.Height),
            W2S::WorldToScreenImVec2(g_Globals.EspConfig.ViewMatrix, player.Hip, g_Globals.EspConfig.Width, g_Globals.EspConfig.Height),
            W2S::WorldToScreenImVec2(g_Globals.EspConfig.ViewMatrix, player.LeftAnkle, g_Globals.EspConfig.Width, g_Globals.EspConfig.Height),
            W2S::WorldToScreenImVec2(g_Globals.EspConfig.ViewMatrix, player.RightAnkle, g_Globals.EspConfig.Width, g_Globals.EspConfig.Height),
            W2S::WorldToScreenImVec2(g_Globals.EspConfig.ViewMatrix, player.Root, g_Globals.EspConfig.Width, g_Globals.EspConfig.Height)
        };

        if (player.IsKnocked) {
            if (IsOffScreen(Bones[0]) && IsOffScreen(Bones[11])) {
                continue;
            }
        }
        else if (std::any_of(Bones.begin(), Bones.end(), IsOffScreen)) {
            continue;
        }
        const ImVec2& headPos = Bones[0];
        const ImVec2& neckPos = Bones[1];
        const ImVec2& leftShoulderPos = Bones[2];
        const ImVec2& rightShoulderPos = Bones[3];
        const ImVec2& leftElbowPos = Bones[4];
        const ImVec2& rightElbowPos = Bones[5];
        const ImVec2& leftWristPos = Bones[6];
        const ImVec2& rightWristPos = Bones[7];
        const ImVec2& hipPos = Bones[8];
        const ImVec2& leftAnklePos = Bones[9];
        const ImVec2& rightAnklePos = Bones[10];
        const ImVec2& rootPos = Bones[11];

        Vector3 ankleMidPoint = (player.LeftAnkle + player.RightAnkle) * 0.5f;
        Vector3 hipDirection = (player.LeftAnkle - player.RightAnkle).Normalized(true);
        Vector3 leftHipDirection = hipDirection;
        Vector3 rightHipDirection = -hipDirection;
        float ankleDistance = (player.LeftAnkle - player.RightAnkle).Magnitude(true);
        float baseHipWidth = ankleDistance * g_Globals.Visuals.HipWidthScale;
        Vector3 verticalPositionLeft = Vector3::Lerp(player.LeftAnkle, player.Hip, g_Globals.Visuals.LeftHipHeightOffset);
        Vector3 verticalPositionRight = Vector3::Lerp(player.RightAnkle, player.Hip, g_Globals.Visuals.RightHipHeightOffset);
        Vector3 leftHip = verticalPositionLeft + (leftHipDirection * (baseHipWidth * g_Globals.Visuals.HipWidthOffset));
        Vector3 rightHip = verticalPositionRight + (rightHipDirection * (baseHipWidth * g_Globals.Visuals.HipWidthOffset));
        ImVec2 leftHipPosition = W2S::WorldToScreenImVec2(g_Globals.EspConfig.ViewMatrix, leftHip, g_Globals.EspConfig.Width, g_Globals.EspConfig.Height);
        ImVec2 rightHipPosition = W2S::WorldToScreenImVec2(g_Globals.EspConfig.ViewMatrix, rightHip, g_Globals.EspConfig.Width, g_Globals.EspConfig.Height);

        float boxHeight;
        float boxWidth;

        if (headPos.x != 0 && headPos.y != 0 && rootPos.x != 0 && rootPos.y != 0) {
            boxHeight = fabsf(headPos.y - rootPos.y);
            boxWidth = boxHeight * 0.65f;
        }
        else {
            boxHeight = 50.0f;
            boxWidth = 30.0f;
        }

        bool isTargeted = false;
        if (g_Globals.Visuals.Alvo) {
            ImGuiIO& io = ImGui::GetIO();
            float cx = io.DisplaySize.x * 0.5f;
            float cy = io.DisplaySize.y * 0.5f;
            float bx1 = headPos.x - (boxWidth / 2);
            float by1 = headPos.y;
            float bx2 = headPos.x + (boxWidth / 2);
            float by2 = headPos.y + boxHeight;
            float margin = 15.0f;
            if (cx >= bx1 - margin && cx <= bx2 + margin && cy >= by1 - margin && cy <= by2 + margin) {
                isTargeted = true;
            }
        }

        if (g_Globals.Visuals.Lines) {
            ImColor targetColor = ImColor(
                g_Globals.Visuals.AlvoColor[0],
                g_Globals.Visuals.AlvoColor[1],
                g_Globals.Visuals.AlvoColor[2],
                g_Globals.Visuals.AlvoColor[3]
            );

            ImColor snapLineColor = isTargeted ? targetColor : ImColor(
                g_Globals.Visuals.LinesColor[0],
                g_Globals.Visuals.LinesColor[1],
                g_Globals.Visuals.LinesColor[2],
                g_Globals.Visuals.LinesColor[3]
            );

            ImColor KnockedColor = ImColor(
                g_Globals.Visuals.KnockedColor[0],
                g_Globals.Visuals.KnockedColor[1],
                g_Globals.Visuals.KnockedColor[2],
                g_Globals.Visuals.KnockedColor[3]
            );
            ImColor lineColor = player.IsKnocked ? ImColor(KnockedColor) : snapLineColor;
            float thickness = 1.5f;
            float glowThickness = thickness * 1.5f;
            ImColor glowColor = ImColor(lineColor.Value.x * 0.5f, lineColor.Value.y * 0.5f, lineColor.Value.z * 0.5f, 0.3f);

            Vector2 ScreenTop(g_Globals.EspConfig.Width / 2, 0);
            Vector2 ScreenBottom(g_Globals.EspConfig.Width / 2, g_Globals.EspConfig.Height);

            auto AddGlowLine = [&](float x1, float y1, float x2, float y2) {
                auto drawList = ImGui::GetBackgroundDrawList();
                drawList->AddLine(
                    ImVec2(x1, y1),
                    ImVec2(x2, y2),
                    lineColor, g_Globals.Visuals.Thickness);
                };

            switch (g_Globals.Visuals.EspLines) {
            case 0:
                AddGlowLine(rootPos.x, rootPos.y, ScreenTop.X, ScreenTop.Y);
                break;
            case 1:
                AddGlowLine(ScreenBottom.X, ScreenBottom.Y, rootPos.x, rootPos.y);
                break;
            }
        }

        if (g_Globals.Visuals.Skeleton) {
            ImColor SkeletonColor = isTargeted ? ImColor(
                g_Globals.Visuals.AlvoColor[0],
                g_Globals.Visuals.AlvoColor[1],
                g_Globals.Visuals.AlvoColor[2],
                g_Globals.Visuals.AlvoColor[3]
            ) : ImColor(
                g_Globals.Visuals.SkeletonColor[0],
                g_Globals.Visuals.SkeletonColor[1],
                g_Globals.Visuals.SkeletonColor[2],
                g_Globals.Visuals.SkeletonColor[3]
            );

            struct SkeletonDrawer {
                ImDrawList* drawList;
                ImColor color;
                float thickness;

                void Bone(const ImVec2& from, const ImVec2& to) const {
                    drawList->AddLine(from, to, color, g_Globals.Visuals.Thickness);
                }

                void Head(const ImVec2& center, float radius) const {
                    drawList->AddCircle(center, radius, color, 0, g_Globals.Visuals.Thickness);
                }
            };

            ImColor IsKnockedColor = player.IsKnocked ? ImColor(1.f, 0.f, 0.f, 1.f) : SkeletonColor;

            float headRadius = std::clamp(100.0f / player.Distance, 0.5f, 1.5f) * 2.0f;

            SkeletonDrawer skeleton{
                ImGui::GetBackgroundDrawList(),
                IsKnockedColor,
                1.5f
            };

            skeleton.Head(headPos, headRadius);
            skeleton.Bone(headPos, neckPos);
            skeleton.Bone(neckPos, leftShoulderPos);
            skeleton.Bone(neckPos, rightShoulderPos);
            skeleton.Bone(leftShoulderPos, leftElbowPos);
            skeleton.Bone(rightShoulderPos, rightElbowPos);
            skeleton.Bone(leftElbowPos, leftWristPos);
            skeleton.Bone(rightElbowPos, rightWristPos);
            skeleton.Bone(neckPos, hipPos);
            skeleton.Bone(hipPos, leftHipPosition);
            skeleton.Bone(leftHipPosition, leftAnklePos);

            skeleton.Bone(hipPos, rightHipPosition);
            skeleton.Bone(rightHipPosition, rightAnklePos);

            if (g_Globals.Visuals.Debug) {
                ImGui::GetBackgroundDrawList()->AddCircle(leftHipPosition, 3.0f, IM_COL32(255, 0, 0, 255));
                ImGui::GetBackgroundDrawList()->AddCircle(rightHipPosition, 3.0f, IM_COL32(0, 255, 0, 0));
                ImGui::GetBackgroundDrawList()->AddCircle(hipPos, 3.0f, IM_COL32(0, 0, 255, 255));
            }
        }

        if (g_Globals.Visuals.ESPHealthTEXT) {
            auto drawList = ImGui::GetBackgroundDrawList();
            float fontScale = g_Globals.Visuals.TextSize / 15.0f; // base 15

            ImGui::PushFont(FWork::Fonts::InterRegular);
            const char* healthLabel = "Health:";
            ImVec2 labelSize = ImGui::CalcTextSize(healthLabel) * fontScale;

            char healthValue[16];
            snprintf(healthValue, sizeof(healthValue), "%d", player.Health);
            ImVec2 valueSize = ImGui::CalcTextSize(healthValue) * fontScale;

            float totalWidth = labelSize.x + valueSize.x;
            float healthTextOffset = (g_Globals.Visuals.HealthBar && g_Globals.Visuals.players_healthbar == 4) ? 13.5f : 8.0f; // Move down if health bar is below
            ImVec2 combinedPosition = ImVec2(rootPos.x - totalWidth / 2, rootPos.y + healthTextOffset - 5);

            ImColor shadowColor = ImColor(0.0f, 0.0f, 0.0f, 0.3f);
            ImColor whiteColor = ImColor(1.0f, 1.0f, 1.0f, 1.0f);

            drawList->AddText(ImVec2(combinedPosition.x + 1, combinedPosition.y + 1), shadowColor, healthLabel);
            drawList->AddText(combinedPosition, whiteColor, healthLabel);

            float healthPercentage = std::clamp(static_cast<float>(player.Health) / 200.0f, 0.0f, 1.0f);
            ImVec4 greenColor = ImVec4(0.0f, 1.0f, 0.0f, 1.0f);
            ImVec4 yellowColor = ImVec4(1.0f, 1.0f, 0.0f, 1.0f);
            ImVec4 redColor = ImVec4(1.0f, 0.0f, 0.0f, 1.0f);

            ImVec4 healthBarColor;
            if (healthPercentage > 0.5f) {
                healthBarColor = ImLerp(greenColor, yellowColor, (1.0f - healthPercentage) * 2.0f);
            }
            else {
                healthBarColor = ImLerp(yellowColor, redColor, (0.5f - healthPercentage) * 2.0f);
            }
            ImColor health = player.IsKnocked ? ImColor(1.0f, 0.0f, 0.0f, 1.0f) : healthBarColor;

            ImVec2 valuePosition = ImVec2(combinedPosition.x + labelSize.x, combinedPosition.y);

            ImGui::PushFont(FWork::Fonts::InterSemiBold);
            drawList->AddText(ImVec2(valuePosition.x + 1, valuePosition.y + 1), shadowColor, healthValue);
            drawList->AddText(valuePosition, health, healthValue);
            ImGui::PopFont();

            ImGui::PopFont();
        }

        if (g_Globals.Visuals.ESPWeapon) {
            auto drawList = ImGui::GetBackgroundDrawList();
            Namegun::Init();

            std::string TextAndIcon;
            bool isIcon = false;

            if (g_Globals.Visuals.ESPWeaponIcon && Namegun::HasIcon(player.WeaponID)) {
                TextAndIcon = Namegun::GetGunIcon(player.WeaponID);
                isIcon = true;
            }
            else {
                TextAndIcon = Namegun::GetGunName(player.WeaponID);
            }

            ImVec2 textSize = ImGui::CalcTextSize(TextAndIcon.c_str());

            float weaponOffset = (g_Globals.Visuals.HealthBar && g_Globals.Visuals.players_healthbar == 4) ? 15.0f : 20.0f; 
            if (g_Globals.Visuals.ESPHealthTEXT) {
                weaponOffset += 10.0f;
            }

            if (isIcon && g_Globals.Visuals.ESPWeaponIcon) {
                weaponOffset += 13.0f;
            }

            float xOffset = isIcon ? -13.0f : -3.0f;
            ImVec2 textPos(rootPos.x - (textSize.x * 0.5f) + xOffset, rootPos.y + weaponOffset);

            ImColor WeaponColor = ImColor(
                g_Globals.Visuals.ESPWeaponColor[0], g_Globals.Visuals.ESPWeaponColor[1],
                g_Globals.Visuals.ESPWeaponColor[2], g_Globals.Visuals.ESPWeaponColor[3]);
            ImFont* BestFont = isIcon ? FWork::Fonts::IconWeapon : FWork::Fonts::InterRegular;
            ImGui::PushFont(BestFont);
            drawList->AddText(textPos, WeaponColor, TextAndIcon.c_str());
            ImGui::PopFont();
        }
        if (g_Globals.Visuals.Watermark) {
        }

        if (g_Globals.Visuals.HealthBar) {
            DrawVerticalHealthBar(entityID, player.Health, 200, ImVec2(headPos.x - (boxWidth / 2) - 8, headPos.y), boxHeight);
        }

        if (g_Globals.Visuals.FilledBox) {
            float padding = 1.0f;
            ImColor fillColor = ImColor(
                g_Globals.Visuals.Filledboxcolor[0],
                g_Globals.Visuals.Filledboxcolor[1],
                g_Globals.Visuals.Filledboxcolor[2],
                g_Globals.Visuals.Filledboxcolor[3]
            );

            float filledBoxX = headPos.x - (boxWidth / 2) + padding;
            float filledBoxY = headPos.y + padding;
            float filledBoxWidth = boxWidth - (2 * padding);
            float filledBoxHeight = boxHeight - (2 * padding);

            ImGui::GetBackgroundDrawList()->AddRectFilled(
                ImVec2(filledBoxX, filledBoxY),
                ImVec2(filledBoxX + filledBoxWidth, filledBoxY + filledBoxHeight),
                fillColor);
        }

        if (g_Globals.Visuals.Box) {
            ImColor boxColor = isTargeted ? ImColor(
                g_Globals.Visuals.AlvoColor[0],
                g_Globals.Visuals.AlvoColor[1],
                g_Globals.Visuals.AlvoColor[2],
                g_Globals.Visuals.AlvoColor[3]
            ) : ImColor(g_Globals.Visuals.BoxColor[0],
                g_Globals.Visuals.BoxColor[1],
                g_Globals.Visuals.BoxColor[2],
                g_Globals.Visuals.BoxColor[3]);

            ImColor box = player.IsKnocked ? ImColor(1.f, 0.f, 0.f, 1.f) : boxColor;
            DrawFullBox(headPos.x - (boxWidth / 2), headPos.y, boxWidth, boxHeight, boxColor, g_Globals.Visuals.Thickness);
        }

        if (g_Globals.Visuals.Name) {
            ImColor NameColor = isTargeted ? ImColor(
                g_Globals.Visuals.AlvoColor[0],
                g_Globals.Visuals.AlvoColor[1],
                g_Globals.Visuals.AlvoColor[2],
                g_Globals.Visuals.AlvoColor[3]
            ) : ImColor(
                g_Globals.Visuals.NameColor[0],
                g_Globals.Visuals.NameColor[1],
                g_Globals.Visuals.NameColor[2],
                g_Globals.Visuals.NameColor[3]
            );

            ImColor DistColor = isTargeted ? ImColor(
                g_Globals.Visuals.AlvoColor[0],
                g_Globals.Visuals.AlvoColor[1],
                g_Globals.Visuals.AlvoColor[2],
                g_Globals.Visuals.AlvoColor[3]
            ) : ImColor(
                g_Globals.Visuals.DistColor[0],
                g_Globals.Visuals.DistColor[1],
                g_Globals.Visuals.DistColor[2],
                g_Globals.Visuals.DistColor[3]
            );

            ImColor shadowColor = ImColor(0.0f, 0.0f, 0.0f, 0.3f);

            ImGui::PushFont(FWork::Fonts::InterSemiBold);

            float fontScale = g_Globals.Visuals.TextSize / 15.0f;
            float scaleFactor = std::clamp(1.5f - (dist * 0.001f), 0.8f, 1.3f);

            std::string nameText = (!player.Name.empty()) ? player.Name : "BOT";

            float distMeters = dist;
            char distBuf[32];
            snprintf(distBuf, sizeof(distBuf), "%.2f m", distMeters);
            std::string distanceText = g_Globals.Visuals.Distance ? distBuf : "";

            ImVec2 nameSize = ImGui::CalcTextSize(nameText.c_str()) * fontScale;
            ImVec2 textSizeDistance = ImGui::CalcTextSize(distanceText.c_str()) * fontScale;

            float nameOffset = 16.0f;

            ImVec2 namePos(headPos.x - (nameSize.x / 2), headPos.y - nameOffset);
            ImVec2 textPosDistance(headPos.x - (textSizeDistance.x / 2), headPos.y + boxHeight + 4.0f);

            auto* drawList = ImGui::GetBackgroundDrawList();

            drawList->AddText(ImGui::GetFont(), ImGui::GetFontSize()* fontScale,
                ImVec2(namePos.x + 1, namePos.y + 1), shadowColor, nameText.c_str());

            drawList->AddText(ImGui::GetFont(), ImGui::GetFontSize()* fontScale,
                namePos, NameColor, nameText.c_str());

            if (g_Globals.Visuals.Distance) {
                ImColor WhiteDist = ImColor(1.0f, 1.0f, 1.0f, 1.0f);
                drawList->AddText(ImGui::GetFont(), ImGui::GetFontSize() * fontScale,
                    ImVec2(textPosDistance.x + 1, textPosDistance.y + 1), shadowColor, distanceText.c_str());

                drawList->AddText(ImGui::GetFont(), ImGui::GetFontSize() * fontScale,
                    textPosDistance, WhiteDist, distanceText.c_str());
            }

            ImGui::PopFont();
        }

        if (g_Globals.Visuals.Enemy && !g_Globals.General.ShowPlayerCounter) {
        }
    };

}

namespace {
    const char* BindKeyName(int vk)
    {
        if (vk == 0) return "None";

        switch (vk)
        {
        case VK_LBUTTON:   return "LButton";
        case VK_RBUTTON:   return "RButton";
        case VK_MBUTTON:   return "MButton";
        case VK_XBUTTON1:  return "XButton1";
        case VK_XBUTTON2:  return "XButton2";
        case VK_BACK:      return "Backspace";
        case VK_TAB:       return "Tab";
        case VK_RETURN:    return "Enter";
        case VK_ESCAPE:    return "Esc";
        case VK_SPACE:     return "Space";
        case VK_PRIOR:     return "Page Up";
        case VK_NEXT:      return "Page Down";
        case VK_END:       return "End";
        case VK_HOME:      return "Home";
        case VK_LEFT:      return "Left";
        case VK_UP:        return "Up";
        case VK_RIGHT:     return "Right";
        case VK_DOWN:      return "Down";
        case VK_INSERT:    return "Insert";
        case VK_DELETE:    return "Delete";
        case VK_SHIFT:     return "Shift";
        case VK_LSHIFT:    return "LShift";
        case VK_RSHIFT:    return "RShift";
        case VK_CONTROL:   return "Ctrl";
        case VK_LCONTROL:  return "LCtrl";
        case VK_RCONTROL:  return "RCtrl";
        case VK_MENU:      return "Alt";
        case VK_LMENU:     return "LAlt";
        case VK_RMENU:     return "RAlt";
        case VK_CAPITAL:   return "Caps Lock";
        case VK_NUMLOCK:   return "Num Lock";
        case VK_SCROLL:    return "Scroll Lock";
        case VK_F1:        return "F1";
        case VK_F2:        return "F2";
        case VK_F3:        return "F3";
        case VK_F4:        return "F4";
        case VK_F5:        return "F5";
        case VK_F6:        return "F6";
        case VK_F7:        return "F7";
        case VK_F8:        return "F8";
        case VK_F9:        return "F9";
        case VK_F10:       return "F10";
        case VK_F11:       return "F11";
        case VK_F12:       return "F12";
        default:
            if (vk >= 'A' && vk <= 'Z')
            {
                static char letter[2]{};
                letter[0] = static_cast<char>(vk);
                letter[1] = '\0';
                return letter;
            }
            if (vk >= '0' && vk <= '9')
            {
                static char digit[2]{};
                digit[0] = static_cast<char>(vk);
                digit[1] = '\0';
                return digit;
            }
            static char buf[16];
            snprintf(buf, sizeof(buf), "VK 0x%02X", vk);
            return buf;
        }
    }
}

void ESP::KeybindsPanel() {
    if (!g_Globals.General.ShowKeybinds)
        return;

    struct BindItem { const char* name; int vk; };
    const BindItem items[] = {
        { "Mark TP",       g_Globals.Exploits.TeleportMarkBind },
        { "Speed Timer",   g_Globals.Exploits.SpeedTimerBind },
        { "Magnet Lite",   g_Globals.Exploits.PullEnemyBind },
        { "Under Cam",     g_Globals.Exploits.UnderCamBind }
    };

    int shownCount = 0;
    for (const auto& item : items) {
        if (item.vk != 0)
            ++shownCount;
    }
    if (shownCount == 0)
        return;

    ImGuiIO& io = ImGui::GetIO();
    ImDrawList* drawList = ImGui::GetBackgroundDrawList();
    ImFont* rowFont = FWork::Fonts::InterRegular
        ? FWork::Fonts::InterRegular
        : ImGui::GetFont();

    const float panelWidth = 175.0f;
    const float rowHeight = 23.0f;
    const float sidePadding = 10.0f;
    const float topPadding = 0.0f;
    const float panelHeight = shownCount * rowHeight;
    const ImVec2 panelPos(
        io.DisplaySize.x - panelWidth - 16.0f,
        (io.DisplaySize.y - panelHeight) * 0.5f
    );
    const ImVec2 panelEnd(panelPos.x + panelWidth, panelPos.y + panelHeight);

    drawList->AddRectFilled(panelPos, panelEnd, IM_COL32(5, 12, 14, 242), 12.0f);
    drawList->AddRectFilled(
        ImVec2(panelPos.x + 6.0f, panelPos.y + 6.0f),
        ImVec2(panelPos.x + 8.0f, panelEnd.y - 6.0f),
        IM_COL32(10, 145, 245, 255),
                2.0f
    );
    const ImU32 rowColor = IM_COL32(145, 156, 158, 235);
    const ImU32 keyColor = IM_COL32(128, 140, 142, 235);
    float rowY = panelPos.y + 5.0f;
    for (const auto& item : items) {
        if (item.vk == 0)
            continue;

        const char* keyName = BindKeyName(item.vk);
        const float fontSize = 12.0f;
        const ImVec2 keySize = rowFont->CalcTextSizeA(fontSize, 1000.0f, 0.0f, keyName);
        drawList->AddText(
            rowFont,
            fontSize,
            ImVec2(panelPos.x + sidePadding + 10.0f, rowY),
            rowColor,
            item.name
        );
        drawList->AddText(
            rowFont,
            fontSize,
            ImVec2(panelEnd.x - sidePadding - keySize.x, rowY),
            keyColor,
            keyName
        );
        rowY += rowHeight;
    }
}