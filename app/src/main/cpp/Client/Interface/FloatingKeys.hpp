#pragma once

/*
 * ============================================================================
 * FloatingKeys.hpp
 * ============================================================================
 *
 * BOTÕES FLUTUANTES de keybind para touch.
 *
 * No desktop o keybind espera uma tecla física (GetAsyncKeyState) — no
 * Android isso nunca dispara (o stub do WindowsCompat.hpp sempre devolve 0)
 * e não existe teclado no meio do jogo. Solução: cada keybind do painel
 * pode criar um BOTÃO FLUTUANTE na tela, desenhado por cima do jogo via
 * ImGui (ForegroundDrawList), com o texto da função ("Aim", "Silent",
 * "Ghost"...).
 *
 * ══════════════════════════════════════════════════════════════════════
 * UM BOTÃO FIXO POR FUNÇÃO (nada de F1/F2/F3):
 *
 *   - Acquire("AimKey") reaproveita o botão já existente da mesma
 *     função (mesmo título) — spawnar/despawnar repetidamente NÃO
 *     acumula botões nem incrementa numeração.
 *   - O box do keybind no painel mostra "Show" / "Hide" (em vez de
 *     "None"/"F#").
 *
 * SINCRONIZAÇÃO BIDIRECIONAL COM O PAINEL (Bind + SyncFromPanel):
 *
 *   - Tocar no botão flutuante alterna o bool da função (o checkbox
 *     do painel muda na hora — dá pra ver que a função está ativa).
 *   - Tocar no checkbox do painel atualiza o estado do botão no
 *     próximo frame (o botão acende/apaga junto).
 *   - No modo "segurar" (FloatingKeysHold) o botão continua
 *     momentâneo, sem toggle.
 *
 * Modos (Globals.General.FloatingKeysHold):
 *   false = TOQUE SIMPLES : um toque liga, outro toque desliga (toggle)
 *   true  = SEGURAR       : ligado enquanto o dedo estiver pressionado
 *
 * O DESENHO é 100% ImGui (libclient). O TOQUE chega pelo Java: o
 * OverlayService cria uma pequena janela overlay transparente em cima de
 * cada botão e repassa ACTION_DOWN/UP para nativeFloatingKeyTouch()
 * (Client/main.cpp), que chama FloatingKeys::OnTouch() aqui.
 *
 * ARRASTAR: se o dedo deslizar mais que o touch-slop, o Java chama
 * nativeFloatingKeyDrag() (cancela o toggle/hold daquele toque) e depois
 * nativeFloatingKeyMove() com o delta do dedo — o botão fica onde foi
 * solto (offset guardado por botão).
 *
 * IsDown(vk) é o que o gameplay consulta — vk é o mesmo int do KeyBind
 * antigo (faixa 0x7000+), então o resto do código nem precisa saber se
 * o "key" veio de teclado ou de botão de tela.
 * ============================================================================
 */

#include <string>
#include <vector>

namespace FloatingKeys
{
    /*
     * Faixa dos vks virtuais dos botões flutuantes. Um KeyBind cujo valor
     * cai nessa faixa é um botão de tela (não uma tecla física).
     */
    constexpr int kVkBase = 0x7000;

    /*
     * Cria (ou REAPROVEITA — um botão fixo por função) um botão flutuante
     * com o label dado e devolve o vk dele.
     *   - Se já existe botão com o mesmo título: devolve o vk dele (nada
     *     de duplicar nem de numeração acumulando).
     *   - Se o pool estiver cheio (kMaxKeys): devolve 0.
     * Chamado pelo widget Custom::KeyBind no Android quando o usuário
     * toca no "Show" para spawnar o botão da função.
     */
    int Acquire(const char* label);

    /*
     * Remove o botão (o widget volta a mostrar "Show"). vk fora da faixa
     * ou inexistente é ignorado.
     */
    void Release(int vk);

    /*
     * Nome do botão — o título da função ("Aim", "Silent"...).
     * vk fora da faixa devolve "None". (Legado: antes mostrava "F1".)
     */
    const char* VkLabel(int vk);

    /*
     * Label humano registrado no Acquire ("Aim", "Silent"...).
     * Devolve nullptr se o vk não existir.
     */
    const char* VkTitle(int vk);

    /*
     * SINCRONIZAÇÃO COM O PAINEL: liga o botão (vk) ao bool do toggle
     * da função (o mesmo bool do checkbox no painel). Depois disso:
     *   - tocar no botão flutuante alterna o bool (checkbox acompanha)
     *   - tocar no checkbox atualiza o botão (via SyncFromPanel)
     * Chame toda frame que o KeyBind for desenhado, ou logo após o
     * Acquire. vk inválido/inexistente é ignorado.
     */
    void Bind(int vk, bool* flag);

    /*
     * Puxa o estado dos bools ligados via Bind() para o estado visual
     * dos botões (direção painel -> botão). Chamado automaticamente
     * 1x por frame dentro do DrawTick. No modo "segurar" é no-op.
     */
    void SyncFromPanel();

    /*
     * Estado EFETIVO do botão, conforme o modo:
     *   modo toque    -> alterna no toque, persiste até o próximo toque
     *                    (e fica espelhado no bool da função via Bind)
     *   modo segurar  -> true somente enquanto o dedo está na tela
     * É o que AndroidInput::IsKeyPressed() consulta para vk >= 0x7000.
     */
    bool IsDown(int vk);

    /*
     * true se vk pertence a um botão flutuante existente.
     */
    bool Exists(int vk);

    /*
     * Evento de toque vindo do JNI (janela Java do botão).
     * down = true no ACTION_DOWN, false no ACTION_UP/CANCEL.
     */
    void OnTouch(int vk, bool down);

    /*
     * O toque VIROU ARRASTO (dedo deslizou além do slop). Cancela o efeito
     * do toque: solta o dedo lógico e desfaz o toggle caso o modo seja
     * toque-simples (o DOWN tinha ligado/desligado o botão) — inclusive
     * no bool da função.
     */
    void DragCancel(int vk);

    /*
     * Movimento do arrasto: desloca a posição do botão pelo delta do dedo
     * (em pixels da surface). O Layout mantém o botão dentro da tela.
     */
    void MoveBy(int vk, float dx, float dy);

    /*
     * Desenha todos os botões na ImGui::GetForegroundDrawList() e
     * guarda, internamente, a lista de retângulos para o Java posicionar
     * as janelas de toque:
     *     [vk0, x0, y0, w0, h0, vk1, x1, y1, w1, h1, ...]
     * Coordenadas em pixels da SURFACE (mesma origem do painel).
     *
     * Chamado 1x por frame pelo PanelApp::Run() — sempre, com o menu
     * aberto ou fechado (os botões servem pra usar DURANTE o jogo).
     */
    void DrawTick(int screenW, int screenH, int* outBounds, int boundsMax, int& boundsCount);

    /*
     * Quantidade de botões ativos.
     */
    int Count();

    /*
     * Rects do ÚLTIMO DrawTick (coordenadas da surface), no formato
     * [vk, x, y, w, h, ...] — é o que o JNI (nativeGetFloatingKeys)
     * devolve pro Java posicionar as janelas de toque.
     */
    constexpr int kMaxKeys = 6;
    void GetBounds(int* outArr, int maxInts, int& outCount);

    /*
     * Limpa tudo (restart/shutdown do painel).
     */
    void Clear();
}
