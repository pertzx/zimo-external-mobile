#include "PanelApp.hpp"
#include "AndroidOverlay.hpp"
#include "AndroidInput.hpp"
#include "Interface/Interface.hpp"
#include "IPC/IPCClient.hpp"
#include <android/log.h>
#include <thread>
#include <chrono>
#include <Notify/Notify.hpp>

// #define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "StormPanel", __VA_ARGS__)
// #define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "StormPanel", __VA_ARGS__)

static bool g_Running = true;
// static Interface* g_Interface = nullptr;
static int g_SurfaceWidth = 0;
static int g_SurfaceHeight = 0;

namespace PanelApp {

void Run(ANativeWindow* window) {
    LOGI("Iniciando painel Storm Cheats");

    if (!Overlay::Setup(window) || !Overlay::Initialize()) {
        LOGE("Falha ao inicializar overlay");
        return;
    }

    // Criar contexto ImGui
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;

    // Inicializar backends
    ImGui_ImplAndroid_Init(window);
    ImGui_ImplOpenGL3_Init("#version 300 es");

    // Carregar fonts e estilo
    Fonts::Initialize();

    // Em PanelApp.cpp, dentro de Run():
    g_Interface = new Interface();
    g_Interface->Initialize(); // Sem parametros
    g_Interface->UpdateStyle();


    // Conectar ao daemon via IPC
    IPCClient::Connect("/data/local/tmp/storm_daemon.sock");

    auto lastFrame = std::chrono::high_resolution_clock::now();

    while (g_Running) {
        AndroidInput::ProcessEvents();

        if (g_Globals.General.ShutDown) {
            break;
        }

        // Sincronizar config com daemon
        IPCClient::SyncConfigToDaemon();
        IPCClient::SyncStateFromDaemon();

        g_Interface->HandleMenuKey();

        // Novo frame ImGui
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplAndroid_NewFrame();
        ImGui::NewFrame();

        {
            // Render ESP recebido do daemon (via IPC)
            IPCClient::RenderESP();

            // Render menu ImGui
            g_Interface->RenderGui();
            NotifyManager::Render();

            // FOV Circles
            if (g_Globals.Misc.Screen.ShowAimbotFov) {
                ImColor Outline(g_Globals.Misc.Screen.AimbotFovColor[0], g_Globals.Misc.Screen.AimbotFovColor[1],
                                g_Globals.Misc.Screen.AimbotFovColor[2], g_Globals.Misc.Screen.AimbotFovColor[3]);
                ImColor Fill(g_Globals.Misc.Screen.FilledFovColor[0], g_Globals.Misc.Screen.FilledFovColor[1],
                             g_Globals.Misc.Screen.FilledFovColor[2], g_Globals.Misc.Screen.FilledFovColor[3]);
                const ImVec2 Center(ImGui::GetIO().DisplaySize.x * 0.5f, ImGui::GetIO().DisplaySize.y * 0.5f);
                ImGui::GetBackgroundDrawList()->AddCircleFilled(Center, g_Globals.AimBot.Fov, Fill, 360);
                ImGui::GetBackgroundDrawList()->AddCircle(Center, g_Globals.AimBot.Fov, Outline, 360);
            }

            if (g_Globals.Misc.Screen.ShowSilentFov) {
                ImColor Outline(g_Globals.Misc.Screen.SilentFovColor[0], g_Globals.Misc.Screen.SilentFovColor[1],
                                g_Globals.Misc.Screen.SilentFovColor[2], g_Globals.Misc.Screen.SilentFovColor[3]);
                ImColor Fill(g_Globals.Misc.Screen.SilentFilledFovColor[0], g_Globals.Misc.Screen.SilentFilledFovColor[1],
                             g_Globals.Misc.Screen.SilentFilledFovColor[2], g_Globals.Misc.Screen.SilentFilledFovColor[3]);
                const ImVec2 Center(ImGui::GetIO().DisplaySize.x * 0.5f, ImGui::GetIO().DisplaySize.y * 0.5f);
                ImGui::GetBackgroundDrawList()->AddCircleFilled(Center, g_Globals.Silent.Fov, Fill, 360);
                ImGui::GetBackgroundDrawList()->AddCircle(Center, g_Globals.Silent.Fov, Outline, 360);
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

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplAndroid_Shutdown();
    ImGui::DestroyContext();
    Fonts::CleanupTextures();
    Overlay::ShutDown();
    IPCClient::Disconnect();
}

void OnResize(int width, int height) {
    g_SurfaceWidth = width;
    g_SurfaceHeight = height;
    if (g_Interface) {
        g_Interface->ResizeWidth = width;
        g_Interface->ResizeHeight = height;
    }
}

void RequestShutdown() {
    g_Running = false;
}

} // namespace PanelApp
