#include "interface.hpp"
#include <imgui.h>
#include <android/shared/IpcProtocol.h>

namespace ZmInternal {
    namespace UI {

        // Thread-local storage for menu state
        thread_local bool g_menuOpen = false;

        void RenderMenu() {
            if (!g_menuOpen) {
                return;
            }

            ImGui::Begin("ZmInternal Menu", &g_menuOpen, ImGuiWindowFlags_AlwaysAutoResize);

            // Master switch
            static bool enableFuncs = true;
            if (ImGui::Checkbox("Enable Functions", &enableFuncs)) {
                ImGui::Checkbox("Enable Functions", &enableFuncs)) {
                    // In a real implementation, we would send this to daemon via IPC
                    // For now, just store locally
                }
            }

            ImGui::Separator();

            // Aimbot section
            if (ImGui::CollapsingHeader("Aimbot")) {
                static bool aimbotEnabled = false;
                if (ImGui::Checkbox("Enable Aimbot", &aimbotEnabled)) {
                    // Send config update to daemon
                }
                ImGui::SameLine();
                HelpMarker("Enable automatic aiming");

                static bool silentAimEnabled = false;
                if (ImGui::Checkbox("Enable Silent Aim", &silentAimEnabled)) {
                    // Send config update to daemon
                }
                ImGui::SameLine();
                HelpMarker("Aim without moving crosshair");

                static float fov = 100.0f;
                ImGui::SliderFOV("FOV", &fov, 0.0f, 180.0f);
                ImGui::SameLine();
                HelpMarker("Field of view for target selection");

                static float smooth = 1.0f;
                ImGui::SliderFloat("Smooth", &smooth, 0.0f, 10.0f, "%.1f");
                ImGui::SameLine();
                HelpMarker("Aim smoothing (lower = snappier)");

                static float maxDistance = 500.0f;
                ImGui::SliderFloat("Max Distance", &maxDistance, 0.0f, 1000.0f, "%.0f");
                ImGui::SameLine();
                HelpMarker("Maximum distance to target");

                static bool visibleCheck = true;
                if (ImGui::Checkbox("Visible Check", &visibleCheck)) {
                    // Send config update to daemon
                }
                ImGui::SameLine();
                HelpMarker("Only target visible enemies");

                static bool ignoreKnocked = true;
                if (ImGui::Checkbox("Ignore Knocked", &ignoreKnocked)) {
                    // Send config update to daemon
                }
                ImGui::SameLine();
                HelpMarker("Don't target knocked enemies");

                static bool ignoreBots = true;
                if (ImGui::Checkbox("Ignore Bots", &ignoreBots)) {
                    // Send config update to daemon
                }
                ImGui::SameLine();
                HelpMarker("Don't target AI bots");

                static int aimbotBone = 0; // 0=head, 1=neck, etc.
                ImGui::Combo("Aim Bone", &aimbotBone, "Head\0Neck\0Chest\0Pelvis\0\0");
                ImGui::SameLine();
                HelpMarker("Which bone to aim for");

                static int silentAimBone = 0;
                ImGui::Combo("Silent Aim Bone", &silentAimBone, "Head\0Neck\0Chest\0Pelvis\0\0");
                ImGui::SameLine();
                HelpMarker("Which bone to use for silent aim");
            }

            ImGui::Separator();

            // ESP section
            if (ImGui::CollapsingHeader("ESP")) {
                static bool espEnabled = false;
                if (ImGui::Checkbox("Enable ESP", &espEnabled)) {
                    // Send config update to daemon
                }
                ImGui::SameLine();
                HelpMarker("Enemy ESP (boxes, health, name, etc.)");

                static bool chamsEnabled = false;
                if (ImGui::Checkbox("Enable Chams", &chamsEnabled)) {
                    // Send config update to daemon
                }
                ImGui::SameLine();
                HelpMarker("Wallhack (experimental - may not work on Android)");
            }

            ImGui::Separator();

            // Exploits section
            if (ImGui::CollapsingHeader("Exploits")) {
                static bool noRecoil = false;
                if (ImGui::Checkbox("No Recoil", &noRecoil)) {
                    // Send config update to daemon
                }
                ImGui::SameLine();
                HelpMarker("Remove weapon recoil");

                static bool fireDelay = false;
                if (ImGui::Checkbox("Fire Delay", &fireDelay)) {
                    // Send config update to daemon
                }
                ImGui::SameLine();
                HelpMarker("Reduce time between shots");

                static bool moreDamage = false;
                if (ImGui::Checkbox("More Damage", &moreDamage)) {
                    // Send config update to daemon
                }
                ImGui::SameLine();
                HelpMarker("Increase weapon damage");

                static bool aimlock = false;
                if (ImGui::Checkbox("Aimlock", &aimlock)) {
                    // Send config update to daemon
                }
                ImGui::SameLine();
                HelpMarker("Lock aim onto target");

                static bool fastMedkit = false;
                if (ImGui::Checkbox("Fast Medkit", &fastMedkit)) {
                    // Send config update to daemon
                }
                ImGui::SameLine();
                HelpMarker("Instant medkit use");

                static bool telaParada = false;
                if (ImGui::Checkbox("Tela Parada", &telaParada)) {
                    // Send config update to daemon
                }
                ImGui::SameLine();
                HelpMarker("Stop enemy movement");

                static bool atributarArma = false;
                if (ImGui::Checkbox("Atributar Arma", &atributarArma)) {
                    // Send config update to daemon
                }
                ImGui::SameLine();
                HelpMarker("Weapon attribute modifier");

                static bool bugarPixel = false;
                if (ImGui::Checkbox("Bugar Pixel", &bugarPixel)) {
                    // Send config update to daemon
                }
                ImGui::SameLine();
                HelpMarker("Pixel bug exploit");

                static bool precision = false;
                if (ImGui::Checkbox("Precision", &precision)) {
                    // Send config update to daemon
                }
                ImGui::SameLine();
                HelpMarker("Increase weapon precision");

                static bool backJump = false;
                if (ImGui::Checkbox("Back Jump", &backJump)) {
                    // Send config update to daemon
                }
                ImGui::SameLine();
                HelpMarker("Jump backwards when shooting");

                static bool socoLonge = false;
                if (ImGui::Checkbox("Soco Longe", &socoLonge)) {
                    // Send config update to daemon
                }
                ImGui::SameLine();
                HelpMarker("Long punch range");

                static bool spinBot = false;
                if (ImGui::Checkbox("Spin Bot", &spinBot)) {
                    // Send config update to daemon
                }
                ImGui::SameLine();
                HelpMarker("Spin rapidly when aiming");

                static bool aimlock2x = false;
                if (ImGui::Checkbox("Aimlock 2x", &aimlock2x)) {
                    // Send config update to daemon
                }
                ImGui::SameLine();
                HelpMarker("Dual aimlock");

                static bool aimbotAwm = false;
                if (ImGui::Checkbox("Aimbot AWM", &aimbotAwm)) {
                    // Send config update to daemon
                }
                ImGui::SameLine();
                HelpMarker("Special aimbot for AWM");
            }

            ImGui::Separator();

            // Info / Watermark
            ImGui::Text("ZmInternal v2.0 Android");
            ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1.0f), "Built with OpenClaude");

            ImGui::End();
        }

        bool IsMenuOpen() {
            return g_menuOpen;
        }

        void SetMenuOpen(bool open) {
            g_menuOpen = open;
        }

        void ToggleMenu() {
            g_menuOpen = !g_menuOpen;
        }

        // Helper function to display tooltips
        void HelpMarker(const char* desc) {
            ImGui::TextDisabled("(?)");
            if (ImGui::IsItemHovered()) {
                ImGui::BeginTooltip();
                ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
                ImGui::TextUnformatted(desc);
                ImGui::PopTextWrapPos();
                ImGui::EndTooltip();
            }
        }

    } // namespace UI
} // namespace ZmInternal