// Panel/IPC/IPCClient.cpp
#include "IPCClient.hpp"
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <android/log.h>

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "StormIPC", __VA_ARGS__)

static int g_Socket = -1;

bool IPCClient::Connect(const char* socketPath) {
    g_Socket = socket(AF_UNIX, SOCK_STREAM, 0);
    if (g_Socket < 0) return false;

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, socketPath, sizeof(addr.sun_path) - 1);

    if (connect(g_Socket, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        close(g_Socket);
        g_Socket = -1;
        return false;
    }

    LOGI("Conectado ao daemon IPC");
    return true;
}

void IPCClient::SyncConfigToDaemon() {
    if (g_Socket < 0) return;

    // Enviar g_Globals como IPC_STATE
    IPC_STATE state;
    memset(&state, 0, sizeof(state));
    state.Magic = IPC_MAGIC_STATE;
    state.ConfigSeq = 1; // incrementar quando mudar

    // Copiar configurações relevantes de g_Globals para state
    // ... (mapear todos os campos de g_Globals para WEB_FEATURES)

    send(g_Socket, &state, sizeof(state), 0);
}

void IPCClient::SyncStateFromDaemon() {
    if (g_Socket < 0) return;

    IPC_STATE state;
    int n = recv(g_Socket, &state, sizeof(state), MSG_DONTWAIT);
    if (n == sizeof(state) && state.Magic == IPC_MAGIC_STATE) {
        // Atualizar estado do jogo no painel
        // EnemyCount, ClosestEnemyDist, etc.
    }
}

void IPCClient::Disconnect() {
    if (g_Socket >= 0) {
        close(g_Socket);
        g_Socket = -1;
    }
}