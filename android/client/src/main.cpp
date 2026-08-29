#include <android/log.h>
#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <android/native_window.h>
#include <android/native_activity.h>
#include <thread>
#include <chrono>
#include <memory>

#include "render/renderer.hpp"
#include "render/overlay.hpp"
#include "render/fonts.hpp"
#include "ui/interface.hpp"
#include "ui/input.hpp"
#include "ipc/client_ipc.hpp"
#include "logic/esp.hpp"
#include "logic/aimbot.hpp"
#include "logic/silent.hpp"
#include "logic/exploits.hpp"
#include <android/shared/IpcProtocol.h>
#include <imgui.h>

#define LOG_TAG "ZmInternal-Client"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

namespace ZmInternal {

    class Client {
    public:
        Client() : m_running(false) {}

        bool Initialize(ANativeWindow* window) {
            LOGI("Initializing client...");

            // Initialize renderer
            if (!m_renderer.Initialize(window)) {
                LOGE("Failed to initialize renderer");
                return false;
            }

            // Initialize ImGui
            IMGUI_CHECKVERSION();
            ImGui::CreateContext();
            ImGuiIO& io = ImGui::GetIO(); (void)io;
            io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
            io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls

            // Setup Dear ImGui style
            ImGui::StyleColorsDark();
            // ImGui::StyleColorsLight();

            // Setup Platform/Renderer backends
            // ImGui_ImplAndroid_Init(); // Would need custom implementation
            // ImGui_ImplOpenGL3_Init("#version 300 es"); // Would need custom implementation

            // Initialize UI systems
            UI::InitializeInput();

            // Initialize IPC client
            if (!m_ipc.Initialize()) {
                LOGE("Failed to initialize IPC client");
                return false;
            }

            // Set up IPC callbacks
            m_ipc.SetSnapshotCallback([this](const IpcMsgSnapshot& snapshot) {
                m_latestSnapshot = snapshot;
                m_snapshotTimestamp = std::chrono::steady_clock::now();
            });

            m_ipc.SetAckCallback([this](const IpcMsgAck& ack) {
                // Handle ack from daemon
                LOGI("Received ACK: seq=%u, success=%d", ack.seq, ack.success);
            });

            m_ipc.SetHeartbeatCallback([this](const IpcMsgHeartbeat& heartbeat) {
                // Handle heartbeat from daemon
                m_daemonLastSeen = std::chrono::steady_clock::now();
            });

            m_running = true;
            LOGI("Client initialized successfully");
            return true;
        }

        void Shutdown() {
            LOGI("Shutting down client...");
            m_running = false;

            // Shutdown systems
            UI::ShutdownInput();
            m_ipc.Shutdown();

            // Shutdown ImGui
            // ImGui_ImplOpenGL3_Shutdown();
            // ImGui_ImplAndroid_Shutdown();
            ImGui::DestroyContext();

            // Shutdown renderer
            m_renderer.Shutdown();

            LOGI("Client shut down");
        }

        void RunFrame() {
            if (!m_running) {
                return;
            }

            // Start the Dear ImGui frame
            // ImGui_ImplOpenGL3_NewFrame();
            // ImGui_ImplAndroid_NewFrame();
            ImGui::NewFrame();

            // Update input
            // In a real implementation, we would process queued input events here

            // Render UI
            UI::RenderMenu();

            // Render ESP if enabled
            if (UI::IsMenuOpen() || true) { // Always render ESP for now, could be toggled
                if (!m_latestSnapshot.entities[0].isActive && m_latestSnapshot.count > 0) {
                    // We have a valid snapshot
                    Logic::RenderESP(&m_latestSnapshot, ImGui::GetForegroundDrawList());
                }
            }

            // Update aimbot
            static IpcMsgConfig defaultConfig = {};
            Logic::UpdateAimbot(&m_latestSnapshot, &defaultConfig);

            // Update silent aim
            Logic::UpdateSilentAim(&m_latestSnapshot, &defaultConfig);

            // Update exploits
            Logic::ApplyExploitToggles(&defaultConfig);

            // Rendering
            m_renderer.BeginFrame();

            // ImGui rendering
            ImGui::Render();
            // ImGui_ImplOpenGL3_RenderDrawLists(ImGui::GetDrawLists());

            m_renderer.EndFrame();
        }

        bool IsRunning() const { return m_running; }

    private:
        Render::Renderer m_renderer;
        IPC::ClientIPC m_ipc;
        std::atomic<bool> m_running;

        // Snapshot data
        IpcMsgSnapshot m_latestSnapshot{};
        std::chrono::steady_clock::time_point m_snapshotTimestamp{};
        std::chrono::steady_clock::time_point m_daemonLastSeen{};
    };

} // namespace ZmInternal

// Global client instance
static ZmInternal::Client* g_client = nullptr;

// Called from Java via JNI
extern "C" {
    JNIEXPORT void JNICALL
    Java_com_zminternal_OverlayActivity_nativeOnCreate(JNIEnv* env, jobject thiz, jobject surface) {
        LOGI("nativeOnCreate called");

        // Get the native window from the surface
        ANativeWindow* window = ANativeWindow_fromSurface(env, surface);
        if (!window) {
            LOGE("Failed to get native window from surface");
            return;
        }

        // Create and initialize client
        g_client = new ZmInternal::Client();
        if (!g_client->Initialize(window)) {
            LOGE("Failed to initialize client");
            delete g_client;
            g_client = nullptr;
            ANativeWindow_release(window);
            return;
        }

        // Note: We don't release the window here as we need it for rendering
        // It will be released in nativeOnDestroy
    }

    JNIEXPORT void JNICALL
    Java_com_zminternal_OverlayActivity_nativeOnDestroy(JNIEnv* env, jobject thiz) {
        LOGI("nativeOnDestroy called");

        if (g_client) {
            g_client->Shutdown();
            delete g_client;
            g_client = nullptr;
        }
    }

    JNIEXPORT void JNICALL
    Java_com_zminternal_OverlayActivity_nativeOnResume(JNIEnv* env, jobject thiz) {
        LOGI("nativeOnResume called");
    }

    JNIEXPORT void JNICALL
    Java_com_zminternal_OverlayActivity_nativeOnPause(JNIEnv* env, jobject thiz) {
        LOGI("nativeOnPause called");
    }

    JNIEXPORT void JNICALL
    Java_com_zminternal_OverlayActivity_nativeOnTouchEvent(JNIEnv* env, jobject thiz, jobject motionEvent) {
        // Process touch event and forward to UI input system
        if (g_client) {
            // In a real implementation, we would convert the Java MotionEvent to AInputEvent
            // and call UI::ProcessInputEvent
            // For now, we'll just log it
            LOGI("Touch event received");
        }
    }

    JNIEXPORT void JNICALL
    Java_com_zminternal_OverlayActivity_nativeOnRenderFrame(JNIEnv* env, jobject thiz) {
        if (g_client && g_client->IsRunning()) {
            g_client->RunFrame();
        }
    }
}