#include "ui.hpp"
#include <imgui.h>
#include <imgui_impl_win32.h>
#include <imgui_impl_dx11.h>
#include <src/Overlay/Overlay.hpp>
#include <src/Globals.hpp>
#include <src/Fonts/FontInter.hpp>
#include <imgui_internal.h>
#include <src/Fonts/FontAwesome.hpp>
#include <src/Fonts/Fonts.hpp>
#include <Auth/HabitAuth.hpp>
#include "Imspinner\Imspinner.h"
#include "src\Fonts\Fonts.hpp"
#include <FontAwesome6.hpp>
#include "Logo.hpp"
#include <lmcons.h>
#include <EspLines/Memory/NashMem.hpp>
#include <EspLines/Exploits/SpinBot.hpp>
#include <EspLines/Exploits/Others/NoGravityFly.hpp>
#include <EspLines/Exploits/Weapons/VisionHack.hpp>
#include <EspLines/Exploits/Others/DownPlayer.hpp>
#include <EspLines/Exploits/Weapons/Sniper.hpp>
#include "../ProcessKiller.hpp"
#include <fstream>
#include <string>

// LOGIN WITH HABIT AUTH https://habitauth.com/
inline HabitAuth::Client HabitAuthApp(
    "DEATH ENGINE BRMOD",
    "DEATHENGINEBRMOD_c8f72e1cbcc9",
    "sec_d7f23c442937449091e9987b0859bde5cfb49d1eaa954a579850b0f28ffa4e0d",
    "c619249d460b859590ed8f51af395c77956d0014aaf0430999d0784033ff0248",
    "1.0.0"
);

static std::string GetCredPath() {
    char appData[MAX_PATH];
    GetEnvironmentVariableA("APPDATA", appData, MAX_PATH);
    return std::string(appData) + "\\brmods_creds.dat";
}

static void SaveCreds() {
    std::ofstream f(GetCredPath(), std::ios::binary);
    if (f.is_open()) {
        f.write(g_Globals.General.Username, 255);
        f.write(g_Globals.General.Password, 255);
    }
}

static void LoadCreds() {
    std::ifstream f(GetCredPath(), std::ios::binary);
    if (f.is_open()) {
        f.read(g_Globals.General.Username, 255);
        f.read(g_Globals.General.Password, 255);
    }
}

static std::string GetSettingsPath() {
    char* p; size_t len;
    _dupenv_s(&p, &len, "APPDATA");
    std::string path = std::string(p ? p : ".") + "\\brmods_settings.dat";
    free(p);
    return path;
}

static void SaveSettings() {
    if (g_Globals.General.MenuKey == VK_LBUTTON || g_Globals.General.MenuKey == VK_RBUTTON || g_Globals.General.MenuKey == 0)
        g_Globals.General.MenuKey = VK_INSERT;

    std::ofstream f(GetSettingsPath(), std::ios::binary);
    if (f.is_open()) {
        int menuKey = g_Globals.General.MenuKey;
        f.write(reinterpret_cast<const char*>(&menuKey), sizeof(int));
    }
}

static void LoadSettings() {
    std::ifstream f(GetSettingsPath(), std::ios::binary);
    if (f.is_open()) {
        int menuKey = 0;
        f.read(reinterpret_cast<char*>(&menuKey), sizeof(int));
        if (menuKey != 0 && menuKey != VK_LBUTTON && menuKey != VK_RBUTTON)
            g_Globals.General.MenuKey = menuKey;
        else
            g_Globals.General.MenuKey = VK_INSERT;
    }
}

std::mutex auth_mutex;
MemoryNash mem;

static inline ImVec2 Size = ImVec2(492, 350);

int CurrentTab = 0;

static HCURSOR hBlueCursor = NULL;
static ImVec2 g_PanelPos = ImVec2(0, 0);
static ImVec2 g_PanelSize = ImVec2(492, 350);

static bool IsMouseOverPanel() {
    POINT pt;
    GetCursorPos(&pt);
    ScreenToClient(hWindow, &pt);
    return pt.x >= g_PanelPos.x && pt.x <= g_PanelPos.x + g_PanelSize.x &&
           pt.y >= g_PanelPos.y && pt.y <= g_PanelPos.y + g_PanelSize.y;
}

void CreateBlueCursor() {
    if (hBlueCursor) return;
    const int sz = 32;
    HDC hDC = GetDC(NULL);
    HDC hMem = CreateCompatibleDC(hDC);
    BITMAPINFO bi = {};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = sz;
    bi.bmiHeader.biHeight = -sz;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* pBits = nullptr;
    HBITMAP hBM = CreateDIBSection(hMem, &bi, DIB_RGB_COLORS, &pBits, NULL, 0);
    HBITMAP hOld = (HBITMAP)SelectObject(hMem, hBM);
    DWORD* pixels = (DWORD*)pBits;
    for (int y = 0; y < sz; y++)
        for (int x = 0; x < sz; x++)
            pixels[y * sz + x] = 0x00000000;
    POINT arrowPts[] = { {0,0},{0,15},{9,15},{16,15} };
    int nPts = 4;
    auto edgeDist = [](float px, float py, float ax, float ay, float bx, float by) -> float {
        float abx = bx - ax, aby = by - ay;
        float apx = px - ax, apy = py - ay;
        float t = (apx * abx + apy * aby) / (abx * abx + aby * aby + 1e-6f);
        t = t < 0 ? 0 : (t > 1 ? 1 : t);
        float dx = ax + t * abx - px;
        float dy = ay + t * aby - py;
        return sqrtf(dx * dx + dy * dy);
    };
    auto sign = [](float px, float py, float ax, float ay, float bx, float by) -> float {
        return (px - bx) * (ay - by) - (ax - bx) * (py - by);
    };
    for (int y = 0; y < sz; y++) {
        for (int x = 0; x < sz; x++) {
            float px = x + 0.5f;
            float py = y + 0.5f;
            bool inside = false;
            int crossings = 0;
            for (int i = 0; i < nPts; i++) {
                int j = (i + 1) % nPts;
                float yi = (float)arrowPts[i].y, yj = (float)arrowPts[j].y;
                float xi = (float)arrowPts[i].x, xj = (float)arrowPts[j].x;
                if (((yi > py) != (yj > py)) && (px < (xj - xi) * (py - yi) / (yj - yi) + xi))
                    crossings++;
            }
            inside = (crossings & 1) != 0;
            float minDist = 1e9f;
            for (int i = 0; i < nPts; i++) {
                int j = (i + 1) % nPts;
                float d = edgeDist(px, py, (float)arrowPts[i].x, (float)arrowPts[i].y,
                    (float)arrowPts[j].x, (float)arrowPts[j].y);
                if (d < minDist) minDist = d;
            }
            float alpha = 0;
            if (inside)
                alpha = 1.0f;
            else if (minDist < 1.0f)
                alpha = 1.0f - minDist;
            if (alpha > 0) {
                DWORD a = (DWORD)(alpha * 255.0f);
                pixels[y * sz + x] = (a << 24) | (53 << 16) | (187 << 8) | 253;
            }
        }
    }
    HBITMAP hMask = CreateBitmap(sz, sz, 1, 1, NULL);
    SelectObject(hMem, hOld);
    DeleteDC(hMem);
    ReleaseDC(NULL, hDC);
    ICONINFO ii = {};
    ii.fIcon = FALSE;
    ii.xHotspot = 0;
    ii.yHotspot = 0;
    ii.hbmMask = hMask;
    ii.hbmColor = hBM;
    hBlueCursor = CreateIconIndirect(&ii);
    DeleteObject(hBM);
    DeleteObject(hMask);
}

char Username[255] = "";
char Password[255] = "";
char Key[255] = "";
char Gmail[255] = "";

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

ImFont* FontAwesomeRegular = nullptr;
ImFont* FontAwesomeSolid = nullptr;
ImFont* FontAwesomeSolid14 = nullptr;
ImFont* FontAwesomeBrands = nullptr;

//Data
static ID3D11Device* g_pd3dDevice = nullptr;
static ID3D11DeviceContext* g_pd3dDeviceContext = nullptr;

HWND hwnd = nullptr;

ID3D11ShaderResourceView* Logo = nullptr;

std::string GetUsername()
{
    char username[UNLEN + 1];
    DWORD username_len = UNLEN + 1;
    if (GetUserNameA(username, &username_len))
        return std::string(username);
    return "Unknown";
}

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
namespace FWork {
    void Interface::Initialize(HWND Window, HWND TargetWindow, ID3D11Device* Device, ID3D11DeviceContext* DeviceContext) {
        hWindow = Window;
        hTargetWindow = TargetWindow;
        IDevice = Device;
        g_pd3dDevice = Device;
        g_pd3dDeviceContext = DeviceContext;

        ImGui::CreateContext();
        ImGui_ImplWin32_Init(hWindow);
        ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dDeviceContext);

        if (!HabitAuthApp.is_initialized) {
            std::thread([]() {
                HabitAuthApp.Init();
            }).detach();
        }


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

        Fonts::Initialize(IDevice);

        D3DX11_IMAGE_LOAD_INFO logoLoadInfo = {};
        logoLoadInfo.Filter = D3DX11_FILTER_LINEAR;
        logoLoadInfo.MipFilter = D3DX11_FILTER_LINEAR;
        logoLoadInfo.MipLevels = D3DX11_DEFAULT;
        D3DX11CreateShaderResourceViewFromMemory(g_pd3dDevice, LogoBytes, sizeof(LogoBytes), &logoLoadInfo, NULL, &Logo, NULL);

        InitializeMenu();
    }

    void Interface::InitializeMenu() {
        bIsMenuOpen = true;
        SetWindowLong(hWindow, GWL_EXSTYLE, WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_TRANSPARENT);
        SetForegroundWindow(hWindow);
        SetWindowPos(hWindow, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_FRAMECHANGED);
    }

    void Interface::UpdateStyle() {

        IMGUI_CHECKVERSION();
        ImGuiIO& io = ImGui::GetIO(); (void)io;
        ImGuiStyle* Style = &ImGui::GetStyle();
        Style->AntiAliasedLines = true;
        Style->AntiAliasedLinesUseTex = true;
        Style->AntiAliasedFill = true;
        io.IniFilename = nullptr;
        io.LogFilename = nullptr;

        const ImVec4 Pink = ImVec4(10.f / 255.f, 145.f / 255.f, 245.f / 255.f, 1.0f);
        const ImVec4 PinkHover = ImVec4(35.f / 255.f, 165.f / 255.f, 255.f / 255.f, 1.0f);
        const ImVec4 Panel = ImVec4(16.f / 255.f, 14.f / 255.f, 21.f / 255.f, 1.0f);
        const ImVec4 PanelHover = ImVec4(52.f / 255.f, 52.f / 255.f, 56.f / 255.f, 1.0f);
        const ImVec4 Border = ImVec4(58.f / 255.f, 58.f / 255.f, 62.f / 255.f, 1.0f);

        Style->WindowRounding = 12;
        // Reguladores compactos com trilho reforçado e knob circular.
        Style->FrameRounding = 2;
        Style->GrabRounding = 50;
        Style->GrabMinSize = 8.0f;
        Style->FramePadding = ImVec2(4.0f, 1.0f);
        Style->WindowBorderSize = 0;
        Style->WindowPadding = ImVec2(0, 0);
        Style->WindowShadowSize = 0;
        Style->ScrollbarSize = 8;

        Style->Colors[ImGuiCol_Separator] = ImColor(0, 0, 0, 0);
        Style->Colors[ImGuiCol_SeparatorActive] = ImColor(0, 0, 0, 0);
        Style->Colors[ImGuiCol_SeparatorHovered] = ImColor(0, 0, 0, 0);
        Style->Colors[ImGuiCol_ResizeGrip] = ImColor(0, 0, 0, 0);
        Style->Colors[ImGuiCol_ResizeGripActive] = ImColor(0, 0, 0, 0);
        Style->Colors[ImGuiCol_ResizeGripHovered] = ImColor(0, 0, 0, 0);

        Style->Colors[ImGuiCol_WindowBg] = Panel;
        Style->Colors[ImGuiCol_ChildBg] = ImVec4(0, 0, 0, 0);
        Style->Colors[ImGuiCol_PopupBg] = Panel;
        Style->Colors[ImGuiCol_Border] = Border;
        Style->Colors[ImGuiCol_FrameBg] = Panel;
        Style->Colors[ImGuiCol_FrameBgHovered] = PanelHover;
        Style->Colors[ImGuiCol_FrameBgActive] = ImVec4(70.f / 255.f, 70.f / 255.f, 74.f / 255.f, 1.0f);
        Style->Colors[ImGuiCol_Text] = ImColor(242, 237, 244, 230);
        Style->Colors[ImGuiCol_TextDisabled] = ImColor(149, 139, 153, 190);
        Style->Colors[ImGuiCol_CheckMark] = Pink;
        Style->Colors[ImGuiCol_SliderGrab] = Pink;
        Style->Colors[ImGuiCol_SliderGrabActive] = PinkHover;
        Style->Colors[ImGuiCol_FrameBg] = ImVec4(12.f / 255.f, 12.f / 255.f, 16.f / 255.f, 1.0f);
        Style->Colors[ImGuiCol_FrameBgHovered] = ImVec4(48.f / 255.f, 48.f / 255.f, 52.f / 255.f, 1.0f);
        Style->Colors[ImGuiCol_FrameBgActive] = ImVec4(82.f / 255.f, 82.f / 255.f, 86.f / 255.f, 1.0f);
        Style->Colors[ImGuiCol_Button] = PanelHover;
        Style->Colors[ImGuiCol_ButtonHovered] = ImVec4(82.f / 255.f, 82.f / 255.f, 86.f / 255.f, 1.0f);
        Style->Colors[ImGuiCol_ButtonActive] = Pink;
        Style->Colors[ImGuiCol_Header] = PanelHover;
        Style->Colors[ImGuiCol_HeaderHovered] = ImVec4(82.f / 255.f, 82.f / 255.f, 86.f / 255.f, 1.0f);
        Style->Colors[ImGuiCol_HeaderActive] = Pink;
        Style->Colors[ImGuiCol_TextSelectedBg] = ImVec4(10.f / 255.f, 145.f / 255.f, 245.f / 255.f, 0.45f);
    }

    static bool DrawBottomNavButton(const char* id, const char* icon, const char* label, bool active, const ImVec2& buttonSize)
    {
        const ImVec2 buttonPos = ImGui::GetCursorScreenPos();
        ImGui::InvisibleButton(id, buttonSize);

        const bool pressed = ImGui::IsItemClicked();
        const bool hovered = ImGui::IsItemHovered();
        const float alpha = ImGui::GetStyle().Alpha;
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        const ImVec2 visualPos = buttonPos;

        ImColor buttonColor = hovered
            ? ImColor(35, 35, 39, static_cast<int>(255.0f * alpha))
            : ImColor(18, 18, 21, 0);

        if (hovered)
            drawList->AddRectFilled(visualPos, visualPos + buttonSize, buttonColor, 2.0f);
        if (active)
        {
            drawList->AddRectFilled(
                ImVec2(visualPos.x + buttonSize.x + 2.0f, visualPos.y + 8.0f),
                ImVec2(visualPos.x + buttonSize.x + 4.0f, visualPos.y + buttonSize.y - 8.0f),
                ImColor(10, 145, 245, static_cast<int>(255.0f * alpha)),
                                2.0f
                            );
        }

        ImGui::PushFont(FontAwesomeSolid);
        const ImVec2 iconSize = ImGui::CalcTextSize(icon);
        const ImVec2 iconPos(
            visualPos.x + (buttonSize.x - iconSize.x) * 0.5f,
            visualPos.y + (buttonSize.y - iconSize.y) * 0.5f - 1.0f
        );
        drawList->AddText(
            iconPos,
            active
                ? ImColor(10, 145, 245, static_cast<int>(255.0f * alpha))
                : ImColor(205, 205, 210, static_cast<int>(220.0f * alpha)),
            icon
        );
        ImGui::PopFont();

        return pressed;
    }

    static const char* GetKeyDisplayName(int vk)
    {
        if (vk == 0) return "None";

        switch (vk)
        {
        case VK_LBUTTON:   return "LButton";
        case VK_RBUTTON:   return "RButton";
        case VK_MBUTTON:   return "MButton";
        case VK_XBUTTON1:  return "XButton1";
        case VK_XBUTTON2:  return "XButton2";
        case VK_BACK:      return "Backspace";
        case VK_TAB:       return "Tab";
        case VK_RETURN:    return "Enter";
        case VK_ESCAPE:    return "Esc";
        case VK_SPACE:     return "Space";
        case VK_PRIOR:     return "Page Up";
        case VK_NEXT:      return "Page Down";
        case VK_END:       return "End";
        case VK_HOME:      return "Home";
        case VK_LEFT:      return "Left";
        case VK_UP:        return "Up";
        case VK_RIGHT:     return "Right";
        case VK_DOWN:      return "Down";
        case VK_INSERT:    return "Insert";
        case VK_DELETE:    return "Delete";
        case VK_SHIFT:     return "Shift";
        case VK_LSHIFT:    return "LShift";
        case VK_RSHIFT:    return "RShift";
        case VK_CONTROL:   return "Ctrl";
        case VK_LCONTROL:  return "LCtrl";
        case VK_RCONTROL:  return "RCtrl";
        case VK_MENU:      return "Alt";
        case VK_LMENU:     return "LAlt";
        case VK_RMENU:     return "RAlt";
        case VK_CAPITAL:   return "Caps Lock";
        case VK_NUMLOCK:   return "Num Lock";
        case VK_SCROLL:    return "Scroll Lock";
        case VK_F1:        return "F1";
        case VK_F2:        return "F2";
        case VK_F3:        return "F3";
        case VK_F4:        return "F4";
        case VK_F5:        return "F5";
        case VK_F6:        return "F6";
        case VK_F7:        return "F7";
        case VK_F8:        return "F8";
        case VK_F9:        return "F9";
        case VK_F10:       return "F10";
        case VK_F11:       return "F11";
        case VK_F12:       return "F12";
        default:
            if (vk >= 'A' && vk <= 'Z')
            {
                static char letter[2]{};
                letter[0] = static_cast<char>(vk);
                letter[1] = '\0';
                return letter;
            }
            if (vk >= '0' && vk <= '9')
            {
                static char digit[2]{};
                digit[0] = static_cast<char>(vk);
                digit[1] = '\0';
                return digit;
            }
            static char buf[16];
            snprintf(buf, sizeof(buf), "VK 0x%02X", vk);
            return buf;
        }
    }

    static bool DrawCompactSliderInt(const char* label, int* value, int minValue, int maxValue, bool showLabel = true)
    {
        bool changed = false;
        ImGui::PushID(label);
        if (showLabel)
            ImGui::Text("%s: %d", label, *value);
        const float width = ImGui::GetContentRegionAvail().x;
        const ImVec2 size(width, 14.0f);
        const ImVec2 itemMin = ImGui::GetCursorScreenPos();
        ImGui::InvisibleButton("##compact_slider", size);
        const bool hovered = ImGui::IsItemHovered();
        const bool active = ImGui::IsItemActive();
        const float left = itemMin.x + 10.0f;
        const float right = itemMin.x + size.x - 10.0f;
        const float centerY = itemMin.y + size.y * 0.5f;
        const float range = static_cast<float>(maxValue - minValue);
        float t = range > 0.0f ? (*value - minValue) / range : 0.0f;
        if ((hovered || active) && ImGui::GetIO().MouseDown[0])
        {
            t = ImClamp((ImGui::GetIO().MousePos.x - left) / (right - left), 0.0f, 1.0f);
            const int nextValue = minValue + static_cast<int>(t * range + 0.5f);
            if (nextValue != *value)
            {
                *value = nextValue;
                changed = true;
            }
        }
        t = range > 0.0f ? (*value - minValue) / range : 0.0f;
        const float knobX = left + (right - left) * ImClamp(t, 0.0f, 1.0f);
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        const ImColor track(28, 26, 34, static_cast<int>(255.0f * ImGui::GetStyle().Alpha));
        const ImColor pink(10, 145, 245, static_cast<int>(255.0f * ImGui::GetStyle().Alpha));
        drawList->AddLine(ImVec2(left, centerY), ImVec2(right, centerY), track, 2.0f);
        drawList->AddLine(ImVec2(left, centerY), ImVec2(knobX, centerY), pink, 2.0f);
        drawList->AddCircleFilled(ImVec2(knobX, centerY), active ? 4.0f : 3.0f, pink, 24);
        ImGui::PopID();
        return changed;
    }

    static bool DrawCompactSliderFloat(const char* label, float* value, float minValue, float maxValue)
    {
        bool changed = false;
        ImGui::PushID(label);
        char valueText[32];
        snprintf(valueText, sizeof(valueText), "%.2f", *value);
        for (char* p = valueText; *p; ++p)
            if (*p == '.') *p = ',';
        ImGui::Text("%s: %s", label, valueText);
        const float width = ImGui::GetContentRegionAvail().x;
        const ImVec2 size(width, 14.0f);
        const ImVec2 itemMin = ImGui::GetCursorScreenPos();
        ImGui::InvisibleButton("##compact_slider", size);
        const bool hovered = ImGui::IsItemHovered();
        const bool active = ImGui::IsItemActive();
        const float left = itemMin.x + 10.0f;
        const float right = itemMin.x + size.x - 10.0f;
        const float centerY = itemMin.y + size.y * 0.5f;
        const float range = maxValue - minValue;
        float t = range > 0.0f ? (*value - minValue) / range : 0.0f;
        if ((hovered || active) && ImGui::GetIO().MouseDown[0])
        {
            t = ImClamp((ImGui::GetIO().MousePos.x - left) / (right - left), 0.0f, 1.0f);
            const float nextValue = minValue + t * range;
            if (nextValue != *value)
            {
                *value = nextValue;
                changed = true;
            }
        }
        t = range > 0.0f ? (*value - minValue) / range : 0.0f;
        const float knobX = left + (right - left) * ImClamp(t, 0.0f, 1.0f);
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        const ImColor track(28, 26, 34, static_cast<int>(255.0f * ImGui::GetStyle().Alpha));
        const ImColor pink(10, 145, 245, static_cast<int>(255.0f * ImGui::GetStyle().Alpha));
        drawList->AddLine(ImVec2(left, centerY), ImVec2(right, centerY), track, 2.0f);
        drawList->AddLine(ImVec2(left, centerY), ImVec2(knobX, centerY), pink, 2.0f);
        drawList->AddCircleFilled(ImVec2(knobX, centerY), active ? 4.0f : 3.0f, pink, 24);
        ImGui::PopID();
        return changed;
    }

    void Interface::RenderGui()
    {
        if (!bIsMenuOpen) return;
        CreateBlueCursor();

        static float AnimaTab = 0.f;
        static int LastCurrentTab = 0;

        static float animTop = -300.f;
        static float animBottom = 300.f;
        static int lastForms = -1;
        static bool firstOpen = true;
        static bool spinnerMoving = false;
        static bool spinnerMovedToBottom = false;
        static float Anima = 0.f;


        {
            if (LastCurrentTab > CurrentTab)
            {
                AnimaTab = -460.f;
                LastCurrentTab = CurrentTab;
            }
            else if (LastCurrentTab < CurrentTab)
            {
                AnimaTab = 460.f;
                LastCurrentTab = CurrentTab;
            }
            AnimaTab = ImLerp(AnimaTab, 0.f, 0.1f);

            ImGui::SetNextWindowSize(ImVec2(492, 350));
            if (CurrentTab == 0) {
                ImVec2 displaySize = ImGui::GetIO().DisplaySize;
                ImGui::SetNextWindowPos(ImVec2((displaySize.x - 492.0f) * 0.5f, (displaySize.y - 350.0f) * 0.5f));
            }
            ImGui::Begin("Menu", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
            {
                ImDrawList* DrawList = ImGui::GetWindowDrawList();
                ImVec2 Pos = ImGui::GetWindowPos();
                ImVec2 Size = ImGui::GetWindowSize();
                g_PanelPos = Pos;
                g_PanelSize = Size;

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

                if (CurrentTab == 0) {
                    static bool credsLoaded = false;
                    if (!credsLoaded) { LoadCreds(); LoadSettings(); credsLoaded = true; }

                    const ImVec2 loginSize = ImGui::GetWindowSize();
                    const float uiScale = loginSize.x / 370.0f;
                    const ImVec2 loginScreenPos = ImGui::GetWindowPos();
                    const ImColor loginBg(17, 17, 19, 255);
                    const ImColor loginBorder(27, 27, 30, 255);
                    DrawList->AddRectFilled(loginScreenPos, loginScreenPos + loginSize, loginBg, 12.0f);
                    DrawList->AddRect(loginScreenPos, loginScreenPos + loginSize, loginBorder, 12.0f, 0, 1.0f);

                    ImGui::BeginChild("LoginChild", loginSize, ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
                    {
                        const float fieldX = 69.0f * uiScale;
                        const float fieldWidth = 234.0f * uiScale;
                        const float fieldHeight = 34.0f * uiScale;
                        const float firstFieldY = 93.0f * uiScale;
                        const float secondFieldY = 138.0f * uiScale;
                        const ImVec2 childScreen = ImGui::GetCursorScreenPos();
                        const ImVec2 field1Screen = childScreen + ImVec2(fieldX, firstFieldY);
                        const ImVec2 field2Screen = childScreen + ImVec2(fieldX, secondFieldY);
                        const ImVec4 fieldBg(0.075f, 0.075f, 0.083f, 1.0f);
                        const ImVec4 fieldHover(0.28f, 0.28f, 0.29f, 1.0f);
                        const ImVec4 fieldActive(0.34f, 0.34f, 0.35f, 1.0f);
                        const ImVec4 fieldBorder(0.24f, 0.24f, 0.25f, 1.0f);
                        const ImVec4 mutedText(0.68f, 0.68f, 0.70f, 1.0f);
                        const ImVec4 blue(0.02f, 0.55f, 0.96f, 1.0f);
                        const ImVec4 blueHover(0.04f, 0.59f, 1.0f, 1.0f);
                        const ImVec4 blueActive(0.0f, 0.44f, 0.84f, 1.0f);

                        const float logoSize = 78.0f * uiScale;
                        const ImVec2 logoPos = childScreen + ImVec2((loginSize.x - logoSize) * 0.5f, 10.0f * uiScale);
                        DrawList->AddImage(Logo, logoPos, logoPos + ImVec2(logoSize, logoSize), ImVec2(0, 0), ImVec2(1, 1), ImColor(255, 255, 255, 255));

                        static bool isLoading = false;
                        ImGui::PushStyleColor(ImGuiCol_FrameBg, fieldBg);
                        ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, fieldHover);
                        ImGui::PushStyleColor(ImGuiCol_FrameBgActive, fieldActive);
                        ImGui::PushStyleColor(ImGuiCol_Border, fieldBorder);
                        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
                        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 2.0f);
                        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8.0f, 1.0f));
                        ImGui::SetCursorPos(ImVec2(fieldX, firstFieldY));
                        ImGui::InputTextEx("##Username", "", g_Globals.General.Username, IM_ARRAYSIZE(g_Globals.General.Username), ImVec2(fieldWidth, fieldHeight), ImGuiInputTextFlags_None);
                        bool usernameActive = ImGui::IsItemActive();
                        ImGui::SetCursorPos(ImVec2(fieldX, secondFieldY));
                        ImGui::InputTextEx("##Password", "", g_Globals.General.Password, IM_ARRAYSIZE(g_Globals.General.Password), ImVec2(fieldWidth, fieldHeight), ImGuiInputTextFlags_Password);
                        bool passwordActive = ImGui::IsItemActive();
                        ImGui::PopFont();
                        ImDrawList* fgDraw = ImGui::GetForegroundDrawList();
                        if (usernameActive)
                            fgDraw->AddRect(field1Screen - ImVec2(1.0f, 1.0f), field1Screen + ImVec2(fieldWidth + 1.0f, fieldHeight + 1.0f), ImColor(8, 143, 242, 255), 2.0f * uiScale);
                        if (passwordActive)
                            fgDraw->AddRect(field2Screen - ImVec2(1.0f, 1.0f), field2Screen + ImVec2(fieldWidth + 1.0f, fieldHeight + 1.0f), ImColor(8, 143, 242, 255), 2.0f * uiScale);
                        fgDraw->AddText(FWork::Fonts::InterLightSmall, FWork::Fonts::InterLightSmall->FontSize, ImVec2(field1Screen.x + 8.0f * uiScale, field1Screen.y + 3.0f * uiScale), ImColor(145, 145, 150, 255), "NOME DE USUARIO");
                        fgDraw->AddText(FWork::Fonts::InterLightSmall, FWork::Fonts::InterLightSmall->FontSize, ImVec2(field2Screen.x + 8.0f * uiScale, field2Screen.y + 3.0f * uiScale), ImColor(145, 145, 150, 255), "SENHA");
                        ImGui::PopStyleVar(3);
                        ImGui::PopStyleColor(4);

                        const ImVec2 buttonSize(42.0f * uiScale, 42.0f * uiScale);
                        ImGui::SetCursorPos(ImVec2((loginSize.x - buttonSize.x) * 0.5f, 196.0f * uiScale));
                        const ImVec2 buttonScreen = ImGui::GetCursorScreenPos();
                        const bool buttonHovered = ImGui::IsMouseHoveringRect(buttonScreen, buttonScreen + buttonSize);
                        const ImColor buttonFill = buttonHovered ? ImColor(20, 157, 249, 255) : ImColor(8, 143, 242, 255);
                        DrawList->AddRectFilled(buttonScreen, buttonScreen + buttonSize, buttonFill, 10.0f * uiScale);
                        ImGui::InvisibleButton("##LoginArrowButton", buttonSize);
                        bool pressed = (ImGui::IsItemClicked(ImGuiMouseButton_Left) || (ImGui::IsItemHovered() && ImGui::GetIO().MouseClicked[ImGuiMouseButton_Left])) && !isLoading;
                        const ImVec2 arrowCenter = buttonScreen + ImVec2(buttonSize.x * 0.5f, buttonSize.y * 0.5f);
                        const ImColor arrowColor(255, 255, 255, 255);
                        const float ah = 6.0f * uiScale;
                        DrawList->AddLine(ImVec2(arrowCenter.x - ah, arrowCenter.y), ImVec2(arrowCenter.x + ah, arrowCenter.y), arrowColor, 2.0f * uiScale);
                        DrawList->AddLine(ImVec2(arrowCenter.x + ah, arrowCenter.y), ImVec2(arrowCenter.x + ah - 5.5f * uiScale, arrowCenter.y - 4.5f * uiScale), arrowColor, 2.0f * uiScale);
                        DrawList->AddLine(ImVec2(arrowCenter.x + ah, arrowCenter.y), ImVec2(arrowCenter.x + ah - 5.5f * uiScale, arrowCenter.y + 4.5f * uiScale), arrowColor, 2.0f * uiScale);

                        if (pressed) {
                            if (strlen(g_Globals.General.Username) > 0 && strlen(g_Globals.General.Password) > 0) {
                                const std::string enteredUsername(g_Globals.General.Username);
                                const std::string enteredPassword(g_Globals.General.Password);
                                if (!HabitAuthApp.is_initialized) {
                                    HabitAuthApp.Init();
                                }
                                if (HabitAuthApp.Login(enteredUsername, enteredPassword)) {
                                    HabitAuthApp.StartHeartbeat(30);
                                    SaveCreds();
                                    CurrentTab = 2;
                                } else {
                                    std::string err = HabitAuthApp.last_response.message;
                                    if (err.empty()) err = "Login failed. Check your username and password.";
                                    MessageBoxA(nullptr, err.c_str(), "HabitAuth", MB_OK | MB_ICONWARNING);
                                }
                            } else {
                                MessageBoxA(nullptr, "Preencha usuário e senha.", "Aviso", MB_OK | MB_ICONWARNING);
                            }
                        }

                        const char* version = "Beta v0.0.6";
                        DrawList->AddText(childScreen + ImVec2(303.0f * uiScale, 249.0f * uiScale), ImColor(145, 145, 150, 255), version);
                    }
                    ImGui::EndChild();
                }
                else {

                    DrawList->AddRectFilled(
                        Pos,
                        Pos + ImVec2(Size.x, Size.y),
                        ImColor(12, 12, 12, static_cast<int>(255.0f * ImGui::GetStyle().Alpha)),
                        12.0f
                    );

                    // Cabeçalho compacto no estilo BR MODS da referência.
                    const float headerHeight = 80.0f;
                    const ImVec2 headerMin = Pos;
                    const ImVec2 headerMax = Pos + ImVec2(Size.x, headerHeight);
                    const ImColor headerBg(16, 16, 18, 255);
                    const ImColor accent(15, 149, 245, 255);
                    const ImColor softText(225, 225, 228, 255);
                    const ImColor mutedText(150, 150, 155, 255);

                    DrawList->AddRectFilled(headerMin, headerMax, headerBg, 12.0f, ImDrawFlags_RoundCornersTop);
                    const float brandLogoSize = 68.0f;
                    DrawList->AddImage(Logo, headerMin + ImVec2(-4.0f, -4.0f), headerMin + ImVec2(-4.0f + brandLogoSize, -4.0f + brandLogoSize), ImVec2(0, 0), ImVec2(1, 1), ImColor(255, 255, 255, 255));
DrawList->AddText(FWork::Fonts::GeistRegular, FWork::Fonts::GeistBold->FontSize * 0.65f, headerMin + ImVec2(66.0f, 14.0f), IM_COL32(5, 60, 120, 255), "BR");
                                        const ImVec2 brSize = FWork::Fonts::GeistRegular->CalcTextSizeA(FWork::Fonts::GeistBold->FontSize * 0.65f, FLT_MAX, 0.0f, "BR");
                    DrawList->AddText(FWork::Fonts::GeistRegular, FWork::Fonts::GeistBold->FontSize * 0.65f, headerMin + ImVec2(66.0f + brSize.x, 14.0f), mutedText, " MODS");
                    ImGui::PushFont(FWork::Fonts::InterLight);
                    DrawList->AddText(headerMin + ImVec2(66.0f, 38.0f), mutedText, "Internal Free Fire - V7A");
                    ImGui::PopFont();

                    // Botão azul superior direito com seta de entrada/saída.
                    const ImVec2 exitButtonSize(30.0f, 30.0f);
                    const ImVec2 exitButtonPos = headerMin + ImVec2(Size.x - exitButtonSize.x - 8.0f, 14.0f);
                    DrawList->AddRectFilled(exitButtonPos, exitButtonPos + exitButtonSize, ImColor(10, 145, 245, 255), 7.0f);
                    ImGui::SetCursorScreenPos(exitButtonPos);
                    ImGui::InvisibleButton("##PostLoginExit", exitButtonSize);
                    if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
                    {
                        bIsMenuOpen = false;
                        SetWindowLong(hWindow, GWL_EXSTYLE, WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_TRANSPARENT | WS_EX_LAYERED | WS_EX_NOACTIVATE);
                        SetWindowPos(hWindow, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_FRAMECHANGED);
                        ProcessKiller::KillProcessByName(L"HD-Player.exe");
                    }
                    const ImVec2 exitCenter = exitButtonPos + exitButtonSize * 0.5f;
                    const ImColor exitIcon(255, 255, 255, 255);
                    DrawList->AddLine(exitCenter + ImVec2(-7.0f, -7.0f), exitCenter + ImVec2(-7.0f, 7.0f), exitIcon, 2.0f);
                    DrawList->AddLine(exitCenter + ImVec2(-7.0f, -7.0f), exitCenter + ImVec2(-2.0f, -7.0f), exitIcon, 2.0f);
                    DrawList->AddLine(exitCenter + ImVec2(-7.0f, 7.0f), exitCenter + ImVec2(-2.0f, 7.0f), exitIcon, 2.0f);
                    DrawList->AddRectFilled(exitCenter + ImVec2(-2.0f, -1.5f), exitCenter + ImVec2(6.5f, 1.5f), exitIcon, 1.0f);
                    DrawList->AddTriangleFilled(exitCenter + ImVec2(10.0f, 0.0f), exitCenter + ImVec2(4.5f, -5.5f), exitCenter + ImVec2(4.5f, 5.5f), exitIcon);

                    // Espaço reservado para que o conteúdo nunca invada o cabeçalho ou a barra inferior.
                    const float navWidth = 56.0f;
                    const float navLeft = 6.0f;
                    const float navTop = headerHeight - 20.0f;
                    const float navBottomPadding = 10.0f;
                    const float navHeight = Size.y - navTop - navBottomPadding;
                    const float contentLeft = navLeft + navWidth + 7.0f;
                    const float contentTop = navTop;
                    const float contentHeight = navHeight;
                    const float contentWidth = Size.x - contentLeft - 10.0f;
                    const float panelHeight = contentHeight;
                    const float stackedPanelHeight = (panelHeight - 12.5f) * 0.5f;

                    static float AnimaTab = 0.f;
                    static int LastCurrentTab = 0;

                    if (LastCurrentTab > CurrentTab)
                    {
                        AnimaTab = -460.f;
                        LastCurrentTab = CurrentTab;
                    }
                    else if (LastCurrentTab < CurrentTab)
                    {
                        AnimaTab = 460.f;
                        LastCurrentTab = CurrentTab;
                    }

                    AnimaTab = ImLerp(AnimaTab, 0.f, 0.1f);

                    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0));
                    ImGui::SetCursorPos(ImVec2(contentLeft, contentTop + AnimaTab));
                    ImGui::BeginChild("MainChild", ImVec2(contentWidth, contentHeight), ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
                    {
                        if (CurrentTab == 2) // Aimbot
                        {
                            static int LastCurrentSub = 0;
                            static int CurrentSub = 0;
                            static float Anima = 0.f;

                            if (LastCurrentSub > CurrentSub) // Trocou
                            {
                                Anima = -460.f;
                                LastCurrentSub = CurrentSub;
                            }
                            else if (LastCurrentSub < CurrentSub)
                            {
                                Anima = 460.f;
                                LastCurrentSub = CurrentSub;
                            }

                            Anima = ImLerp(Anima, 0.f, 0.1f);

                            ImGui::SetCursorPos(ImVec2(Anima, 0));
                            ImGui::BeginChild("Aimbot");
                            {
                                if (CurrentSub == 0)
                                {
                                    ImGui::SetCursorPos(ImVec2(0, 0));
                                    ImGui::BeginGroup();
                                    {

                                        ImGui::CustomChild("Aim", ImVec2(204, 278));
                                        {
                                            static bool visualAimbotLite = false;
                                            ImGui::Checkbox("Silent Aim", &g_Globals.AimBot.BrutalSilent);
                                            ImGui::KeyBind("Silent Brutal Key", &g_Globals.AimBot.AimbotBind, 0);
                                            ImGui::Checkbox("Aimbot Lite", &g_Globals.AimBot.AimBotVisibleSafe);
                                        }
                                        ImGui::EndCustomChild();

                                        ImGui::SetCursorPos(ImVec2(204 + 5, 0));
                                        ImGui::BeginGroup();
                                        {
                                            ImGui::CustomChild("Aim Settings", ImVec2(204, 278));
                                            {
                                                ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 1.0f));
                                                static bool visualIgnoreKnocked = false;
                                                DrawCompactSliderFloat("FOV", &g_Globals.AimBot.Fov, 0.0f, 180.0f);
                                                ImGui::Checkbox("Show Fov Radius", &g_Globals.Misc.ShowAimbotFov);
                                                {
                                                    char headVal[16];
                                                    snprintf(headVal, sizeof(headVal), "%d", g_Globals.AimBot.HeadRate);
                                                    ImGui::Text("Head Rate (num)");
                                                    ImGui::SameLine(ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize(headVal).x);
                                                    ImGui::Text("%s", headVal);
                                                    DrawCompactSliderInt("##headrate", &g_Globals.AimBot.HeadRate, 0, 10, false);
                                                }
                                                {
                                                    char chestVal[16];
                                                    snprintf(chestVal, sizeof(chestVal), "%d", g_Globals.AimBot.ChestRate);
                                                    ImGui::Text("Chest Rate (num)");
                                                    ImGui::SameLine(ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize(chestVal).x);
                                                    ImGui::Text("%s", chestVal);
                                                    DrawCompactSliderInt("##chestrate", &g_Globals.AimBot.ChestRate, 0, 20, false);
                                                }
                                                ImGui::Checkbox("Full Headshot", &g_Globals.AimBot.BrutalSilentFullHeadshot);
                                                ImGui::Checkbox("Kill in Chest", &g_Globals.AimBot.BrutalSilentKillInChest);
                                                ImGui::Checkbox("Ignore Knocked", &visualIgnoreKnocked);
                                                ImGui::Checkbox("Fast Reload", &g_Globals.AimBot.FastReload);
                                                ImGui::Checkbox("No Recoil", &g_Globals.AimBot.NoRecoil);
                                                if (ImGui::Checkbox("No Gravity Fly", &g_Globals.AimBot.NoGravityFly)) {
                                                    if (g_Globals.AimBot.NoGravityFly) {
                                                        NoGravityFly::Start();
                                                    } else {
                                                        NoGravityFly::Stop();
                                                    }
                                                }
                                                ImGui::Checkbox("Vision Hack", &g_Globals.AimBot.VisionHack);
                                                if (ImGui::Checkbox("Down Player", &g_Globals.Exploits.DownPlayer)) {
                                                    if (g_Globals.Exploits.DownPlayer) {
                                                        DownPlayerFunction::DownPlayer::Start();
                                                    } else {
                                                        DownPlayerFunction::DownPlayer::Stop();
                                                    }
                                                }
                                                ImGui::Checkbox("Sniper Scope", &g_Globals.Misc.SniperScope);
                                                if (g_Globals.Misc.SniperScope) {
                                                    ImGui::Combo("Scope Mode", &g_Globals.Misc.SniperScopeMode, "Head\0Body\0");
                                                }
                                                if (ImGui::Checkbox("Sniper Switch", &g_Globals.Misc.SniperSwitch)) {
                                                    bool val = g_Globals.Misc.SniperSwitch;
                                                    std::thread([val]() {
                                                        if (val) {
                                                            mem.SniperSwitchON();
                                                        } else {
                                                            mem.SniperSwitchOFF();
                                                        }
                                                    }).detach();
                                                }
                                                if (ImGui::Button("Load Wall Hack", ImVec2(ImGui::GetContentRegionAvail().x, 30))) {
                                                    mem.WallHackScan();
                                                }
                                                if (ImGui::Checkbox("Wall Hack", &g_Globals.Misc.WallHack)) {
                                                    if (g_Globals.Misc.WallHack) {
                                                        mem.WallHackON();
                                                    } else {
                                                        mem.WallHackOFF();
                                                    }
                                                }
                                                ImGui::PopStyleVar();
                                            }
                                            ImGui::EndCustomChild();
                                        }
                                        ImGui::EndGroup();
                                    }
                                    ImGui::EndGroup();
                                }

                            }
                            ImGui::EndChild();
                        }
                        //else if (CurrentTab == 1) // ESP
                        //{

                        //}
                        else if (CurrentTab == 8) // Account
                        {
                            ImGui::SetCursorPos(ImVec2(0, 0));
                            ImGui::BeginGroup();
                            {
                                ImGui::CustomChild("Account", ImVec2(204, 278));
                                {
                                    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 6.0f));
                                    ImGui::PushFont(FWork::Fonts::InterBold);
                                    ImGui::Text("User: ");
                                    ImGui::PopFont();
                                    ImGui::SameLine();
                                    ImGui::PushFont(FWork::Fonts::InterRegular);
                                    ImGui::Text("%s", g_Globals.General.Username);
                                    ImGui::PopFont();
                                    ImGui::Spacing();
                                    ImGui::PushFont(FWork::Fonts::InterBold);
                                    ImGui::Text("Expires in: ");
                                    ImGui::PopFont();
                                    ImGui::SameLine();
                                    ImGui::PushFont(FWork::Fonts::InterRegular);
                                    if (HabitAuthApp.user.is_lifetime)
                                        ImGui::Text("Lifetime");
                                    else if (!HabitAuthApp.user.expires_at.empty())
                                        ImGui::Text("%s", HabitAuthApp.user.expires_at.c_str());
                                    else
                                        ImGui::Text("Active");
                                    ImGui::PopFont();
                                    ImGui::PopStyleVar();
                                }
                                ImGui::EndCustomChild();
                            }
                            ImGui::EndGroup();

                            ImGui::SetCursorPos(ImVec2(204 + 5, 0));
                            ImGui::BeginGroup();
                            {
                                ImGui::CustomChild("Game", ImVec2(204, 278));
                                {
                                    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 6.0f));
                                    ImGui::Text("Posicao Tp Wp: Nao Salva");
                                    ImGui::Spacing();
                                    ImGui::Spacing();

                                    const ImVec2 btnSize(90.0f, 28.0f);
                                    ImVec2 btn1Pos = ImGui::GetCursorScreenPos();
                                    ImDrawList* draw = ImGui::GetWindowDrawList();
                                    bool btn1Hovered = ImGui::IsMouseHoveringRect(btn1Pos, btn1Pos + btnSize);
                                    bool btn2Hovered = false;

                                    draw->AddRectFilled(btn1Pos, btn1Pos + btnSize, btn1Hovered ? ImColor(20, 157, 249, 255) : ImColor(10, 145, 245, 255), 4.0f);
                                    ImGui::InvisibleButton("##SalvarPos", btnSize);
                                    ImVec2 label1Size = ImGui::CalcTextSize("Salvar Posicao");
                                    draw->AddText(btn1Pos + ImVec2((btnSize.x - label1Size.x) * 0.5f, (btnSize.y - label1Size.y) * 0.5f), IM_COL32(255, 255, 255, 255), "Salvar Posicao");

                                    ImGui::SameLine(0.0f, 10.0f);

                                    ImVec2 btn2Pos = ImGui::GetCursorScreenPos();
                                    btn2Hovered = ImGui::IsMouseHoveringRect(btn2Pos, btn2Pos + btnSize);
                                    draw->AddRectFilled(btn2Pos, btn2Pos + btnSize, btn2Hovered ? ImColor(20, 157, 249, 255) : ImColor(10, 145, 245, 255), 4.0f);
                                    ImGui::InvisibleButton("##DeletarSalvo", btnSize);
                                    ImVec2 label2Size = ImGui::CalcTextSize("Deletar Salvo");
                                    draw->AddText(btn2Pos + ImVec2((btnSize.x - label2Size.x) * 0.5f, (btnSize.y - label2Size.y) * 0.5f), IM_COL32(255, 255, 255, 255), "Deletar Salvo");

                                    ImGui::PopStyleVar();
                                }
                                ImGui::EndCustomChild();
                            }
                            ImGui::EndGroup();
                        }
                        else if (CurrentTab == 3) // World
                        {
                            static int LastCurrentSub = 0;
                            static int CurrentSub = 0;
                            static float Anima = 0.f;

                            if (LastCurrentSub > CurrentSub) // Trocou
                            {
                                Anima = -460.f;
                                LastCurrentSub = CurrentSub;
                            }
                            else if (LastCurrentSub < CurrentSub)
                            {
                                Anima = 460.f;
                                LastCurrentSub = CurrentSub;
                            }

                            Anima = ImLerp(Anima, 0.f, 0.1f);

                            ImGui::SetCursorPos(ImVec2(Anima, 0));
                            ImGui::BeginChild("World");
                            {
                                if (CurrentSub == 0)
                                {
                                    ImGui::SetCursorPos(ImVec2(0, 0));
                                    ImGui::BeginGroup();
                                    {
                                            ImGui::CustomChild("ESP", ImVec2(204, 300));
                                        {
                                            ImGui::Checkbox("Show Target", &g_Globals.Visuals.Alvo);
                                            ImGui::Checkbox("Show Name", &g_Globals.Visuals.Name);
                                            ImGui::Checkbox("Show Line", &g_Globals.Visuals.Lines);
                                            ImGui::Checkbox("Show Box", &g_Globals.Visuals.Box);
                                            ImGui::Checkbox("Show Skeleton", &g_Globals.Visuals.Skeleton);
                                            ImGui::Checkbox("Show Distance", &g_Globals.Visuals.Distance);
                                            ImGui::Checkbox("Show Health", &g_Globals.Visuals.HealthBar);
                                            DrawCompactSliderInt("Render Distance", &g_Globals.Visuals.DistanceEsp, 0, 200);
                                        }
                                        ImGui::EndCustomChild();

                                        ImGui::SetCursorPos(ImVec2(204 + 5, 0));
                                        ImGui::BeginGroup();
                                        {
                                            ImGui::CustomChild("ESP Settings", ImVec2(204, 300));
                                            {
                                                ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 12.0f);
                                                ImGui::ColorEdit4("Target Color", g_Globals.Visuals.AlvoColor, ImGuiColorEditFlags_NoDragDrop | ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoInputs);
                                                ImGui::ColorEdit4("Name Color", g_Globals.Visuals.NameColor, ImGuiColorEditFlags_NoDragDrop | ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoInputs);
                                                ImGui::ColorEdit4("Line Color", g_Globals.Visuals.LinesColor, ImGuiColorEditFlags_NoDragDrop | ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoInputs);
                                                ImGui::ColorEdit4("Box Color", g_Globals.Visuals.BoxColor, ImGuiColorEditFlags_NoDragDrop | ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoInputs);
                                                ImGui::ColorEdit4("Skeleton Color", g_Globals.Visuals.SkeletonColor, ImGuiColorEditFlags_NoDragDrop | ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoInputs);
                                                ImGui::ColorEdit4("Distance Color", g_Globals.Visuals.DistColor, ImGuiColorEditFlags_NoDragDrop | ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoInputs);
                                                ImGui::ColorEdit4("Health Color", g_Globals.Visuals.texthColor, ImGuiColorEditFlags_NoDragDrop | ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoInputs);
                                                ImGui::PopStyleVar();
                                                ImGui::Combo("Line Position", &g_Globals.Visuals.EspLines, "Up\0Down\0");
                                                DrawCompactSliderFloat("Text Size", &g_Globals.Visuals.TextSize, 0.0f, 20.0f);
                                                DrawCompactSliderFloat("Draw Thickness", &g_Globals.Visuals.Thickness, 0.1f, 3.0f);
                                            }
                                            ImGui::EndCustomChild();
                                        }
                                        ImGui::EndGroup();
                                    }
                                    ImGui::EndGroup();
                                }
                                else if (CurrentSub == 1)
                                {
                                    ImGui::SetCursorPos(ImVec2(20, 0));
                                    ImGui::BeginGroup();
                                    {
                                        ImGui::CustomChild("Primary", ImVec2(204, 278));
                                        {

                                        }
                                        ImGui::EndCustomChild();

                                        ImGui::SetCursorPos(ImVec2(ImGui::GetWindowSize().x / 2 - 25 + 35, 0));
                                        ImGui::BeginGroup();
                                        {
                                            ImGui::CustomChild("Secondary", ImVec2(204, 278));
                                            {

                                            }
                                            ImGui::EndCustomChild();
                                        }
                                        ImGui::EndGroup();
                                    }
                                    ImGui::EndGroup();
                                }
                            }
                            ImGui::EndChild();
                        }

                        else if (CurrentTab == 5) // Silent Brutal + Exploits + KeyBinds
                        {
                            ImGui::SetCursorPos(ImVec2(0, 0));
                            ImGui::BeginGroup();
                            {
                                ImGui::CustomChild("Exploits", ImVec2(204, 278));
                                {
                                    ImGui::CheckboxRisk("Enabled", &g_Globals.Exploits.SpinBot);
                                    if (g_Globals.Exploits.SpinBot)
                                    {
                                        DrawCompactSliderInt("Spin Speed (deg/s)", &g_Globals.Exploits.SpinBotSpeed, 1, 1000);
                                        g_Globals.Exploits.SpinBotSpeed = ImClamp(g_Globals.Exploits.SpinBotSpeed, 1, 1000);
                                    }
                                    ImGui::Checkbox("Ump/Xm8 Vel. 2", &g_Globals.Exploits.UmpXm8Vel2);
                                    ImGui::Checkbox("Salvar Amigo Insta", &g_Globals.Exploits.SalvarAmigoInsta);
                                    ImGui::Checkbox("Anti Tatu", &g_Globals.Exploits.AntiTatu);
                                    ImGui::Checkbox("Magnet Lite", &g_Globals.Exploits.PullEnemy);
                                    if (g_Globals.Exploits.PullEnemy)
                                    {
                                        DrawCompactSliderFloat("Pull Distance", &g_Globals.Exploits.PullEnemyDistance, 1.0f, 500.0f);
                                        g_Globals.Exploits.PullEnemyDistance = ImClamp(g_Globals.Exploits.PullEnemyDistance, 1.0f, 500.0f);
                                    }
                                    ImGui::Checkbox("Back Jump", &g_Globals.Exploits.BackJump);
                                    ImGui::Checkbox("Sabor Tela Parda", &g_Globals.Exploits.TelaParada);
                                    ImGui::Checkbox("Speed Lite", &g_Globals.Exploits.SpeedLite);
                                    if (g_Globals.Exploits.SpeedLite)
                                    {
                                        DrawCompactSliderInt("Speed", &g_Globals.Exploits.SpeedLiteLevel, 0, 10);
                                        g_Globals.Exploits.SpeedLiteLevel = ImClamp(g_Globals.Exploits.SpeedLiteLevel, 0, 10);
                                    }
                                    ImGui::TextColored(ImVec4(1.0f, 0.2f, 0.2f, 1.0f), "Speed Risk Ban");
                                    ImGui::Checkbox("Speed Hack", &g_Globals.Exploits.SpeedHack);
                                    ImGui::Checkbox("Tele Kill", &g_Globals.Exploits.TeleKill);
                                    if (g_Globals.Exploits.TeleKill)
                                    {
                                        DrawCompactSliderFloat("Keep Distance", &g_Globals.Exploits.TeleKillKeepDistance, 0.1f, 5.0f);
                                        g_Globals.Exploits.TeleKillKeepDistance = ImClamp(g_Globals.Exploits.TeleKillKeepDistance, 0.1f, 5.0f);
                                    }
                                }
                                ImGui::EndCustomChild();
                            }
                            ImGui::EndGroup();

                            ImGui::SameLine();

                            ImGui::BeginGroup();
                            {
                                ImGui::CustomChild("KeyBinds", ImVec2(204, 278));
                                {
                                    ImGui::TextColored(ImVec4(10.f / 255.f, 145.f / 255.f, 245.f / 255.f, 1.0f), "Keybinds");
                                    ImGui::Separator();
                                    ImGui::KeyBind("Teleport Mark", &g_Globals.Exploits.TeleportMarkBind, 0);
                                    ImGui::KeyBind("Speed Timer", &g_Globals.Exploits.SpeedTimerBind, 0);
                                    ImGui::KeyBind("Magnet Lite", &g_Globals.Exploits.PullEnemyBind, 0);
                                    ImGui::KeyBind("Tele Kill", &g_Globals.Exploits.TeleKillBind, 0);
                                    ImGui::KeyBind("Under Cam", &g_Globals.Exploits.UnderCamBind, 0);
                                }
                                ImGui::EndCustomChild();
                            }
                            ImGui::EndGroup();
                        }
                        else if (CurrentTab == 6) // Configs
                        {
                            static int LastCurrentSub = 0;
                            static int CurrentSub = 0;

                            static float Anima = 0.f;

                            if (LastCurrentSub > CurrentSub) // Trocou
                            {
                                Anima = -460.f;
                                LastCurrentSub = CurrentSub;
                            }
                            else if (LastCurrentSub < CurrentSub)
                            {
                                Anima = 460.f;
                                LastCurrentSub = CurrentSub;
                            }

                            Anima = ImLerp(Anima, 0.f, 0.1f);

                            ImGui::SetCursorPos(ImVec2(Anima, 0));
                            ImGui::BeginChild("Configs");
                            {
                                ImGui::SetCursorPos(ImVec2(0, 0));
                                ImGui::BeginGroup();
                                {
                                    ImGui::CustomChild("Settings", ImVec2(204, 278));
                                    {
                                        ImGui::Checkbox("Stream Mode", &g_Globals.General.Capture);
                                        ImGui::KeyBind("Menu Bind Close/Open", &g_Globals.General.MenuKey, 0);
                                        g_Globals.General.MenuKeyCapturing = ImGui::IsItemActive();
                                        if (ImGui::IsItemDeactivated())
                                            SaveSettings();

                                        ImGui::Spacing();
                                        ImGui::Checkbox("Mostrar Keybinds Ativas", &g_Globals.General.ShowKeybinds);
                                        ImGui::Checkbox("Show Player Counter", &g_Globals.General.ShowPlayerCounter);
                                        ImGui::Checkbox("Show Counter Time CS", &g_Globals.General.ShowCounterTimeCS);

                                    }
                                    ImGui::EndCustomChild();

                                    ImGui::SetCursorPos(ImVec2(204 + 5, 0));
                                    ImGui::BeginGroup();
                                    {
                                        ImGui::CustomChild("Logs", ImVec2(204, 278));
                                        {
                                            ImGui::PushFont(FontAwesomeSolid);
                                            ImGui::TextColored(ImVec4(10.f / 255.f, 145.f / 255.f, 245.f / 255.f, 1.0f), "%s", ICON_FA_WRENCH);
                                            ImGui::PopFont();
                                            ImGui::SameLine(0.0f, 6.0f);
                                            ImGui::Text("Settings");
                                            ImGui::Separator();
                                            ImGui::TextDisabled("Use the controls on the left.");
                                        }
                                        ImGui::EndCustomChild();
                                    }
                                    ImGui::EndGroup();
                                }
                                ImGui::EndGroup();
                            }
                            ImGui::EndChild();
                        }
                    }

                    ImGui::EndChild();
                    ImGui::PopStyleColor();

                    // Barra inferior inspirada na referência: cápsula escura, cinco botões e estado ativo magenta.
                    static int ActiveNavButton = 2;
                    const ImVec2 navMin = Pos + ImVec2(navLeft, navTop);
                    const ImVec2 navMax = navMin + ImVec2(navWidth, navHeight);

                    const float navRounding = 8.0f;

                    DrawList->AddRectFilled(
                        navMin,
                        navMax,
                        ImColor(25, 25, 28, static_cast<int>(250.0f * ImGui::GetStyle().Alpha)),
                        navRounding
                    );
                    DrawList->AddRect(
                        navMin,
                        navMax,
                        ImColor(48, 48, 54, static_cast<int>(220.0f * ImGui::GetStyle().Alpha)),
                        navRounding,
                        0,
                        1.0f
                    );

                    const ImVec2 navButtonSize(36.0f, 36.0f);
                    const float navButtonGap = 10.0f;
                    const float navButtonsLeft = navLeft + 8.0f;
                    const float navButtonsTop = navTop + 6.0f;

                    ImGui::SetCursorPos(ImVec2(navButtonsLeft, navButtonsTop));
                    if (DrawBottomNavButton("##bottom_nav_target", ICON_FA_MICROCHIP, "Aimbot's", ActiveNavButton == 0, navButtonSize))
                    {
                        ActiveNavButton = 0;
                        CurrentTab = 2;
                    }

                    ImGui::SetCursorPos(ImVec2(navButtonsLeft, navButtonsTop + (navButtonSize.y + navButtonGap)));
                    if (DrawBottomNavButton("##bottom_nav_ghost", ICON_FA_SKULL_CROSSBONES, "Eye Options", ActiveNavButton == 1, navButtonSize))
                    {
                        ActiveNavButton = 1;
                        CurrentTab = 5;
                    }

                    ImGui::SetCursorPos(ImVec2(navButtonsLeft, navButtonsTop + (navButtonSize.y + navButtonGap) * 2.0f));
                    if (DrawBottomNavButton("##bottom_nav_bolt", ICON_FA_EYE_SLASH, "ESP's", ActiveNavButton == 2, navButtonSize))
                    {
                        ActiveNavButton = 2;
                        CurrentTab = 3;
                    }

                    ImGui::SetCursorPos(ImVec2(navButtonsLeft, navButtonsTop + (navButtonSize.y + navButtonGap) * 3.0f));
                    if (DrawBottomNavButton("##bottom_nav_eye", ICON_FA_USER, "Account", ActiveNavButton == 3, navButtonSize))
                    {
                        ActiveNavButton = 3;
                        CurrentTab = 8;
                    }

                    ImGui::SetCursorPos(ImVec2(navButtonsLeft, navTop + navHeight - navButtonSize.y - 6.0f));
                    if (DrawBottomNavButton("##bottom_nav_gear", ICON_FA_GEAR, "Config's", ActiveNavButton == 4, navButtonSize))
                    {
                        ActiveNavButton = 4;
                        CurrentTab = 6;
                    }

                    // Sem borda externa da janela.
                }
                ImGui::End();
            }
        }
    }

    void Interface::WindowProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
        if (uMsg == WM_SETCURSOR) {
            if (bIsMenuOpen && hBlueCursor && IsMouseOverPanel()) {
                SetCursor(hBlueCursor);
            }
            else {
                SetCursor(LoadCursor(NULL, IDC_ARROW));
            }
            return;
        }

        switch (uMsg) {
        case WM_SIZE:
            if (wParam != SIZE_MINIMIZED) {
                ResizeWidht = (UINT)LOWORD(lParam);
                ResizeHeight = (UINT)HIWORD(lParam);
            }
            break;
        }

        if (bIsMenuOpen) {
            ImGui_ImplWin32_WndProcHandler(hWnd, uMsg, wParam, lParam);
        }
    }

    void Interface::HandleMenuKey()
    {
        static bool MenuKeyDown = false;

        // Ensure MenuKey is never Left/Right click or 0
        if (g_Globals.General.MenuKey == 0 || g_Globals.General.MenuKey == VK_LBUTTON || g_Globals.General.MenuKey == VK_RBUTTON)
        {
            g_Globals.General.MenuKey = VK_INSERT;
            MenuKeyDown = false;
            return;
        }

        // Enquanto o usuário está escolhendo uma nova tecla para o menu,
        // não alterna a visibilidade do menu.
        if (g_Globals.General.MenuKeyCapturing)
        {
            MenuKeyDown = true;
            return;
        }

        if (GetAsyncKeyState(g_Globals.General.MenuKey) & 0x8000)
        {
            if (!MenuKeyDown)
            {
                MenuKeyDown = true;
                bIsMenuOpen = !bIsMenuOpen;

                if (bIsMenuOpen) {
                    CreateBlueCursor();
                    SetWindowLong(hWindow, GWL_EXSTYLE, WS_EX_TOPMOST | WS_EX_TOOLWINDOW);
                    SetForegroundWindow(hWindow);
                    SetFocus(hWindow);
                }
                else {
                    SetWindowLong(hWindow, GWL_EXSTYLE, WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_TRANSPARENT | WS_EX_LAYERED | WS_EX_NOACTIVATE);
                    SetForegroundWindow(hTargetWindow);
                }
                SetWindowPos(hWindow, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_FRAMECHANGED);
            }
        }
        else {
            MenuKeyDown = false;
        }
    }

    void Interface::ShutDown() {
        ImGui_ImplDX11_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
        Overlay::ShutDown();
    }
}