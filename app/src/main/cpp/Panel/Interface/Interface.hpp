#pragma once
// ========== DEFINIR ANTES DE INCLUIR imgui.h ==========
// #define IMGUI_DEFINE_MATH_OPERATORS
#include <imgui.h>
#include "IPC/IPCClient.hpp"
#include "Fonts/Fonts.hpp"
#include <Custom.hpp>
#include <Utils/Utils.hpp>

class Interface
{
public:
    Interface() = default;
    ~Interface() {
        ShutDown();
    }

    void Initialize();
    void InitializeMenu();
    void UpdateStyle();
    void RenderGui();
    void HandleMenuKey();
    void ShutDown();
    bool GetMenuOpen() const { return bIsMenuOpen; }
    int  GetCurrentTab() const { return CurrentTab; }

private:
    bool bIsMenuOpen = false;
    int CurrentTab = 0;
    bool isLoading = false;
    char loadingMessage[64] = "";
    bool MenuKeyDown = false;
public:
    int ResizeWidth = 0;
    int ResizeHeight = 0;
};
inline Interface* g_Interface = nullptr;
