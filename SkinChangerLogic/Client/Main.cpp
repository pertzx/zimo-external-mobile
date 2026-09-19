#define IMGUI_DEFINE_MATH_OPERATORS
#include <imgui_impl_dx11.h>
#include <imgui_impl_win32.h>
#include <imgui_internal.h>
#include <imgui.h>
#include <d3d11.h>
#include <d3dx11.h>
#include <tchar.h>
#include <string>
#include <iostream>
#include <Fonts/FontAwesome6.hpp>
#include <Fonts/FontAwesome.hpp>
#include <Fonts/Fonts.hpp>
#include "Options.hpp"
#include "ClothChanger.hpp"
#include "GameMemory.hpp"
#include "ClothIcons.hpp"
#include <dwmapi.h>
#include <Fonts/ImageBytes.hpp>
#include <thread>
#include <vector>
#include <cstdio>
#include <algorithm>
#include <cstring>

static ID3D11Device*            g_pd3dDevice = nullptr;
static ID3D11DeviceContext*     g_pd3dDeviceContext = nullptr;
static IDXGISwapChain*          g_pSwapChain = nullptr;
static UINT                     g_ResizeWidth = 0, g_ResizeHeight = 0;
static ID3D11RenderTargetView*  g_mainRenderTargetView = nullptr;

bool CreateDeviceD3D(HWND hWnd);
void CleanupDeviceD3D();
void CreateRenderTarget();
void CleanupRenderTarget();
LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

ImFont* FontAwesomeRegular = nullptr;
ImFont* FontAwesomeSolid = nullptr;
ImFont* FontAwesomeSolid14 = nullptr;
ImFont* FontAwesomeBrands = nullptr;

ImFont* InterBlack = nullptr;
ImFont* InterBold = nullptr;
ImFont* InterBold12 = nullptr;
ImFont* InterExtraBold = nullptr;
ImFont* InterExtraLight = nullptr;
ImFont* InterLight = nullptr;
ImFont* InterMedium = nullptr;
ImFont* InterRegular = nullptr;
ImFont* InterSemiBold = nullptr;
ImFont* InterThin = nullptr;

ID3D11ShaderResourceView* Logo;

int CurrentTab = 0;
HWND hwnd = nullptr;

static inline ImVec2 Size = ImVec2(820, 600);
static constexpr float kSidebarW = 72.f;

void MouseMovement() {
    if (ImGui::IsItemActive()) {
        static RECT Rect = { 0 };
        GetWindowRect(hwnd, &Rect);
        ImGui::GetWindowSize();
        MoveWindow(hwnd, Rect.left + ImGui::GetMouseDragDelta().x, Rect.top + ImGui::GetMouseDragDelta().y, Size.x, Size.y, TRUE);
    }
}

static ImColor RarityColor(const std::string& rarity)
{
    if (rarity == "GREEN") return ImColor(80, 200, 120);
    if (rarity == "BLUE") return ImColor(70, 140, 255);
    if (rarity == "PURPLE") return ImColor(170, 90, 255);
    if (rarity == "PURPLE_PLUS") return ImColor(210, 70, 255);
    if (rarity == "ORANGE") return ImColor(255, 160, 40);
    if (rarity == "ORANGE_PLUS") return ImColor(255, 110, 30);
    if (rarity == "RED") return ImColor(255, 70, 90);
    return ImColor(200, 200, 210);
}

static const char* BrowseTabIcon(ClothBrowseTab tab)
{
    switch (tab)
    {
    case ClothBrowseTab::Trajes:     return ICON_FA_PERSON;
    case ClothBrowseTab::Passes:     return ICON_FA_TICKET;
    case ClothBrowseTab::Famous:     return ICON_FA_STAR;
    case ClothBrowseTab::Top:        return ICON_FA_SHIRT;
    case ClothBrowseTab::Bottom:     return ICON_FA_SOCKS;
    case ClothBrowseTab::Shoes:      return ICON_FA_SHOE_PRINTS;
    case ClothBrowseTab::Head:       return ICON_FA_HAT_COWBOY;
    case ClothBrowseTab::Mask:       return ICON_FA_MASK;
    case ClothBrowseTab::Facepaint:  return ICON_FA_SPRAY_CAN;
    case ClothBrowseTab::Applied:    return ICON_FA_LAYER_GROUP;
    default:                         return ICON_FA_LAYER_GROUP;
    }
}

static ClothCategory BrowseToPieceCategory(ClothBrowseTab tab)
{
    switch (tab)
    {
    case ClothBrowseTab::Top:       return ClothCategory::Top;
    case ClothBrowseTab::Bottom:    return ClothCategory::Bottom;
    case ClothBrowseTab::Shoes:     return ClothCategory::Shoes;
    case ClothBrowseTab::Head:      return ClothCategory::Head;
    case ClothBrowseTab::Mask:      return ClothCategory::Mask;
    case ClothBrowseTab::Facepaint: return ClothCategory::Facepaint;
    default:                        return ClothCategory::Other;
    }
}

static const char* CategoryIcon(ClothCategory cat)
{
    switch (cat)
    {
    case ClothCategory::Top:       return ICON_FA_SHIRT;
    case ClothCategory::Bottom:    return ICON_FA_SOCKS;
    case ClothCategory::Shoes:     return ICON_FA_SHOE_PRINTS;
    case ClothCategory::Head:      return ICON_FA_HAT_COWBOY;
    case ClothCategory::Mask:      return ICON_FA_MASK;
    case ClothCategory::Facepaint: return ICON_FA_SPRAY_CAN;
    case ClothCategory::Traje:     return ICON_FA_PERSON;
    default:                       return ICON_FA_LAYER_GROUP;
    }
}

static bool IconSidebarTab(const char* id, const char* icon, bool selected)
{
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems)
        return false;

    const ImGuiID widgetId = window->GetID(id);
    const ImVec2 pos = window->DC.CursorPos;
    const ImVec2 size(42.f, 42.f);
    const ImRect bb(pos, pos + size);

    ImGui::ItemSize(size, 0);
    if (!ImGui::ItemAdd(bb, widgetId))
        return false;

    bool hovered, held;
    bool pressed = ImGui::ButtonBehavior(bb, widgetId, &hovered, &held);

    const float alpha = ImGui::GetStyle().Alpha;
    ImDrawList* dl = window->DrawList;

    ImU32 bg = selected ? ImColor(255, 70, 175, (int)(alpha * 255))
                        : (hovered ? ImColor(32, 32, 36, (int)(alpha * 255)) : ImColor(20, 20, 22, (int)(alpha * 255)));
    ImU32 border = selected ? ImColor(255, 70, 175, (int)(alpha * 255))
                            : ImColor(42, 42, 48, (int)(alpha * 255));

    dl->AddRectFilled(bb.Min, bb.Max, bg, 10.f);
    dl->AddRect(bb.Min, bb.Max, border, 10.f, 0, 1.25f);

    ImGui::PushFont(FontAwesomeSolid);
    ImVec2 iconSize = ImGui::CalcTextSize(icon);
    ImU32 iconCol = selected ? ImColor(255, 255, 255, (int)(alpha * 255))
                             : ImColor(255, 255, 255, (int)(alpha * (hovered ? 200 : 130)));
    dl->AddText(bb.Min + ImVec2((size.x - iconSize.x) * 0.5f, (size.y - iconSize.y) * 0.5f), iconCol, icon);
    ImGui::PopFont();

    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 8.f);
    return pressed;
}

static void SidebarSectionLabel(const char* text, float alpha)
{
    ImGui::Dummy(ImVec2(0, 4.f));
    ImGui::PushFont(InterRegular);
    ImVec2 pos = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    // Tiny centered section mark
    ImVec2 ts = ImGui::CalcTextSize(text);
    const float avail = 44.f;
    dl->AddText(pos + ImVec2((avail - ts.x) * 0.5f, 0), ImColor(255, 255, 255, (int)(alpha * 70)), text);
    ImGui::Dummy(ImVec2(avail, ts.y + 6.f));
    ImGui::PopFont();
}

static void SidebarDivider(float alpha)
{
    ImVec2 pos = ImGui::GetCursorScreenPos();
    ImGui::GetWindowDrawList()->AddRectFilled(
        pos + ImVec2(8.f, 2.f), pos + ImVec2(36.f, 3.5f),
        ImColor(255, 255, 255, (int)(alpha * 28)), 2.f);
    ImGui::Dummy(ImVec2(44.f, 12.f));
}

static void TryLoadClothes()
{
    if (ClothChanger::IsLoaded())
        return;

    const char* paths[] = {
        "clothes.json",
        "codes\\clothes.json",
        "..\\codes\\clothes.json",
        "..\\..\\codes\\clothes.json",
    };

    for (const char* path : paths)
    {
        if (ClothChanger::LoadFromFile(path))
            break;
    }
}

int APIENTRY wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPWSTR lpCmdLine, int nCmdShow) {

    WNDCLASSEXW wc = { sizeof(wc), CS_CLASSDC, WndProc, 0L, 0L, GetModuleHandle(nullptr), nullptr, nullptr, nullptr, nullptr, L"_", nullptr };
    RegisterClassExW(&wc);
    hwnd = CreateWindowExW(NULL, wc.lpszClassName, L"__", WS_POPUP, (GetSystemMetrics(SM_CXSCREEN) / 2) - (Size.x / 2), (GetSystemMetrics(SM_CYSCREEN) / 2) - (Size.y / 2), Size.x, Size.y, NULL, NULL, wc.hInstance, NULL);

    MARGINS Margins = { -1 };
    DwmExtendFrameIntoClientArea(hwnd, &Margins);

    if (!CreateDeviceD3D(hwnd))
    {
        CleanupDeviceD3D();
        UnregisterClassW(wc.lpszClassName, wc.hInstance);
        return 1;
    }

    ShowWindow(hwnd, SW_SHOWDEFAULT);
    UpdateWindow(hwnd);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    ImGuiStyle* Style = &ImGui::GetStyle();

    io.IniFilename = nullptr;
    io.LogFilename = nullptr;

    Style->WindowRounding = 10.0f;
    Style->WindowBorderSize = 0.0f;
    Style->WindowPadding = ImVec2(0, 0);
    Style->WindowShadowSize = 0;
    Style->ScrollbarSize = 12.0f;
    Style->ScrollbarRounding = 6.0f;

    Style->Colors[ImGuiCol_Separator] = ImColor(0, 0, 0, 0);
    Style->Colors[ImGuiCol_SeparatorActive] = ImColor(0, 0, 0, 0);
    Style->Colors[ImGuiCol_SeparatorHovered] = ImColor(0, 0, 0, 0);
    Style->Colors[ImGuiCol_ResizeGrip] = ImColor(0, 0, 0, 0);
    Style->Colors[ImGuiCol_ResizeGripActive] = ImColor(0, 0, 0, 0);
    Style->Colors[ImGuiCol_ResizeGripHovered] = ImColor(0, 0, 0, 0);

    Style->Colors[ImGuiCol_ScrollbarBg] = ImColor(18, 18, 20, 180);
    Style->Colors[ImGuiCol_ScrollbarGrab] = ImColor(255, 70, 175, 160);
    Style->Colors[ImGuiCol_ScrollbarGrabHovered] = ImColor(255, 100, 190, 220);
    Style->Colors[ImGuiCol_ScrollbarGrabActive] = ImColor(255, 70, 175, 255);

    Style->Colors[ImGuiCol_WindowBg] = ImColor(12, 12, 12);
    Style->Colors[ImGuiCol_ChildBg] = ImColor(0, 0, 0, 0);
    Style->Colors[ImGuiCol_Border] = ImColor(23, 24, 25);
    Style->Colors[ImGuiCol_Text] = ImColor(1.f, 1.f, 1.f, 0.8f);
    Style->Colors[ImGuiCol_TextSelectedBg] = ImColor(255, 70, 175, 100);

    InterBlack = ImGui::GetIO().Fonts->AddFontFromMemoryCompressedTTF(InterBlack_compressed_data, InterBlack_compressed_size, 14);
    InterBold = ImGui::GetIO().Fonts->AddFontFromMemoryCompressedTTF(InterBold_compressed_data, InterBold_compressed_size, 16);
    InterBold12 = ImGui::GetIO().Fonts->AddFontFromMemoryCompressedTTF(InterBold_compressed_data, InterBold_compressed_size, 12);
    InterExtraBold = ImGui::GetIO().Fonts->AddFontFromMemoryCompressedTTF(InterExtraBold_compressed_data, InterExtraBold_compressed_size, 14);
    InterExtraLight = ImGui::GetIO().Fonts->AddFontFromMemoryCompressedTTF(InterExtraLight_compressed_data, InterExtraLight_compressed_size, 14);
    InterLight = ImGui::GetIO().Fonts->AddFontFromMemoryCompressedTTF(InterLight_compressed_data, InterLight_compressed_size, 16);
    InterMedium = ImGui::GetIO().Fonts->AddFontFromMemoryCompressedTTF(InterMedium_compressed_data, InterMedium_compressed_size, 16);
    InterRegular = ImGui::GetIO().Fonts->AddFontFromMemoryCompressedTTF(InterRegular_compressed_data, InterRegular_compressed_size, 16);
    InterSemiBold = ImGui::GetIO().Fonts->AddFontFromMemoryCompressedTTF(InterSemiBold_compressed_data, InterSemiBold_compressed_size, 16);
    InterThin = ImGui::GetIO().Fonts->AddFontFromMemoryCompressedTTF(InterThin_compressed_data, InterThin_compressed_size, 14);

    ImFontConfig FontAwesomeConfig;
    static const ImWchar FontAwesomeRanges[] = { ICON_MIN_FA, ICON_MAX_FA, 0 };
    static const ImWchar FontAwesomeRangesBrands[] = { ICON_MIN_FAB, ICON_MAX_FAB, 0 };

    FontAwesomeRegular = ImGui::GetIO().Fonts->AddFontFromMemoryCompressedTTF(FontAwesome6Regular_compressed_data, FontAwesome6Regular_compressed_size, 17.f, &FontAwesomeConfig, FontAwesomeRanges);
    FontAwesomeSolid = ImGui::GetIO().Fonts->AddFontFromMemoryCompressedTTF(FontAwesome6Solid_compressed_data, FontAwesome6Solid_compressed_size, 17.f, &FontAwesomeConfig, FontAwesomeRanges);
    FontAwesomeSolid14 = ImGui::GetIO().Fonts->AddFontFromMemoryCompressedTTF(FontAwesome6Solid_compressed_data, FontAwesome6Solid_compressed_size, 14.f, &FontAwesomeConfig, FontAwesomeRanges);
    FontAwesomeBrands = ImGui::GetIO().Fonts->AddFontFromMemoryCompressedTTF(FontAwesome6Brands_compressed_data, FontAwesome6Brands_compressed_size, 17.f, &FontAwesomeConfig, FontAwesomeRangesBrands);

    D3DX11CreateShaderResourceViewFromMemory(g_pd3dDevice, LogoBytes, sizeof(LogoBytes), NULL, NULL, &Logo, NULL);
    ClothIcons::Init(g_pd3dDevice);

    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dDeviceContext);

    bool done = false;
    while (!done)
    {
        MSG msg;
        while (PeekMessage(&msg, nullptr, 0U, 0U, PM_REMOVE))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
            if (msg.message == WM_QUIT)
                done = true;
        }
        if (done)
            break;

        if (g_ResizeWidth != 0 && g_ResizeHeight != 0)
        {
            CleanupRenderTarget();
            g_pSwapChain->ResizeBuffers(0, g_ResizeWidth, g_ResizeHeight, DXGI_FORMAT_UNKNOWN, 0);
            g_ResizeWidth = g_ResizeHeight = 0;
            CreateRenderTarget();
        }

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();
        ClothIcons::Pump();
        {
            // Tab
            static float AnimaTab = 0.0f;
            static int LastCurrentTab = 0;

            if (LastCurrentTab != CurrentTab)
            {
                AnimaTab = (LastCurrentTab > CurrentTab) ? -460.f : 460.f;
                LastCurrentTab = CurrentTab;
            }

            AnimaTab = ImLerp(AnimaTab, 0.f, 6.f * ImGui::GetIO().DeltaTime);

            // SubTab
            static int LastCurrentSub = 0;
            static int CurrentSub = 0;
            static float Anima = 0.f;

            if (LastCurrentSub != CurrentSub)
            {
                Anima = (LastCurrentSub > CurrentSub) ? -460.f : 460.f;
                LastCurrentSub = CurrentSub;
            }

            Anima = ImLerp(Anima, 0.f, 6.f * ImGui::GetIO().DeltaTime);

            ImGui::SetNextWindowPos(ImVec2(0, 0));
            ImGui::SetNextWindowSize(Size);
            ImGui::Begin("Menu", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
            {
                ImDrawList* DrawList = ImGui::GetWindowDrawList();
                ImVec2 Pos = ImGui::GetWindowPos();
                ImVec2 Size = ImGui::GetWindowSize();
                if (CurrentTab == 0) {

                    ImVec2 childSize = ImVec2(Size.x, Size.y);
                    ImVec2 childPos = ImVec2((Size.x - childSize.x) * 0.5f, (Size.y - childSize.y) * 0.5f + AnimaTab);
                    ImGui::SetCursorPos(childPos);
                    ImGui::BeginChild("LoginChild", childSize, ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
                    {
                        MouseMovement();
                        ImVec2 childInnerSize = ImGui::GetWindowSize();
                        float centerX = (childInnerSize.x - 285) * 0.5f;
                        float centerY = (childInnerSize.y - 180) * 0.5f + 55;
                        ImVec2 childPosScreen = ImGui::GetCursorScreenPos();
                        ImVec2 imageSize = ImVec2(130, 100);
                        ImVec2 imagePos = ImVec2(childPosScreen.x + centerX + (285 - imageSize.x) * 0.5f, childPosScreen.y + centerY - 115);
                        DrawList->AddImage(Logo, imagePos, imagePos + imageSize, ImVec2(0, 0), ImVec2(1, 1), ImColor(255, 255, 255, (int)(ImGui::GetStyle().Alpha * 255)));

                        ImGui::TabHeader("LoginHeader", &CurrentSub, { "Login", "Register" }, CurrentTab);

                        if (CurrentSub == 0) {
                            ImGui::SetCursorPos({ centerX + Anima, centerY });
                            ImGui::InputTextEx("##Username", "Username", g_Options.General.Username, IM_ARRAYSIZE(g_Options.General.Username), ImVec2(285, 35), ImGuiInputTextFlags_None);

                            ImGui::SetCursorPos({ centerX + Anima, centerY + 40 });
                            ImGui::InputTextEx("##Password", "Password", g_Options.General.Password, IM_ARRAYSIZE(g_Options.General.Password), ImVec2(285, 35), ImGuiInputTextFlags_Password);

                            ImGui::SetCursorPos({ centerX + Anima, centerY + 80 });
                            if (ImGui::Button("Sign In", ImVec2(285, 40))) {
                                std::thread([&]() {
                                    GameMemory::Attach(false);
                                    CurrentTab = 1;
                                    }).detach();
                            }
                        }
                        else if (CurrentSub == 1) {

                            ImGui::SetCursorPos({ centerX + Anima, centerY });
                            ImGui::InputTextEx("##Username", "Username", g_Options.General.Username, IM_ARRAYSIZE(g_Options.General.Username), ImVec2(285, 35), ImGuiInputTextFlags_None);

                            ImGui::SetCursorPos({ centerX + Anima, centerY + 40 });
                            ImGui::InputTextEx("##Password", "Password", g_Options.General.Password, IM_ARRAYSIZE(g_Options.General.Password), ImVec2(285, 35), ImGuiInputTextFlags_Password);

                            ImGui::SetCursorPos({ centerX + Anima, centerY + 80 });
                            ImGui::InputTextEx("##Key", "Key", g_Options.General.Key, IM_ARRAYSIZE(g_Options.General.Key), ImVec2(285, 35), ImGuiInputTextFlags_Password);

                            ImGui::SetCursorPos({ centerX + Anima, centerY + 120 });
                            if (ImGui::Button("Sign In", ImVec2(285, 35))) {
                                std::thread([&]() {
                                    GameMemory::Attach(false);
                                    CurrentTab = 1;
                                    }).detach();
                            }
                        }


                    }
                    ImGui::EndChild();

                }
                else {

                    const float sidebarW = kSidebarW;
                    const int alpha = (int)(ImGui::GetStyle().Alpha * 255);

                    // Narrow icon sidebar (matches mockup)
                    DrawList->AddRectFilled(Pos, Pos + ImVec2(sidebarW, Size.y), ImColor(16, 16, 16, alpha), ImGui::GetStyle().WindowRounding, ImDrawFlags_RoundCornersLeft);
                    DrawList->AddLine(Pos + ImVec2(sidebarW, 0), Pos + ImVec2(sidebarW, Size.y), ImGui::GetColorU32(ImGuiCol_Border));

                    static int SelectedCat = static_cast<int>(ClothBrowseTab::Trajes);
                    static bool ClothesTried = false;
                    if (!ClothesTried)
                    {
                        TryLoadClothes();
                        ClothesTried = true;
                    }

                    try { ClothChanger::Tick(); }
                    catch (...) {}

                    ImGui::BeginChild("LeftChild", ImVec2(sidebarW, Size.y), ImGuiChildFlags_None,
                        ImGuiWindowFlags_NoScrollbar);
                    {
                        MouseMovement();
                        ImGui::SetCursorPos(ImVec2(14.f, 16.f));
                        ImGui::BeginGroup();
                        {
                            const float a = ImGui::GetStyle().Alpha * 255.f;

                            SidebarSectionLabel("SET", a);
                            const ClothBrowseTab setTabs[] = { ClothBrowseTab::Trajes, ClothBrowseTab::Passes };
                            for (ClothBrowseTab tab : setTabs)
                            {
                                const int tabId = static_cast<int>(tab);
                                if (IconSidebarTab(("##set" + std::to_string(tabId)).c_str(), BrowseTabIcon(tab), SelectedCat == tabId))
                                {
                                    SelectedCat = tabId;
                                    g_Options.ClothChanger.Category = tabId;
                                }
                            }

                            SidebarDivider(a);
                            SidebarSectionLabel("PECA", a);

                            const ClothBrowseTab pieceTabs[] = {
                                ClothBrowseTab::Top, ClothBrowseTab::Bottom, ClothBrowseTab::Shoes,
                                ClothBrowseTab::Head, ClothBrowseTab::Mask, ClothBrowseTab::Facepaint
                            };
                            for (ClothBrowseTab tab : pieceTabs)
                            {
                                const int tabId = static_cast<int>(tab);
                                if (IconSidebarTab(("##piece" + std::to_string(tabId)).c_str(), BrowseTabIcon(tab), SelectedCat == tabId))
                                {
                                    SelectedCat = tabId;
                                    g_Options.ClothChanger.Category = tabId;
                                }
                            }

                            SidebarDivider(a);
                            SidebarSectionLabel("ON", a);
                            if (IconSidebarTab("##applied", BrowseTabIcon(ClothBrowseTab::Applied),
                                SelectedCat == static_cast<int>(ClothBrowseTab::Applied)))
                                SelectedCat = static_cast<int>(ClothBrowseTab::Applied);

                            ImGui::Dummy(ImVec2(0, 16.f));
                        }
                        ImGui::EndGroup();
                    }
                    ImGui::EndChild();

                    ImGui::SetCursorPos(ImVec2(sidebarW, AnimaTab));
                    ImGui::BeginChild("MainChild", ImVec2(Size.x - sidebarW, Size.y), ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
                    {
                        MouseMovement();

                        const float contentPad = 18.f;
                        ImGui::SetCursorPos(ImVec2(contentPad + Anima, 16.f));

                        ImGui::PushFont(InterBold);
                        ImGui::TextColored(ImColor(1.f, 1.f, 1.f, 0.95f * ImGui::GetStyle().Alpha), "%s",
                            ClothChanger::GetBrowseTabName(static_cast<ClothBrowseTab>(SelectedCat)));
                        ImGui::PopFont();

                        ImGui::SameLine();
                        ImGui::SetCursorPosX(ImGui::GetWindowSize().x - 280.f);
                        ImGui::PushFont(InterRegular);
                        ImGui::TextColored(
                            GameMemory::IsAttached()
                                ? ImColor(80, 200, 120, alpha)
                                : ImColor(255, 90, 90, alpha),
                            "%s", GameMemory::Status().c_str());
                        ImGui::PopFont();

                        if (!GameMemory::IsAttached())
                        {
                            ImGui::SameLine();
                            if (ImGui::Button("Reconnect", ImVec2(90, 22)))
                            {
                                std::thread([]() { GameMemory::Attach(false); }).detach();
                            }
                        }

                        {
                            const ClothBrowseTab tab = static_cast<ClothBrowseTab>(SelectedCat);
                            const char* subtitle = nullptr;
                            if (tab == ClothBrowseTab::Trajes)
                                subtitle = "Todos os conjuntos (Top+Bottom) · clique equipa completo";
                            else if (tab == ClothBrowseTab::Passes)
                                subtitle = "Passes premium M/F · do mais antigo ao mais novo";
                            else if (tab == ClothBrowseTab::Famous)
                                subtitle = "Conjuntos famosos · Fantasma, Mandela, etc.";
                            else if (tab == ClothBrowseTab::Applied)
                                subtitle = "Skins ativas no personagem";
                            else
                                subtitle = "Pecas individuais";

                            ImGui::SetCursorPos(ImVec2(contentPad + Anima, 38.f));
                            ImGui::PushFont(InterRegular);
                            ImGui::TextColored(ImColor(1.f, 1.f, 1.f, 0.38f * ImGui::GetStyle().Alpha), "%s", subtitle);
                            ImGui::PopFont();
                        }

                        ImGui::SetCursorPos(ImVec2(contentPad + Anima, 58.f));
                        ImGui::PushFont(InterRegular);
                        {
                            const bool memOk = ClothChanger::MemStatus().rfind("OK", 0) == 0;
                            ImGui::TextColored(ImColor(1.f, 1.f, 1.f, 0.35f * ImGui::GetStyle().Alpha), "%s", ClothChanger::LoadStatus().c_str());
                            ImGui::SameLine();
                            ImGui::TextColored(
                                memOk ? ImColor(80, 200, 120, alpha) : ImColor(255, 120, 90, alpha),
                                "| %s", ClothChanger::MemStatus().c_str());
                        }
                        ImGui::PopFont();

                        ImGui::SetCursorPos(ImVec2(contentPad + Anima, 82.f));

                        ImGui::PushFont(FontAwesomeSolid);
                        ImGui::TextColored(ImColor(255, 70, 175, alpha), ICON_FA_MAGNIFYING_GLASS);
                        ImGui::PopFont();
                        ImGui::SameLine();
                        ImGui::SetCursorPosY(ImGui::GetCursorPosY() - 2.f);
                        ImGui::InputTextEx("##SearchClothes", "Search skins...", g_Options.ClothChanger.Search, IM_ARRAYSIZE(g_Options.ClothChanger.Search), ImVec2(280, 32), ImGuiInputTextFlags_None);

                        ImGui::SameLine(0, 12.f);
                        if (ImGui::Button("Clear All", ImVec2(100, 32)))
                            ClothChanger::ClearAllCloths();

                        ImGui::SetCursorPos(ImVec2(contentPad + Anima, 128.f));
                        ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarSize, 14.f);
                        ImGui::BeginChild("ClothesList", ImVec2(Size.x - sidebarW - contentPad * 2.f, Size.y - 144.f),
                            ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
                        {
                            // Scroll mais rápido com a roda do mouse
                            if (ImGui::IsWindowHovered() && ImGui::GetIO().MouseWheel != 0.f)
                            {
                                const float boost = 90.f; // px extras por notch
                                ImGui::SetScrollY(ImGui::GetScrollY() - ImGui::GetIO().MouseWheel * boost);
                            }
                            if (SelectedCat == static_cast<int>(ClothBrowseTab::Applied))
                            {
                                const auto& applied = ClothChanger::Applied();
                                if (applied.empty())
                                {
                                    ImGui::SetCursorPosY(40.f);
                                    ImGui::PushFont(InterMedium);
                                    ImGui::TextColored(ImColor(1.f, 1.f, 1.f, 0.35f * ImGui::GetStyle().Alpha), "Nenhuma skin aplicada.");
                                    ImGui::PopFont();
                                }
                                else
                                {
                                    for (size_t i = 0; i < applied.size(); ++i)
                                    {
                                        const AppliedCloth& a = applied[i];
                                        ImGui::PushID(static_cast<int>(i));

                                        ImVec2 rowPos = ImGui::GetCursorScreenPos();
                                        ImVec2 rowSize(ImGui::GetContentRegionAvail().x - 14.f, 52.f);
                                        ImDrawList* dl = ImGui::GetWindowDrawList();

                                        dl->AddRectFilled(rowPos, rowPos + rowSize, ImColor(18, 18, 20, alpha), 8.f);
                                        dl->AddRect(rowPos, rowPos + rowSize, ImColor(32, 32, 36, alpha), 8.f);
                                        dl->AddRectFilled(rowPos, rowPos + ImVec2(4.f, rowSize.y), RarityColor(a.Rarity), 8.f, ImDrawFlags_RoundCornersLeft);

                                        ImVec2 iconBox = rowPos + ImVec2(12.f, 6.f);
                                        dl->AddRectFilled(iconBox, iconBox + ImVec2(40.f, 40.f), ImColor(28, 28, 32, alpha), 6.f);
                                        if (ID3D11ShaderResourceView* iconSrv = ClothIcons::Get(a.ClothID))
                                        {
                                            dl->AddImage((ImTextureID)iconSrv, iconBox + ImVec2(2, 2), iconBox + ImVec2(38.f, 38.f),
                                                ImVec2(0, 0), ImVec2(1, 1), ImColor(255, 255, 255, alpha));
                                        }

                                        ImGui::SetCursorScreenPos(rowPos + ImVec2(64.f, 10.f));
                                        ImGui::BeginGroup();
                                        ImGui::PushFont(InterSemiBold);
                                        ImGui::TextUnformatted(a.ClothDesc.c_str());
                                        ImGui::PopFont();
                                        ImGui::PushFont(InterRegular);
                                        ImGui::TextColored(ImColor(1.f, 1.f, 1.f, 0.4f * ImGui::GetStyle().Alpha), "%s",
                                            ClothChanger::GetCategoryName(static_cast<ClothCategory>(a.Category)));
                                        ImGui::PopFont();
                                        ImGui::EndGroup();

                                        ImGui::SameLine(rowSize.x - 90.f);
                                        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 8.f);
                                        if (ImGui::Button("Remove", ImVec2(78, 32)))
                                            ClothChanger::RemoveByCategory(a.Category);

                                        ImGui::SetCursorScreenPos(rowPos + ImVec2(0, rowSize.y + 8.f));
                                        ImGui::Dummy(ImVec2(0, 0));
                                        ImGui::PopID();
                                    }
                                }
                            }
                            else if (SelectedCat == static_cast<int>(ClothBrowseTab::Trajes)
                                  || SelectedCat == static_cast<int>(ClothBrowseTab::Passes)
                                  || SelectedCat == static_cast<int>(ClothBrowseTab::Famous))
                            {
                                const bool isPassTab = (SelectedCat == static_cast<int>(ClothBrowseTab::Passes));
                                const bool isFamousTab = (SelectedCat == static_cast<int>(ClothBrowseTab::Famous));
                                static bool outfitCacheInit = false;
                                static int cachedOutfitTab = -1;
                                static char cachedSearchOutfit[128] = {};
                                static std::vector<size_t> outfitIndices;
                                if (!outfitCacheInit || cachedOutfitTab != SelectedCat
                                    || strcmp(cachedSearchOutfit, g_Options.ClothChanger.Search) != 0)
                                {
                                    outfitCacheInit = true;
                                    cachedOutfitTab = SelectedCat;
                                    strncpy_s(cachedSearchOutfit, g_Options.ClothChanger.Search, _TRUNCATE);
                                    if (isFamousTab)
                                        outfitIndices = ClothChanger::FilterFamous(g_Options.ClothChanger.Search);
                                    else if (isPassTab)
                                        outfitIndices = ClothChanger::FilterPasses(g_Options.ClothChanger.Search);
                                    else
                                        outfitIndices = ClothChanger::FilterTrajes(g_Options.ClothChanger.Search);
                                    if (!outfitIndices.empty())
                                    {
                                        const auto& outfits = isFamousTab ? ClothChanger::Famous()
                                            : (isPassTab ? ClothChanger::Passes() : ClothChanger::Trajes());
                                        std::vector<uint32_t> prefetch;
                                        const size_t n = (std::min)(outfitIndices.size(), size_t{ 80 });
                                        prefetch.reserve(n);
                                        for (size_t i = 0; i < n; ++i)
                                            prefetch.push_back(outfits[outfitIndices[i]].IconItemID);
                                        ClothIcons::RequestMany(prefetch.data(), prefetch.size());
                                    }
                                }

                                const auto& outfits = isFamousTab ? ClothChanger::Famous()
                                    : (isPassTab ? ClothChanger::Passes() : ClothChanger::Trajes());
                                if (!ClothChanger::IsLoaded())
                                {
                                    ImGui::SetCursorPosY(40.f);
                                    ImGui::TextColored(ImColor(255, 70, 175, alpha), "Could not load clothes.json");
                                }
                                else if (outfitIndices.empty())
                                {
                                    ImGui::SetCursorPosY(40.f);
                                    ImGui::TextColored(ImColor(1.f, 1.f, 1.f, 0.35f * ImGui::GetStyle().Alpha),
                                        isFamousTab ? "Nenhum famoso encontrado."
                                        : (isPassTab ? "Nenhum passe encontrado." : "Nenhum traje encontrado."));
                                }
                                else
                                {
                                    ImGui::PushFont(InterRegular);
                                    ImGui::TextColored(ImColor(1.f, 1.f, 1.f, 0.35f * ImGui::GetStyle().Alpha),
                                        isFamousTab ? "%zu famosos" : (isPassTab ? "%zu passes" : "%zu trajes"),
                                        outfitIndices.size());
                                    ImGui::PopFont();
                                    ImGui::Dummy(ImVec2(0, 6.f));

                                    ImGuiListClipper clipper;
                                    clipper.Begin(static_cast<int>(outfitIndices.size()), 64.f);
                                    while (clipper.Step())
                                    {
                                        for (int n = clipper.DisplayStart; n < clipper.DisplayEnd; ++n)
                                        {
                                            const size_t oIdx = outfitIndices[n];
                                            const TrajeEntry& t = outfits[oIdx];
                                            ImGui::PushID(static_cast<int>(oIdx) + (isFamousTab ? 700000 : (isPassTab ? 800000 : 900000)));

                                            ImVec2 rowPos = ImGui::GetCursorScreenPos();
                                            ImVec2 rowSize(ImGui::GetContentRegionAvail().x - 14.f, 56.f);
                                            ImDrawList* dl = ImGui::GetWindowDrawList();
                                            const bool applied = isFamousTab
                                                ? ClothChanger::IsFamousApplied(oIdx)
                                                : (isPassTab
                                                    ? ClothChanger::IsPassApplied(oIdx)
                                                    : ClothChanger::IsTrajeApplied(oIdx));
                                            const bool hovered = ImGui::IsMouseHoveringRect(rowPos, rowPos + rowSize);

                                            ImU32 bg = applied ? ImColor(42, 16, 34, alpha)
                                                     : (hovered ? ImColor(26, 26, 30, alpha) : ImColor(16, 16, 18, alpha));
                                            dl->AddRectFilled(rowPos, rowPos + rowSize, bg, 10.f);
                                            dl->AddRect(rowPos, rowPos + rowSize,
                                                applied ? ImColor(255, 70, 175, (int)(alpha * 0.75f)) : ImColor(36, 36, 42, alpha), 10.f);
                                            dl->AddRectFilled(rowPos, rowPos + ImVec2(4.f, rowSize.y), RarityColor(t.Rarity), 10.f, ImDrawFlags_RoundCornersLeft);

                                            ImVec2 iconBox = rowPos + ImVec2(14.f, 6.f);
                                            const ImVec2 iconSize(44.f, 44.f);
                                            dl->AddRectFilled(iconBox, iconBox + iconSize, ImColor(28, 28, 32, alpha), 8.f);
                                            if (ID3D11ShaderResourceView* iconSrv = ClothIcons::Get(t.IconItemID))
                                            {
                                                dl->AddImage((ImTextureID)iconSrv, iconBox + ImVec2(2, 2), iconBox + iconSize - ImVec2(2, 2),
                                                    ImVec2(0, 0), ImVec2(1, 1), ImColor(255, 255, 255, alpha));
                                            }

                                            float textX = 70.f;
                                            if (isPassTab && !t.Name.empty())
                                            {
                                                char badge[32] = {};
                                                // "EP01 ..." / "BP25 ..."
                                                if ((t.Name[0] == 'E' || t.Name[0] == 'B') && t.Name.size() > 2 && t.Name[1] == 'P')
                                                {
                                                    size_t sp = t.Name.find(' ');
                                                    if (sp == std::string::npos || sp > 8)
                                                        sp = (std::min)(t.Name.size(), size_t(4));
                                                    snprintf(badge, sizeof(badge), "%.*s", (int)sp, t.Name.c_str());
                                                }
                                                else if (t.PassNumber > 0)
                                                {
                                                    snprintf(badge, sizeof(badge), "P%03d", t.PassNumber);
                                                }
                                                if (badge[0])
                                                {
                                                ImVec2 badgePos = rowPos + ImVec2(70.f, 10.f);
                                                ImVec2 badgeSz = ImGui::CalcTextSize(badge);
                                                ImVec2 pad(8.f, 3.f);
                                                dl->AddRectFilled(badgePos, badgePos + badgeSz + pad * 2.f,
                                                    ImColor(255, 70, 175, (int)(alpha * 0.85f)), 5.f);
                                                dl->AddText(badgePos + pad, ImColor(255, 255, 255, alpha), badge);
                                                textX = 70.f + badgeSz.x + pad.x * 2.f + 10.f;
                                                }
                                            }

                                            ImGui::SetCursorScreenPos(rowPos + ImVec2(textX, 10.f));
                                            ImGui::BeginGroup();
                                            ImGui::PushFont(InterSemiBold);
                                            ImGui::TextUnformatted(t.Name.c_str());
                                            ImGui::PopFont();
                                            ImGui::PushFont(InterRegular);
                                            if (isPassTab)
                                                ImGui::TextColored(ImColor(1.f, 1.f, 1.f, 0.38f * ImGui::GetStyle().Alpha),
                                                    "Equipa o passe completo  ·  %zu pecas", t.PieceIDs.size());
                                            else if (isFamousTab)
                                                ImGui::TextColored(ImColor(1.f, 1.f, 1.f, 0.38f * ImGui::GetStyle().Alpha),
                                                    "Conjunto famoso  ·  %zu pecas", t.PieceIDs.size());
                                            else if (t.PieceIDs.size() <= 1)
                                                ImGui::TextColored(ImColor(1.f, 1.f, 1.f, 0.38f * ImGui::GetStyle().Alpha),
                                                    "Traje completo (1 peca)");
                                            else
                                                ImGui::TextColored(ImColor(1.f, 1.f, 1.f, 0.38f * ImGui::GetStyle().Alpha),
                                                    "Traje completo  ·  %zu pecas", t.PieceIDs.size());
                                            ImGui::PopFont();
                                            ImGui::EndGroup();

                                            ImGui::SetCursorScreenPos(rowPos);
                                            if (ImGui::InvisibleButton("##outfitRow", rowSize))
                                            {
                                                if (isFamousTab)
                                                    ClothChanger::ApplyOrToggleFamous(oIdx);
                                                else if (isPassTab)
                                                    ClothChanger::ApplyOrTogglePass(oIdx);
                                                else
                                                    ClothChanger::ApplyOrToggleTraje(oIdx);
                                            }

                                            {
                                                const char* label = applied ? "ON" : "Equipar";
                                                ImVec2 ts = ImGui::CalcTextSize(label);
                                                ImVec2 btnMin = rowPos + ImVec2(rowSize.x - 96.f, 12.f);
                                                dl->AddRectFilled(btnMin, btnMin + ImVec2(82.f, 32.f),
                                                    applied ? ImColor(255, 70, 175, (int)(alpha * 210)) : ImColor(38, 38, 44, alpha), 7.f);
                                                dl->AddText(btnMin + ImVec2((82.f - ts.x) * 0.5f, (32.f - ts.y) * 0.5f),
                                                    ImColor(255, 255, 255, alpha), label);
                                            }

                                            ImGui::SetCursorScreenPos(rowPos + ImVec2(0, rowSize.y + 8.f));
                                            ImGui::Dummy(ImVec2(0, 0));
                                            ImGui::PopID();
                                        }
                                    }
                                }
                            }
                            else
                            {
                                const ClothCategory cat = BrowseToPieceCategory(static_cast<ClothBrowseTab>(SelectedCat));
                                static int cachedCat = -1;
                                static char cachedSearch[128] = {};
                                static std::vector<size_t> indices;

                                if (cachedCat != SelectedCat || strcmp(cachedSearch, g_Options.ClothChanger.Search) != 0)
                                {
                                    cachedCat = SelectedCat;
                                    strncpy_s(cachedSearch, g_Options.ClothChanger.Search, _TRUNCATE);
                                    indices = ClothChanger::FilterIndices(cat, g_Options.ClothChanger.Search);

                                    if (!indices.empty())
                                    {
                                        const auto& dbPrefetch = ClothChanger::DB();
                                        std::vector<uint32_t> prefetch;
                                        const size_t n = (std::min)(indices.size(), size_t{ 80 });
                                        prefetch.reserve(n);
                                        for (size_t i = 0; i < n; ++i)
                                            prefetch.push_back(dbPrefetch[indices[i]].ItemID);
                                        ClothIcons::RequestMany(prefetch.data(), prefetch.size());
                                    }
                                }

                                const auto& db = ClothChanger::DB();

                                if (!ClothChanger::IsLoaded())
                                {
                                    ImGui::SetCursorPosY(40.f);
                                    ImGui::PushFont(InterMedium);
                                    ImGui::TextColored(ImColor(255, 70, 175, alpha), "Could not load clothes.json");
                                    ImGui::PopFont();
                                }
                                else if (indices.empty())
                                {
                                    ImGui::SetCursorPosY(40.f);
                                    ImGui::TextColored(ImColor(1.f, 1.f, 1.f, 0.35f * ImGui::GetStyle().Alpha), "No skins found.");
                                }
                                else
                                {
                                    ImGui::PushFont(InterRegular);
                                    ImGui::TextColored(ImColor(1.f, 1.f, 1.f, 0.35f * ImGui::GetStyle().Alpha), "%zu items", indices.size());
                                    ImGui::PopFont();
                                    ImGui::Dummy(ImVec2(0, 4.f));

                                    ImGuiListClipper clipper;
                                    clipper.Begin(static_cast<int>(indices.size()), 60.f);
                                    while (clipper.Step())
                                    {
                                        for (int n = clipper.DisplayStart; n < clipper.DisplayEnd; ++n)
                                        {
                                            const ClothEntry& e = db[indices[n]];
                                            ImGui::PushID(static_cast<int>(e.ItemID));

                                            ImVec2 rowPos = ImGui::GetCursorScreenPos();
                                            ImVec2 rowSize(ImGui::GetContentRegionAvail().x - 14.f, 52.f);
                                            ImDrawList* dl = ImGui::GetWindowDrawList();
                                            const bool applied = ClothChanger::IsApplied(e.ItemID);
                                            const bool hovered = ImGui::IsMouseHoveringRect(rowPos, rowPos + rowSize);

                                            ImU32 bg = applied ? ImColor(40, 18, 32, alpha)
                                                     : (hovered ? ImColor(24, 24, 28, alpha) : ImColor(18, 18, 20, alpha));
                                            dl->AddRectFilled(rowPos, rowPos + rowSize, bg, 8.f);
                                            dl->AddRect(rowPos, rowPos + rowSize, applied ? ImColor(255, 70, 175, (int)(alpha * 0.7f)) : ImColor(32, 32, 36, alpha), 8.f);
                                            dl->AddRectFilled(rowPos, rowPos + ImVec2(4.f, rowSize.y), RarityColor(e.Rarity), 8.f, ImDrawFlags_RoundCornersLeft);

                                            ImVec2 iconBox = rowPos + ImVec2(12.f, 6.f);
                                            const ImVec2 iconSize(40.f, 40.f);
                                            dl->AddRectFilled(iconBox, iconBox + iconSize, ImColor(28, 28, 32, alpha), 6.f);

                                            if (ID3D11ShaderResourceView* iconSrv = ClothIcons::Get(e.ItemID))
                                            {
                                                dl->AddImage((ImTextureID)iconSrv, iconBox + ImVec2(2, 2), iconBox + iconSize - ImVec2(2, 2),
                                                    ImVec2(0, 0), ImVec2(1, 1), ImColor(255, 255, 255, alpha));
                                            }
                                            else
                                            {
                                                ImGui::PushFont(FontAwesomeSolid14);
                                                ImVec2 icSize = ImGui::CalcTextSize(CategoryIcon(cat));
                                                dl->AddText(iconBox + ImVec2((iconSize.x - icSize.x) * 0.5f, (iconSize.y - icSize.y) * 0.5f), RarityColor(e.Rarity), CategoryIcon(cat));
                                                ImGui::PopFont();
                                            }

                                            ImGui::SetCursorScreenPos(rowPos + ImVec2(64.f, 10.f));
                                            ImGui::BeginGroup();
                                            ImGui::PushFont(InterSemiBold);
                                            ImGui::TextUnformatted(e.Description.c_str());
                                            ImGui::PopFont();
                                            ImGui::PushFont(InterRegular);
                                            ImGui::TextColored(ImColor(1.f, 1.f, 1.f, 0.35f * ImGui::GetStyle().Alpha), "%s", e.Rarity.c_str());
                                            ImGui::PopFont();
                                            ImGui::EndGroup();

                                            ImGui::SetCursorScreenPos(rowPos);
                                            if (ImGui::InvisibleButton("##clothRow", rowSize))
                                                ClothChanger::ApplyOrToggle(e.ItemID);

                                            // Apply label on the right
                                            {
                                                const char* label = applied ? "ON" : "Apply";
                                                ImVec2 ts = ImGui::CalcTextSize(label);
                                                ImVec2 btnMin = rowPos + ImVec2(rowSize.x - 86.f, 10.f);
                                                ImVec2 btnMax = btnMin + ImVec2(74.f, 32.f);
                                                dl->AddRectFilled(btnMin, btnMax, applied ? ImColor(255, 70, 175, (int)(alpha * 200)) : ImColor(40, 40, 46, alpha), 6.f);
                                                dl->AddText(btnMin + ImVec2((74.f - ts.x) * 0.5f, (32.f - ts.y) * 0.5f),
                                                    ImColor(255, 255, 255, alpha), label);
                                            }

                                            ImGui::SetCursorScreenPos(rowPos + ImVec2(0, rowSize.y + 8.f));
                                            ImGui::Dummy(ImVec2(0, 0));
                                            ImGui::PopID();
                                        }
                                    }
                                }
                            }
                            // ---- Custom overlay scrollbar (draggable, always visible) ----
                            {
                                const float smax = ImGui::GetScrollMaxY();
                                if (smax > 0.5f)
                                {
                                    const ImVec2 wpos = ImGui::GetWindowPos();
                                    const float wh = ImGui::GetWindowHeight();
                                    const float ww = ImGui::GetWindowWidth();
                                    const float sy = ImGui::GetScrollY();

                                    const float barW = 6.f;
                                    const float barX = ww - barW - 3.f;
                                    const float trackTop = 4.f;
                                    const float trackH = wh - 8.f;

                                    const float viewRatio = wh / (wh + smax);
                                    const float grabH = (std::max)(34.f, trackH * viewRatio);
                                    float grabY = trackTop + (trackH - grabH) * (sy / smax);

                                    // Drag handling: invisible hit-area over the track (screen space)
                                    ImGui::SetCursorScreenPos(wpos + ImVec2(barX - 2.f, trackTop));
                                    ImGui::InvisibleButton("##vscroll", ImVec2(barW + 5.f, trackH));
                                    const bool active = ImGui::IsItemActive();
                                    const bool hovered = ImGui::IsItemHovered();
                                    if (active)
                                    {
                                        const float localY = ImGui::GetIO().MousePos.y - (wpos.y + trackTop) - grabH * 0.5f;
                                        float t = (trackH - grabH) > 0.f ? localY / (trackH - grabH) : 0.f;
                                        if (t < 0.f) t = 0.f;
                                        if (t > 1.f) t = 1.f;
                                        ImGui::SetScrollY(t * smax);
                                        grabY = trackTop + (trackH - grabH) * t;
                                    }

                                    ImDrawList* sdl = ImGui::GetWindowDrawList();
                                    sdl->AddRectFilled(wpos + ImVec2(barX, trackTop),
                                        wpos + ImVec2(barX + barW, trackTop + trackH),
                                        ImColor(20, 20, 24, (int)(alpha * 0.55f)), 3.f);
                                    const ImU32 grabCol = active ? ImColor(255, 70, 175, alpha)
                                        : (hovered ? ImColor(255, 105, 195, (int)(alpha * 0.9f))
                                                   : ImColor(255, 70, 175, (int)(alpha * 0.72f)));
                                    sdl->AddRectFilled(wpos + ImVec2(barX, grabY),
                                        wpos + ImVec2(barX + barW, grabY + grabH), grabCol, 3.f);
                                }
                            }
                        }
                        ImGui::EndChild();
                        ImGui::PopStyleVar(); // ScrollbarSize
                    }
                    ImGui::EndChild();

                }
            }
            ImGui::End();

        }
        ImGui::EndFrame();
        ImGui::Render();
        float clearColor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
        g_pd3dDeviceContext->OMSetRenderTargets(1, &g_mainRenderTargetView, nullptr);
        g_pd3dDeviceContext->ClearRenderTargetView(g_mainRenderTargetView, clearColor);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

        g_pSwapChain->Present(1, 0);
    }

    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    ClothIcons::Shutdown();

    CleanupDeviceD3D();
    DestroyWindow(hwnd);
    UnregisterClassW(wc.lpszClassName, wc.hInstance);

    return 0;
}

bool CreateDeviceD3D(HWND hWnd)
{
    DXGI_SWAP_CHAIN_DESC sd;
    ZeroMemory(&sd, sizeof(sd));
    sd.BufferCount = 2;
    sd.BufferDesc.Width = 0;
    sd.BufferDesc.Height = 0;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hWnd;
    sd.SampleDesc.Count = 1;
    sd.SampleDesc.Quality = 0;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    UINT createDeviceFlags = 0;
    D3D_FEATURE_LEVEL featureLevel;
    const D3D_FEATURE_LEVEL featureLevelArray[2] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0, };
    HRESULT res = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, createDeviceFlags, featureLevelArray, 2, D3D11_SDK_VERSION, &sd, &g_pSwapChain, &g_pd3dDevice, &featureLevel, &g_pd3dDeviceContext);
    if (res == DXGI_ERROR_UNSUPPORTED)
        res = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, createDeviceFlags, featureLevelArray, 2, D3D11_SDK_VERSION, &sd, &g_pSwapChain, &g_pd3dDevice, &featureLevel, &g_pd3dDeviceContext);
    if (res != S_OK)
        return false;

    CreateRenderTarget();
    return true;
}

void CleanupDeviceD3D()
{
    CleanupRenderTarget();
    if (g_pSwapChain) { g_pSwapChain->Release(); g_pSwapChain = nullptr; }
    if (g_pd3dDeviceContext) { g_pd3dDeviceContext->Release(); g_pd3dDeviceContext = nullptr; }
    if (g_pd3dDevice) { g_pd3dDevice->Release(); g_pd3dDevice = nullptr; }
}

void CreateRenderTarget()
{
    ID3D11Texture2D* pBackBuffer;
    g_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer));
    g_pd3dDevice->CreateRenderTargetView(pBackBuffer, nullptr, &g_mainRenderTargetView);
    pBackBuffer->Release();
}

void CleanupRenderTarget()
{
    if (g_mainRenderTargetView) { 
        g_mainRenderTargetView->Release();
        g_mainRenderTargetView = nullptr;
    }
}

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
        return true;

    switch (msg)
    {
    case WM_SIZE:
        if (wParam == SIZE_MINIMIZED)
            return 0;
        g_ResizeWidth = (UINT)LOWORD(lParam);
        g_ResizeHeight = (UINT)HIWORD(lParam);
        return 0;
    case WM_SYSCOMMAND:
        if ((wParam & 0xfff0) == SC_KEYMENU)
            return 0;
        break;
    case WM_DESTROY:
        ::PostQuitMessage(0);
        return 0;
    }
    return ::DefWindowProcW(hWnd, msg, wParam, lParam);
}
