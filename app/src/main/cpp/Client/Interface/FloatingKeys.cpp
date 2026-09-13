#include "FloatingKeys.hpp"

#include <imgui.h>
#include <Fonts/Fonts.hpp>
#include <Globals.hpp>
#include <PanelApp.hpp>

#include <cmath>
#include <cstring>
#include <mutex>
#include <cstdio>

/*
 * ============================================================================
 * FloatingKeys.cpp
 * ============================================================================
 * Implementação dos botões flutuantes de keybind (ver o header pra arquitetura).
 * ============================================================================
 */

namespace
{
    /*
     * Rects do último DrawTick — lidos pelo JNI (main.cpp).
     */
    int s_Bounds[FloatingKeys::kMaxKeys * 5] = { 0 };
    int s_BoundsCount = 0;

    struct FloatingKey
    {
        int vk = 0;
        char title[24] = {};   // texto do painel ("Aim", "Silent"...)
        bool state = false;    // estado toggle (modo toque)
        bool finger = false;   // dedo atualmente pressionado (modo segurar)
        float x = 0, y = 0, w = 0, h = 0;
    };

    static std::mutex g_Mutex;
    static std::vector<FloatingKey> g_Keys;
    static int g_NextSlot = 0;

    const char* SlotLabel(int slot)
    {
        static char buf[8][8];
        static int rot = 0;
        rot = (rot + 1) & 7;
        snprintf(buf[rot], sizeof(buf[rot]), "F%d", slot + 1);
        return buf[rot];
    }

    FloatingKey* FindLocked(int vk)
    {
        for (auto& k : g_Keys)
            if (k.vk == vk)
                return &k;
        return nullptr;
    }

    void LayoutLocked(int screenW, int screenH)
    {
        /*
         * Coluna compacta no canto DIREITO da tela, começando um pouco
         * acima do centro. Se a tela estiver de cabeça pra baixo ou com
         * tamanho zero (boot), mantém o layout anterior.
         */
        if (screenW <= 0 || screenH <= 0)
            return;

        const float w = 76.0f;
        const float h = 40.0f;
        const float gap = 10.0f;
        const float margin = 14.0f;

        float x = (float)screenW - w - margin;
        float y = (float)screenH * 0.28f;

        for (auto& k : g_Keys)
        {
            k.x = x;
            k.y = y;
            k.w = w;
            k.h = h;
            y += h + gap;
        }
    }
}

namespace FloatingKeys
{

int Acquire(const char* label)
{
    std::lock_guard<std::mutex> lock(g_Mutex);

    FloatingKey k;
    k.vk = kVkBase + (g_NextSlot++ & 0xFF);

    if (label && *label)
    {
        /*
         * Tira o sufixo "Key" do label do widget ("AimKey" -> "Aim")
         * pra ficar bonito no botão, e limita a 22 chars.
         */
        std::string t = label;
        if (t.size() > 3 && t.compare(t.size() - 3, 3, "Key") == 0)
            t.resize(t.size() - 3);

        strncpy(k.title, t.c_str(), sizeof(k.title) - 1);
    }
    else
    {
        strncpy(k.title, "?", sizeof(k.title) - 1);
    }

    g_Keys.push_back(k);
    return k.vk;
}

void Release(int vk)
{
    if (vk < kVkBase)
        return;

    std::lock_guard<std::mutex> lock(g_Mutex);

    for (size_t i = 0; i < g_Keys.size(); i++)
    {
        if (g_Keys[i].vk == vk)
        {
            g_Keys.erase(g_Keys.begin() + i);
            return;
        }
    }
}

const char* VkLabel(int vk)
{
    if (vk < kVkBase)
        return "None";

    std::lock_guard<std::mutex> lock(g_Mutex);

    FloatingKey* k = FindLocked(vk);
    return k ? SlotLabel(k->vk - kVkBase) : "None";
}

const char* VkTitle(int vk)
{
    std::lock_guard<std::mutex> lock(g_Mutex);

    FloatingKey* k = FindLocked(vk);
    return k ? k->title : nullptr;
}

bool Exists(int vk)
{
    if (vk < kVkBase)
        return false;

    std::lock_guard<std::mutex> lock(g_Mutex);
    return FindLocked(vk) != nullptr;
}

bool IsDown(int vk)
{
    if (vk < kVkBase)
        return false;

    std::lock_guard<std::mutex> lock(g_Mutex);

    FloatingKey* k = FindLocked(vk);
    if (!k)
        return false;

    if (g_Globals.General.FloatingKeysHold)
        return k->finger;

    return k->state;
}

void OnTouch(int vk, bool down)
{
    std::lock_guard<std::mutex> lock(g_Mutex);

    FloatingKey* k = FindLocked(vk);
    if (!k)
        return;

    if (down)
    {
        k->finger = true;

        if (!g_Globals.General.FloatingKeysHold)
            k->state = !k->state;   // modo toque: alterna no press
    }
    else
    {
        k->finger = false;
    }
}

void DrawTick(int screenW, int screenH, int* outBounds, int boundsMax, int& boundsCount)
{
    (void)outBounds;
    (void)boundsMax;
    boundsCount = 0;
    s_BoundsCount = 0;

    std::lock_guard<std::mutex> lock(g_Mutex);

    if (g_Keys.empty())
        return;

    LayoutLocked(screenW, screenH);

    ImDrawList* dl = ImGui::GetForegroundDrawList();
    if (!dl)
        return;

    const ImGuiIO& io = ImGui::GetIO();

    for (auto& k : g_Keys)
    {
        const bool active = g_Globals.General.FloatingKeysHold
            ? k.finger
            : k.state;

        const bool hovered =
            io.MousePos.x >= k.x && io.MousePos.x <= k.x + k.w &&
            io.MousePos.y >= k.y && io.MousePos.y <= k.y + k.h;
        const bool pressed = hovered && io.MouseDown[0];

        ImVec2 mn(k.x, k.y);
        ImVec2 mx(k.x + k.w, k.y + k.h);

        // Sombra + corpo
        dl->AddRectFilled(mn + ImVec2(0, 3), mx + ImVec2(0, 3), IM_COL32(0, 0, 0, 70), 12.0f);
        dl->AddRectFilled(mn, mx,
            active ? IM_COL32(46, 10, 10, 235) : IM_COL32(16, 16, 16, 200), 12.0f);

        // Borda: acesa em vermelho quando ativo
        dl->AddRect(mn, mx,
            active ? IM_COL32(200, 0, 0, 255)
                   : IM_COL32(90, 90, 95, 180),
            12.0f, 0, pressed ? 2.5f : 1.5f);

        // Bolinha de estado
        ImVec2 dot(k.x + 14.0f, k.y + k.h * 0.5f);
        dl->AddCircleFilled(dot, 4.0f,
            active ? IM_COL32(200, 0, 0, 255) : IM_COL32(70, 70, 70, 255), 16);
        if (active)
            dl->AddCircleFilled(dot, 7.0f, IM_COL32(200, 0, 0, 40), 16);

        // Texto (título do painel + rótulo F#)
        ImGui::PushFont(Fonts::InterMedium);
        char line[40];
        snprintf(line, sizeof(line), "%s", k.title);
        ImVec2 ts = ImGui::CalcTextSize(line);
        dl->AddText(ImVec2(k.x + 24.0f, k.y + (k.h - ts.y) * 0.5f),
            active ? IM_COL32(255, 235, 235, 255) : IM_COL32(200, 200, 205, 235), line);
        ImGui::PopFont();

        // Área de toque pro Java (mesma rect, um tiquinho maior)
        {
            int o = s_BoundsCount;
            if (o + 5 <= (int)(sizeof(s_Bounds) / sizeof(int)))
            {
                s_Bounds[o + 0] = k.vk;
                s_Bounds[o + 1] = (int)(k.x - 4);
                s_Bounds[o + 2] = (int)(k.y - 4);
                s_Bounds[o + 3] = (int)(k.w + 8);
                s_Bounds[o + 4] = (int)(k.h + 8);
                s_BoundsCount += 5;
            }
        }
    }

    boundsCount = s_BoundsCount;
}

void GetBounds(int* outArr, int maxInts, int& outCount)
{
    std::lock_guard<std::mutex> lock(g_Mutex);

    outCount = 0;

    if (!outArr || maxInts <= 0)
        return;

    const int n = (s_BoundsCount < maxInts) ? s_BoundsCount : maxInts;

    for (int i = 0; i < n; i++)
        outArr[i] = s_Bounds[i];

    outCount = n;
}

int Count()
{
    std::lock_guard<std::mutex> lock(g_Mutex);
    return (int)g_Keys.size();
}

void Clear()
{
    std::lock_guard<std::mutex> lock(g_Mutex);
    g_Keys.clear();
}

} // namespace FloatingKeys
