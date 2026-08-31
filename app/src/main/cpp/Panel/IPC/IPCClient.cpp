#include "IPCClient.hpp"
#include "Globals.hpp"  // <-- ADICIONAR ESTA LINHA
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <android/log.h>
#include <cstring>
#include <errno.h>

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "StormIPC", __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "StormIPC", __VA_ARGS__)

static int g_Socket = -1;
static IPC_GAME_STATE g_LastState{};
static IPC_CONFIG_STATE g_LastConfig{};
static uint32_t g_ConfigSeq = 0;

namespace IPCClient {

bool Connect(const char* socketPath) {
    g_Socket = socket(AF_UNIX, SOCK_STREAM, 0);
    if (g_Socket < 0) {
        LOGE("Falha ao criar socket: %s", strerror(errno));
        return false;
    }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, socketPath, sizeof(addr.sun_path) - 1);

    if (connect(g_Socket, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        LOGE("Falha ao conectar ao daemon: %s", strerror(errno));
        close(g_Socket);
        g_Socket = -1;
        return false;
    }

    LOGI("Conectado ao daemon IPC");
    return true;
}

void Disconnect() {
    if (g_Socket >= 0) {
        close(g_Socket);
        g_Socket = -1;
    }
}

bool IsConnected() {
    return g_Socket >= 0;
}

void SyncConfigToDaemon() {
    if (g_Socket < 0) return;

    IPC_CONFIG_STATE state;
    memset(&state, 0, sizeof(state));
    state.Magic = IPC_MAGIC_STATE;
    state.Seq = ++g_ConfigSeq;

    state.AimBot_Enabled = g_Globals.AimBot.Enabled;
    state.AimBot_Fov = g_Globals.AimBot.Fov;
    state.AimBot_MaxDistance = g_Globals.AimBot.MaxDistance;
    state.AimBot_Target = g_Globals.AimBot.Target;
    state.AimBot_IgnoreBots = g_Globals.AimBot.IgnoreBots;
    state.AimBot_IgnoreKnocked = g_Globals.AimBot.IgnoreKnocked;
    state.AimBot_VisibleCheck = g_Globals.AimBot.VisibleCheck;
    state.AimBot_Pull = g_Globals.AimBot.aimmagnect;
    state.AimBot_Ghost = g_Globals.AimBot.ghost;
    state.AimBot_ShowFov = g_Globals.Misc.Screen.ShowAimbotFov;
    state.AimBot_KeyBind = g_Globals.AimBot.KeyBind;

    state.Silent_Enabled = g_Globals.Silent.Enabled;
    state.Silent_Fov = g_Globals.Silent.Fov;
    state.Silent_MaxDistance = g_Globals.Silent.MaxDistance;
    state.Silent_ShowFov = g_Globals.Misc.Screen.ShowSilentFov;

    state.ESP_Enemy = g_Globals.Visuals.ESP.Enemy;
    state.ESP_ShowTeam = g_Globals.Visuals.ESP.ShowTeam;
    state.ESP_Watermark = g_Globals.Visuals.ESP.Watermark;
    state.ESP_Box = g_Globals.Visuals.ESP.Box;
    state.ESP_BoxFilled = g_Globals.Visuals.ESP.BoxFilled;
    state.ESP_ShowName = g_Globals.Visuals.ESP.ShowName;
    state.ESP_HealthBar = g_Globals.Visuals.ESP.HealthBar;
    state.ESP_Distance = g_Globals.Visuals.ESP.Distance;
    state.ESP_Skeleton = g_Globals.Visuals.ESP.Skeleton;
    state.ESP_Weapon = g_Globals.Visuals.ESP.Weapon;
    state.ESP_SnapLines = g_Globals.Visuals.ESP.SnapLines;
    state.ESP_RenderDistance = g_Globals.Visuals.ESP.RenderDistance;
    state.ESP_Thickness = g_Globals.Visuals.ESP.Thickness;
    state.ESP_TextSize = g_Globals.Visuals.ESP.TextSize;
    state.ESP_BoxStyle = g_Globals.Visuals.ESP.BoxStyle;
    state.ESP_HealthBarStyle = g_Globals.Visuals.ESP.HealthBarStyle;
    state.ESP_WeaponStyle = g_Globals.Visuals.ESP.WeaponStyle;
    state.ESP_SnapLinesPos = g_Globals.Visuals.ESP.SnapLinesPos;

    state.Chams_Enabled = g_Globals.Visuals.Chams.Enabled;
    state.Chams_AggressiveMode = g_Globals.Visuals.Chams.AggressiveMode;

    state.Exp_AimLock2x = g_Globals.Misc.Exploits.LocalPlayer.AimLock2x;
    state.Exp_AimbotAwm = g_Globals.Misc.Exploits.LocalPlayer.AimbotAwm;
    state.Exp_NoRecoil = g_Globals.Misc.Exploits.LocalPlayer.NoRecoil;
    state.Exp_RecoilControl = g_Globals.Misc.Exploits.LocalPlayer.RecoilControl;
    state.Exp_FastMedkit = g_Globals.Misc.Exploits.LocalPlayer.FastMedkit;
    state.Exp_TelaParada = g_Globals.Misc.Exploits.LocalPlayer.telaparada;
    state.Exp_AtributarArma = g_Globals.Misc.Exploits.LocalPlayer.AtributarArma;
    state.Exp_AtributarArmaLevel = g_Globals.Misc.Exploits.LocalPlayer.AtributarArmaLevel;
    state.Exp_Aimlock = g_Globals.Misc.Exploits.LocalPlayer.Aimlock;
    state.Exp_MoreDamage = g_Globals.Misc.Exploits.LocalPlayer.MoreDamage;
    state.Exp_FireDelay = g_Globals.Misc.Exploits.LocalPlayer.FireDelay;
    state.Exp_BugarPixel = g_Globals.Misc.Exploits.LocalPlayer.BugarPixel;
    state.Exp_Precision = g_Globals.Misc.Exploits.LocalPlayer.Precision;
    state.Exp_BackJump = g_Globals.Misc.Exploits.LocalPlayer.BackJump;
    state.Exp_SocoLonge = g_Globals.Misc.Exploits.LocalPlayer.SocoLonge;
    state.Exp_SpinBot = g_Globals.Misc.Exploits.LocalPlayer.SpinBot;
    state.Exp_SpinSpeed = g_Globals.Misc.Exploits.LocalPlayer.SpinSpeed;

    state.Gen_ThreadDelay = g_Globals.General.ThreadDelay;
    state.Gen_CaptureBypass = g_Globals.General.CaptureBypass;
    state.Gen_ShutDown = g_Globals.General.ShutDown;
    state.Gen_N32 = g_Globals.General.N32;
    state.Gen_V31 = g_Globals.General.V31;

    memcpy(state.EnemyColor, g_Globals.Visuals.ESP.EnemyColor, sizeof(state.EnemyColor));
    memcpy(state.TeamColor, g_Globals.Visuals.ESP.TeamColor, sizeof(state.TeamColor));
    memcpy(state.BoxColor, g_Globals.Visuals.ESP.BoxColor, sizeof(state.BoxColor));
    memcpy(state.SkeletonColor, g_Globals.Visuals.ESP.SkeletonColor, sizeof(state.SkeletonColor));

    ssize_t sent = send(g_Socket, &state, sizeof(state), 0);
    if (sent != sizeof(state)) {
        LOGE("Falha ao enviar config: %s", strerror(errno));
    }
}

void SyncStateFromDaemon() {
    if (g_Socket < 0) return;

    IPC_GAME_STATE state;
    ssize_t n = recv(g_Socket, &state, sizeof(state), MSG_DONTWAIT);
    if (n == sizeof(state) && state.Magic == IPC_MAGIC_PLAYERS) {
        g_LastState = state;
    }
}

const IPC_GAME_STATE& GetLastState() {
    return g_LastState;
}

void RenderESP() {
    const IPC_GAME_STATE& state = g_LastState;
    if (state.Magic != IPC_MAGIC_PLAYERS) return;

    ImDrawList* drawList = ImGui::GetBackgroundDrawList();
    if (!drawList) return;

    for (uint32_t i = 0; i < state.PlayerCount && i < state.MaxPlayers; i++) {
        const IPC_PLAYER_DATA& player = state.Players[i];
        if (player.Health <= 0) continue;

        ImColor color = player.IsTeam
            ? ImColor(g_Globals.Visuals.ESP.TeamColor[0], g_Globals.Visuals.ESP.TeamColor[1],
                      g_Globals.Visuals.ESP.TeamColor[2], g_Globals.Visuals.ESP.TeamColor[3])
            : ImColor(g_Globals.Visuals.ESP.EnemyColor[0], g_Globals.Visuals.ESP.EnemyColor[1],
                      g_Globals.Visuals.ESP.EnemyColor[2], g_Globals.Visuals.ESP.EnemyColor[3]);

        if (g_Globals.Visuals.ESP.Box) {
            float x = player.Box[0];
            float y = player.Box[1];
            float w = player.Box[2];
            float h = player.Box[3];
            drawList->AddRect(ImVec2(x, y), ImVec2(x + w, y + h), color, 0.0f, 0, g_Globals.Visuals.ESP.Thickness);

            if (g_Globals.Visuals.ESP.BoxFilled) {
                drawList->AddRectFilled(ImVec2(x, y), ImVec2(x + w, y + h),
                    ImColor(g_Globals.Visuals.ESP.FilledBoxColor[0], g_Globals.Visuals.ESP.FilledBoxColor[1],
                            g_Globals.Visuals.ESP.FilledBoxColor[2], 0.3f));
            }
        }

        if (g_Globals.Visuals.ESP.HealthBar && player.MaxHealth > 0) {
            float healthPct = player.Health / player.MaxHealth;
            float barW = 4.0f;
            float barH = player.Box[3];
            float barX = player.Box[0] - barW - 2.0f;
            float barY = player.Box[1];
            drawList->AddRectFilled(ImVec2(barX, barY), ImVec2(barX + barW, barY + barH), IM_COL32(0, 0, 0, 200));
            drawList->AddRectFilled(ImVec2(barX, barY + barH * (1.0f - healthPct)),
                                    ImVec2(barX + barW, barY + barH),
                                    IM_COL32((int)(255 * (1.0f - healthPct)), (int)(255 * healthPct), 0, 255));
        }

        if (g_Globals.Visuals.ESP.ShowName && player.Name[0]) {
            ImVec2 textSize = ImGui::CalcTextSize(player.Name);
            drawList->AddText(ImVec2(player.ScreenPos[0] - textSize.x * 0.5f, player.Box[1] - textSize.y - 2.0f),
                              color, player.Name);
        }

        if (g_Globals.Visuals.ESP.Distance) {
            char distStr[32];
            snprintf(distStr, sizeof(distStr), "%.0fm", player.Distance);
            drawList->AddText(ImVec2(player.Box[0], player.Box[1] + player.Box[3] + 2.0f), color, distStr);
        }

        if (g_Globals.Visuals.ESP.Weapon && player.Weapon[0]) {
            drawList->AddText(ImVec2(player.Box[0] + player.Box[2] + 4.0f, player.Box[1]), color, player.Weapon);
        }

        if (g_Globals.Visuals.ESP.SnapLines) {
            ImVec2 screenCenter(ImGui::GetIO().DisplaySize.x * 0.5f, ImGui::GetIO().DisplaySize.y);
            if (g_Globals.Visuals.ESP.SnapLinesPos == 1) {
                screenCenter.y = 0;
            }
            drawList->AddLine(screenCenter, ImVec2(player.ScreenPos[0], player.ScreenPos[1]), color, g_Globals.Visuals.ESP.Thickness);
        }

        if (g_Globals.Visuals.ESP.Skeleton && player.SkeletonPointCount > 1) {
            for (int s = 1; s < player.SkeletonPointCount; s++) {
                drawList->AddLine(
                    ImVec2(player.SkeletonPoints[s-1][0], player.SkeletonPoints[s-1][1]),
                    ImVec2(player.SkeletonPoints[s][0], player.SkeletonPoints[s][1]),
                    color, g_Globals.Visuals.ESP.Thickness);
            }
        }
    }
}

} // namespace IPCClient
