#include "FloatingKeys.hpp"

#include <imgui.h>
#include <Fonts/Fonts.hpp>
#include <Globals.hpp>
#include <PanelApp.hpp>

#include <cmath>
#include <cstring>
#include <mutex>
#include <cstdio>
#include <string>

/*
 * ============================================================================
 * FloatingKeys.cpp
 * ============================================================================
 * Implementação dos botões flutuantes de keybind (ver o header pra arquitetura).
 *
 * MUDANÇAS DESSA VERSÃO:
 *   - UM BOTÃO FIXO POR FUNÇÃO: Acquire reaproveita o botão de mesmo
 *     título em vez de criar um novo — acabou a numeração F1, F2, F3, F4
 *     acumulando a cada spawn/despawn. O vk é reciclado do menor slot
 *     livre quando o botão é recriado (irrelevante na UI, que não mostra
 *     número algum: o box do painel mostra "Show"/"Hide" e o botão
 *     mostra o nome da função).
 *   - SINCRONIZAÇÃO BIDIRECIONAL com o painel: Bind(vk, bool*) liga o
 *     botão ao toggle da função. Tocar no botão alterna o bool; o
 *     SyncFromPanel (1x/frame no DrawTick) espelha o bool no botão.
 *   - BOTÕES MAIORES: 96x52 (antes 76x40), gap 12, bolinha e texto
 *     maiores pra facilitar o toque durante o jogo.
 *
 * Mantido das versões anteriores:
 *   - ARRASTO: cada botão guarda um offset (ox, oy) somado ao layout
 *     base. O Java detecta o arrasto (deslize > slop), chama DragCancel()
 *     e passa os deltas em MoveBy(). O botão fica onde foi solto.
 *   - O toque SEMPRE chega pela janelinha Java de cada botão
 *     (OverlayService::syncFloatingKeys), não depende do io.MousePos
 *     do painel.
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
        bool dragging = false; // toque virou arrasto
        bool pendingToggle = false; // toggle aplicado no DOWN (desfeito se virar drag)
        float ox = 0, oy = 0;  // offset do arrasto (relativo ao layout base)
        float x = 0, y = 0, w = 0, h = 0;

        /*
         * SINCRONIZAÇÃO: ponteiro pro bool do toggle da função no painel
         * (ex.: &g_Globals.AimBot.Enabled). nullptr = botão solto, sem
         * ligação com o painel (só estado interno).
         */
        bool* bound = nullptr;
    };

    static std::mutex g_Mutex;
    static std::vector<FloatingKey> g_Keys;

    FloatingKey* FindLocked(int vk)
    {
        for (auto& k : g_Keys)
            if (k.vk == vk)
                return &k;
        return nullptr;
    }

    /*
     * Preenche o título a partir do label do widget ("AimKey" -> "Aim").
     */
    void MakeTitle(const char* label, char (&out)[24])
    {
        std::string t = (label && *label) ? label : "?";

        if (t.size() > 3 && t.compare(t.size() - 3, 3, "Key") == 0)
            t.resize(t.size() - 3);

        memset(out, 0, sizeof(out));
        strncpy(out, t.c_str(), sizeof(out) - 1);
    }

    /*
     * Direção PAINEL -> BOTÃO: espelha o bool da função no estado visual
     * do botão. Só faz sentido no modo toque (no modo segurar o botão é
     * momentâneo, controlado pelo dedo).
     */
    void SyncFromPanelLocked()
    {
        if (g_Globals.General.FloatingKeysHold)
            return;

        for (auto& k : g_Keys)
            if (k.bound)
                k.state = *k.bound ? true : false;
    }

    void LayoutLocked(int screenW, int screenH)
    {
        /*
         * Coluna no canto DIREITO da tela, começando um pouco acima do
         * centro. Cada botão soma o offset do arrasto (ox/oy) e o
         * resultado é preso dentro da tela. Se a tela estiver de cabeça
         * pra baixo ou com tamanho zero (boot), mantém o layout anterior.
         */
        if (screenW <= 0 || screenH <= 0)
            return;

        /*
         * BOTÕES MAIORES (pedido do usuário): 96x52, gap 12.
         * Fácil de acertar durante a partida.
         */
        const float w = 96.0f;
        const float h = 52.0f;
        const float gap = 12.0f;
        const float margin = 14.0f;

        const float baseX = (float)screenW - w - margin;
        const float baseY = (float)screenH * 0.24f;

        int slot = 0;

        for (auto& k : g_Keys)
        {
            k.w = w;
            k.h = h;

            float x = baseX + k.ox;
            float y = baseY + slot * (h + gap) + k.oy;

            if (x < 0.0f) x = 0.0f;
            if (y < 0.0f) y = 0.0f;
            if (x > (float)screenW - w) x = (float)screenW - w;
            if (y > (float)screenH - h) y = (float)screenH - h;

            k.x = x;
            k.y = y;

            slot++;
        }
    }
}

namespace FloatingKeys
{

int Acquire(const char* label)
{
    std::lock_guard<std::mutex> lock(g_Mutex);

    char title[24];
    MakeTitle(label, title);

    /*
     * UM BOTÃO POR FUNÇÃO: já existe botão com esse título? Reaproveita —
     * mesmo vk, sem duplicar, sem numeração acumulando.
     */
    for (auto& k : g_Keys)
    {
        if (strncmp(k.title, title, sizeof(k.title)) == 0)
            return k.vk;
    }

    /*
     * Pool cheio: falha explícita (o widget mantém *Key como estava).
     */
    if ((int)g_Keys.size() >= kMaxKeys)
        return 0;

    /*
     * Menor slot livre — o vk de um botão removido é reciclado.
     */
    for (int slot = 0; slot < 256; slot++)
    {
        const int vk = kVkBase + slot;

        if (!FindLocked(vk))
        {
            FloatingKey k;
            k.vk = vk;
            strncpy(k.title, title, sizeof(k.title) - 1);
            g_Keys.push_back(k);

            return vk;
        }
    }

    return 0;
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
    return k ? k->title : "None";
}

const char* VkTitle(int vk)
{
    std::lock_guard<std::mutex> lock(g_Mutex);

    FloatingKey* k = FindLocked(vk);
    return k ? k->title : nullptr;
}

void Bind(int vk, bool* flag)
{
    if (vk < kVkBase)
        return;

    std::lock_guard<std::mutex> lock(g_Mutex);

    FloatingKey* k = FindLocked(vk);
    if (k)
        k->bound = flag;
}

void SyncFromPanel()
{
    std::lock_guard<std::mutex> lock(g_Mutex);
    SyncFromPanelLocked();
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
        k->dragging = false;

        if (!g_Globals.General.FloatingKeysHold)
        {
            k->state = !k->state;      // modo toque: alterna no press
            k->pendingToggle = true;   // será desfeito se virar arrasto

            /*
             * SINCRONIZAÇÃO BOTÃO -> PAINEL: o toggle da função acompanha
             * na hora (o checkbox no painel acende/apaga junto).
             */
            if (k->bound)
                *k->bound = k->state;
        }
    }
    else
    {
        k->finger = false;
        k->pendingToggle = false;
        k->dragging = false;
    }
}

void DragCancel(int vk)
{
    std::lock_guard<std::mutex> lock(g_Mutex);

    FloatingKey* k = FindLocked(vk);
    if (!k)
        return;

    k->finger = false;

    if (k->pendingToggle)
    {
        k->state = !k->state;   // desfaz o toggle do DOWN
        k->pendingToggle = false;

        /*
         * Desfaz também no painel — o bool volta ao valor anterior.
         */
        if (k->bound)
            *k->bound = k->state;
    }

    k->dragging = true;
}

void MoveBy(int vk, float dx, float dy)
{
    std::lock_guard<std::mutex> lock(g_Mutex);

    FloatingKey* k = FindLocked(vk);
    if (!k)
        return;

    k->ox += dx;
    k->oy += dy;

    /*
     * O clamp real (manter dentro da tela) acontece no LayoutLocked a cada
     * frame, porque depende do tamanho atual da tela.
     */
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

    /*
     * SINCRONIZAÇÃO PAINEL -> BOTÃO (1x por frame): se o usuário mudou o
     * toggle no painel, o botão acende/apaga junto.
     */
    SyncFromPanelLocked();

    LayoutLocked(screenW, screenH);

    ImDrawList* dl = ImGui::GetForegroundDrawList();
    if (!dl)
        return;

    for (auto& k : g_Keys)
    {
        const bool active = g_Globals.General.FloatingKeysHold
            ? k.finger
            : k.state;

        // O dedo vem da janelinha Java do botão (não do io do painel)
        const bool pressed = k.finger || k.dragging;

        ImVec2 mn(k.x, k.y);
        ImVec2 mx(k.x + k.w, k.y + k.h);

        // Sombra + corpo
        dl->AddRectFilled(mn + ImVec2(0, 3), mx + ImVec2(0, 3), IM_COL32(0, 0, 0, 70), 14.0f);
        dl->AddRectFilled(mn, mx,
            active ? IM_COL32(46, 10, 10, 235)
                   : (pressed ? IM_COL32(30, 30, 30, 220) : IM_COL32(16, 16, 16, 200)),
            14.0f);

        // Borda: acesa em vermelho quando ativo
        dl->AddRect(mn, mx,
            active ? IM_COL32(200, 0, 0, 255)
                   : IM_COL32(90, 90, 95, 180),
            14.0f, 0, pressed ? 2.5f : 1.5f);

        // Bolinha de estado (maior)
        ImVec2 dot(k.x + 16.0f, k.y + k.h * 0.5f);
        dl->AddCircleFilled(dot, 5.0f,
            active ? IM_COL32(200, 0, 0, 255) : IM_COL32(70, 70, 70, 255), 20);
        if (active)
            dl->AddCircleFilled(dot, 9.0f, IM_COL32(200, 0, 0, 40), 20);

        // Texto (título da função)
        ImGui::PushFont(Fonts::InterMedium);
        char line[40];
        snprintf(line, sizeof(line), "%s", k.title);
        ImVec2 ts = ImGui::CalcTextSize(line);
        dl->AddText(ImVec2(k.x + 28.0f, k.y + (k.h - ts.y) * 0.5f),
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
