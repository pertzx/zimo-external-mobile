#ifndef ESP_MANAGER_HPP
#define ESP_MANAGER_HPP

#include <chrono>
#include <string>
#include <Windows.h>

#include <SDK_FF/ESP.hpp>

class ESPManager {
private:
    FFESP esp;
    bool isActive;
    bool isConnected;
    std::chrono::steady_clock::time_point lastFrameTime;
    float currentFPS;
    int frameCount;

public:
    ESPManager() : isActive(true), isConnected(false), currentFPS(0.0f), frameCount(0) {
        lastFrameTime = std::chrono::steady_clock::now();
    }

    bool Initialize() {
        if (!esp.Initialize()) {
            return false;
        }
        isConnected = true;
        return true;
    }

    void Update() {
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastFrameTime).count();
        frameCount++;

        if (elapsed >= 1000) {
            currentFPS = frameCount * 1000.0f / (float)elapsed;
            frameCount = 0;
            lastFrameTime = now;
        }

        if (GetAsyncKeyState(VK_F1) & 1) { isActive = !isActive; Sleep(200); }
        if (GetAsyncKeyState(VK_F2) & 1) { esp.cfg.skeleton   = !esp.cfg.skeleton;   Sleep(200); }
        if (GetAsyncKeyState(VK_F3) & 1) { esp.cfg.show_team  = !esp.cfg.show_team;  Sleep(200); }
        if (GetAsyncKeyState(VK_F4) & 1) { esp.cfg.show_dead  = !esp.cfg.show_dead;  Sleep(200); }

        if (isActive && isConnected) {
         
        }
    }

    void Shutdown() {
        esp.Shutdown();
        isConnected = false;
    }

    bool IsActive()      const { return isActive; }
    bool IsConnected()   const { return isConnected; }
    float GetFPS()       const { return currentFPS; }
    int GetPlayerCount() const { return 0; }

    std::string GetConnectionStatus() const {
        return isConnected ? "CONECTADO" : "DESCONECTADO";
    }

    const FFESPConfig& GetConfig() const { return esp.cfg; }

    void ToggleESP()      { isActive = !isActive; }
    void ToggleSkeleton() { esp.cfg.skeleton  = !esp.cfg.skeleton; }
    void ToggleShowTeam() { esp.cfg.show_team = !esp.cfg.show_team; }
    void ToggleShowDead() { esp.cfg.show_dead = !esp.cfg.show_dead; }
};

#endif 
