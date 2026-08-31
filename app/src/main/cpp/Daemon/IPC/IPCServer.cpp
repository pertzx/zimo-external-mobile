#include "IPCServer.hpp"
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <android/log.h>
#include <cstring>
#include <errno.h>
#include <fcntl.h>

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "StormIPC", __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "StormIPC", __VA_ARGS__)

static int g_ServerSocket = -1;
static int g_ClientSocket = -1;
static IPC_GAME_STATE g_CurrentState{};
static IPC_CONFIG_STATE g_LastConfig{};
static bool g_Running = false;

namespace IPCServer {

bool Start(const char* socketPath) {
    // Remover socket antigo se existir
    unlink(socketPath);

    g_ServerSocket = socket(AF_UNIX, SOCK_STREAM, 0);
    if (g_ServerSocket < 0) {
        LOGE("Falha ao criar server socket: %s", strerror(errno));
        return false;
    }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, socketPath, sizeof(addr.sun_path) - 1);

    if (bind(g_ServerSocket, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        LOGE("Falha ao bind socket: %s", strerror(errno));
        close(g_ServerSocket);
        g_ServerSocket = -1;
        return false;
    }

    if (listen(g_ServerSocket, 1) < 0) {
        LOGE("Falha ao listen socket: %s", strerror(errno));
        close(g_ServerSocket);
        g_ServerSocket = -1;
        return false;
    }

    // Non-blocking
    int flags = fcntl(g_ServerSocket, F_GETFL, 0);
    fcntl(g_ServerSocket, F_SETFL, flags | O_NONBLOCK);

    g_Running = true;
    LOGI("Servidor IPC iniciado em %s", socketPath);
    return true;
}

void Stop() {
    g_Running = false;
    if (g_ClientSocket >= 0) {
        close(g_ClientSocket);
        g_ClientSocket = -1;
    }
    if (g_ServerSocket >= 0) {
        close(g_ServerSocket);
        g_ServerSocket = -1;
    }
}

bool IsRunning() {
    return g_Running;
}

void AcceptClient() {
    if (g_ClientSocket >= 0) return; // Ja tem cliente

    struct sockaddr_un addr;
    socklen_t len = sizeof(addr);
    int client = accept(g_ServerSocket, (struct sockaddr*)&addr, &len);
    if (client >= 0) {
        g_ClientSocket = client;
        int flags = fcntl(g_ClientSocket, F_GETFL, 0);
        fcntl(g_ClientSocket, F_SETFL, flags | O_NONBLOCK);
        LOGI("Cliente IPC conectado");
    }
}

void ProcessCommands() {
    AcceptClient();
    if (g_ClientSocket < 0) return;

    IPC_CONFIG_STATE config;
    ssize_t n = recv(g_ClientSocket, &config, sizeof(config), MSG_DONTWAIT);
    if (n == sizeof(config) && config.Magic == IPC_MAGIC_STATE) {
        if (config.Seq != g_LastConfig.Seq) {
            g_LastConfig = config;

            // Mapear IPC_CONFIG_STATE de volta para g_Globals
            g_Globals.AimBot.Enabled = config.AimBot_Enabled;
            g_Globals.AimBot.Fov = config.AimBot_Fov;
            g_Globals.AimBot.MaxDistance = config.AimBot_MaxDistance;
            g_Globals.AimBot.Target = config.AimBot_Target;
            g_Globals.AimBot.IgnoreBots = config.AimBot_IgnoreBots;
            g_Globals.AimBot.IgnoreKnocked = config.AimBot_IgnoreKnocked;
            g_Globals.AimBot.VisibleCheck = config.AimBot_VisibleCheck;
            g_Globals.AimBot.aimmagnect = config.AimBot_Pull;
            g_Globals.AimBot.ghost = config.AimBot_Ghost;
            g_Globals.Misc.Screen.ShowAimbotFov = config.AimBot_ShowFov;
            g_Globals.AimBot.KeyBind = config.AimBot_KeyBind;

            g_Globals.Silent.Enabled = config.Silent_Enabled;
            g_Globals.Silent.Fov = config.Silent_Fov;
            g_Globals.Silent.MaxDistance = config.Silent_MaxDistance;
            g_Globals.Misc.Screen.ShowSilentFov = config.Silent_ShowFov;

            g_Globals.Visuals.ESP.Enemy = config.ESP_Enemy;
            g_Globals.Visuals.ESP.ShowTeam = config.ESP_ShowTeam;
            g_Globals.Visuals.ESP.Watermark = config.ESP_Watermark;
            g_Globals.Visuals.ESP.Box = config.ESP_Box;
            g_Globals.Visuals.ESP.BoxFilled = config.ESP_BoxFilled;
            g_Globals.Visuals.ESP.ShowName = config.ESP_ShowName;
            g_Globals.Visuals.ESP.HealthBar = config.ESP_HealthBar;
            g_Globals.Visuals.ESP.Distance = config.ESP_Distance;
            g_Globals.Visuals.ESP.Skeleton = config.ESP_Skeleton;
            g_Globals.Visuals.ESP.Weapon = config.ESP_Weapon;
            g_Globals.Visuals.ESP.SnapLines = config.ESP_SnapLines;
            g_Globals.Visuals.ESP.RenderDistance = config.ESP_RenderDistance;
            g_Globals.Visuals.ESP.Thickness = config.ESP_Thickness;
            g_Globals.Visuals.ESP.TextSize = config.ESP_TextSize;
            g_Globals.Visuals.ESP.BoxStyle = config.ESP_BoxStyle;
            g_Globals.Visuals.ESP.HealthBarStyle = config.ESP_HealthBarStyle;
            g_Globals.Visuals.ESP.WeaponStyle = config.ESP_WeaponStyle;
            g_Globals.Visuals.ESP.SnapLinesPos = config.ESP_SnapLinesPos;

            g_Globals.Visuals.Chams.Enabled = config.Chams_Enabled;
            g_Globals.Visuals.Chams.AggressiveMode = config.Chams_AggressiveMode;

            g_Globals.Misc.Exploits.LocalPlayer.AimLock2x = config.Exp_AimLock2x;
            g_Globals.Misc.Exploits.LocalPlayer.AimbotAwm = config.Exp_AimbotAwm;
            g_Globals.Misc.Exploits.LocalPlayer.NoRecoil = config.Exp_NoRecoil;
            g_Globals.Misc.Exploits.LocalPlayer.RecoilControl = config.Exp_RecoilControl;
            g_Globals.Misc.Exploits.LocalPlayer.FastMedkit = config.Exp_FastMedkit;
            g_Globals.Misc.Exploits.LocalPlayer.telaparada = config.Exp_TelaParada;
            g_Globals.Misc.Exploits.LocalPlayer.AtributarArma = config.Exp_AtributarArma;
            g_Globals.Misc.Exploits.LocalPlayer.AtributarArmaLevel = config.Exp_AtributarArmaLevel;
            g_Globals.Misc.Exploits.LocalPlayer.Aimlock = config.Exp_Aimlock;
            g_Globals.Misc.Exploits.LocalPlayer.MoreDamage = config.Exp_MoreDamage;
            g_Globals.Misc.Exploits.LocalPlayer.FireDelay = config.Exp_FireDelay;
            g_Globals.Misc.Exploits.LocalPlayer.BugarPixel = config.Exp_BugarPixel;
            g_Globals.Misc.Exploits.LocalPlayer.Precision = config.Exp_Precision;
            g_Globals.Misc.Exploits.LocalPlayer.BackJump = config.Exp_BackJump;
            g_Globals.Misc.Exploits.LocalPlayer.SocoLonge = config.Exp_SocoLonge;
            g_Globals.Misc.Exploits.LocalPlayer.SpinBot = config.Exp_SpinBot;
            g_Globals.Misc.Exploits.LocalPlayer.SpinSpeed = config.Exp_SpinSpeed;

            g_Globals.General.ThreadDelay = config.Gen_ThreadDelay;
            g_Globals.General.CaptureBypass = config.Gen_CaptureBypass;
            g_Globals.General.ShutDown = config.Gen_ShutDown;
            g_Globals.General.N32 = config.Gen_N32;
            g_Globals.General.V31 = config.Gen_V31;

            memcpy(g_Globals.Visuals.ESP.EnemyColor, config.EnemyColor, sizeof(config.EnemyColor));
            memcpy(g_Globals.Visuals.ESP.TeamColor, config.TeamColor, sizeof(config.TeamColor));
            memcpy(g_Globals.Visuals.ESP.BoxColor, config.BoxColor, sizeof(config.BoxColor));
            memcpy(g_Globals.Visuals.ESP.SkeletonColor, config.SkeletonColor, sizeof(config.SkeletonColor));
        }
    } else if (n == 0) {
        // Cliente desconectou
        close(g_ClientSocket);
        g_ClientSocket = -1;
        LOGI("Cliente IPC desconectado");
    }
}

void SyncState() {
    if (g_ClientSocket < 0) return;

    ssize_t sent = send(g_ClientSocket, &g_CurrentState, sizeof(g_CurrentState), 0);
    if (sent != sizeof(g_CurrentState)) {
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
            close(g_ClientSocket);
            g_ClientSocket = -1;
            LOGI("Cliente IPC desconectado (send)");
        }
    }
}

void UpdateGameState(const IPC_GAME_STATE& state) {
    g_CurrentState = state;
}

} // namespace IPCServer
