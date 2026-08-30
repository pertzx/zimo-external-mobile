#include "PanelApp.hpp"
#include "AndroidOverlay.hpp"
#include "AndroidInput.hpp"
#include "Interface/Interface.hpp"
#include "IPC/IPCClient.hpp"
#include <android/log.h>
#include <thread>
#include <chrono>

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "StormPanel", __VA_ARGS__)

static bool g_Running = true;
static Interface* g_Interface = nullptr;

void PanelApp::Run(ANativeWindow* window) {
    LOGI("Iniciando painel Storm Cheats");

    // Inicializar overlay OpenGL
    if (!Overlay::Setup(window) || !Overlay::Initialize()) {
        LOGE("Falha ao inicializar overlay");
        return;
    }

    // Inicializar ImGui + Interface
    g_Interface = new Interface();
    g_Interface->Initialize(window);

    // Conectar ao daemon via IPC
    IPCClient::Connect("/data/local/tmp/storm_daemon.sock");

    auto lastFrame = std::chrono::high_resolution_clock::now();

    while (g_Running) {
        // Processar input Android
        AndroidInput::ProcessEvents();

        // Verificar shutdown
        if (g_Globals.General.ShutDown) {
            break;
        }

        // Sincronizar config com daemon
        IPCClient::SyncConfigToDaemon();
        IPCClient::SyncStateFromDaemon();

        // Atualizar tamanho da janela
        ImVec2 displaySize = Overlay::GetTargetWindowSize();
        if (g_Interface->ResizeWidth != 0 || g_Interface->ResizeHeight != 0) {
            g_Interface->ResizeWidth = g_Interface->ResizeHeight = 0;
        }

        // Handle menu key
        g_Interface->HandleMenuKey();

        // Novo frame ImGui
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplAndroid_NewFrame();  // MUDANÇA: Win32 → Android
        ImGui::NewFrame();

        {
            // Render ESP recebido do daemon (via IPC)
            // O daemon envia lista de jogadores; o painel desenha
            IPCClient::RenderESP();

            // Render menu ImGui
            g_Interface->RenderGui();
            NotifyManager::Render();

            // FOV Circles
            if (g_Globals.Misc.Screen.ShowAimbotFov) {
                // ... mesmo código do original ...
            }
            if (g_Globals.Misc.Screen.ShowSilentFov) {
                // ... mesmo código do original ...
            }
        }

        ImGui::EndFrame();
        ImGui::Render();

        // Render OpenGL
        Overlay::glClearTransparent();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        Overlay::glRefresh();

        // Frame limiter
        const double targetFPS = (g_Globals.General.ThreadDelay > 0) 
            ? static_cast<double>(g_Globals.General.ThreadDelay) : 60.0;
        const double frameDuration = 1000.0 / targetFPS;

        auto now = std::chrono::high_resolution_clock::now();
        double elapsed = std::chrono::duration<double, std::milli>(now - lastFrame).count();

        if (elapsed < frameDuration) {
            std::this_thread::sleep_for(
                std::chrono::milliseconds(static_cast<long long>(frameDuration - elapsed)));
        }
        lastFrame = std::chrono::high_resolution_clock::now();
    }

    // Cleanup
    if (g_Interface) {
        delete g_Interface;
        g_Interface = nullptr;
    }
    Overlay::ShutDown();
    IPCClient::Disconnect();
}

void PanelApp::RequestShutdown() {
    g_Running = false;
}