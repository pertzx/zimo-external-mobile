#pragma once

#include <WindowsCompat.hpp>

#include <Math/Vectors/Vector3.hpp>
#include <Unity/Unity.hpp>

namespace Silent
{
    /*
     * JITTER do direction escrito no RayDir (HitObjectInfo). Valores
     * pequenos para o tiro cair na cabeca com desvio humano.
     */
    constexpr float SMOOTH_MIN = 0.0008f;
    constexpr float SMOOTH_MAX = 0.0022f;

    /*
     * ====================================================================
     * ARQUITETURA "REUSO DO SNAPSHOT DA ESP"
     * (fix: silent fraco + ESP bugada enquanto atira)
     * ====================================================================
     * Diagnostico das versoes anteriores: a ponte (socket do daemon) tem
     * throughput FIXO — cada read/write e um roundtrip, e TODOS os modulos
     * dividem o mesmo cano. O silent somava:
     *   - thread leitora propia (cadeia da cabeca: 7-15 roundtrips a cada
     *     6-8ms = milhares de reads/s) e
     *   - burst de writes sem freio (monopolizava o mutex da ponte).
     * Resultado: a ESP perdia update (bugada) e a posicao do alvo
     * envelhecia (silent fraco/torta) — exatamente quando o usuario atira.
     *
     * AGORA:
     *   - A cabeca do alvo NAO e lida na ponte: o writer busca direto do
     *     snapshot que a ESP ja atualizou (Data::GetPlayers, campo
     *     HeadWorld, lido pelo ReadLoop da ESP). Custo de socket: ZERO.
     *   - weaponPtr/StartPosition/IsFiring passam a ser lidos AQUI com
     *     throttle largo (250ms / 16ms / 100ms) — dezenas de ops/s.
     *   - Escrita em ritmo CONSTANTE de 3ms atirando (~330/s): "ultimo a
     *     escrever ganha a bala" nao exige burst de milhares — exige
     *     densidade estavel SEM estrangular a ponte que a ESP usa.
     *
     * Volume total do silent: ~400 ops/s contra ~3000+ das versoes
     * anteriores. A ESP volta a atualizar e a cabeca volta a ser fresca
     * (e a MESMA posicao que a ESP desenha) — silent constante.
     */

     /*
     * NUCLEO DA CONSTANCIA (o core da versao antiga que "ia quase
     * sempre"): ATIRANDO o write roda EM SEQUENCIA, SEM Sleep — o
     * proprio roundtrip do canal dedicado da o ritmo (milhares de
     * writes/s). Cede ao SO apenas a cada BURST_YIELD_EVERY writes
     * (sched_yield, custo ~zero). PARADO: write morno a cada
     * WRITE_PACE_IDLE_MS (quente pro 1o tiro).
     * A ESP NAO sente essa densidade: ela esta na OUTRA conexao
     * (socket propria dela) com o daemon.
     */
    constexpr uint32_t BURST_YIELD_EVERY = 128;  // sched_yield a cada 128 writes
    constexpr LONGLONG WRITE_PACE_IDLE_MS = 5;   // parado (quente pro 1o tiro)

    /* Refresh da cabeca (snapshot da ESP) POR TEMPO em cima de cache —
     * nunca por write: o burst nao pode martelar o mutex do snapshot
     * milhares de vezes por segundo (disputaria com ReadLoop/Draw). */
    constexpr LONGLONG HEAD_REFRESH_FIRING_MS = 8;   // atirando
    constexpr LONGLONG HEAD_REFRESH_IDLE_MS   = 33;  // parado

    /* Leituras propias do writer, throttled (dezenas de ops/s, nao milhares). */
    constexpr LONGLONG WEAPON_REFRESH_MS  = 250;  // m_LastAimingInfoFromWeapon
    constexpr LONGLONG ORIGIN_REFRESH_MS  = 16;   // HitObjectInfo::StartPosition
    constexpr LONGLONG FIRE_FALLBACK_MS   = 100;  // IsFiring caso a UI nao publique

    /* Copia LOCAL da view matrix pro FovCheck do write quente (zero socket). */
    constexpr LONGLONG MATRIX_REFRESH_MS = 33;

    constexpr LONGLONG HEAD_STALE_MAX_MS = 600;

    /* Ponte falhando no write: backoff antes de tentar de novo. */
    constexpr LONGLONG WRITE_FAIL_BACKOFF_MS = 25;

    /* ====================================================================
     * PREDICAO LEVE (fix "silent atira um pouquinho atras do alvo em
     * movimento"): a cabeca vem do snapshot da ESP — quando o inimigo
     * corre, ela envelhece (LastSeenTick) e o tiro cai ATRAS dele.
     *
     * O writer observa a VELOCIDADE real do alvo entre duas observacoes
     * novas do snapshot (LastSeenTick mudou) e, na hora do write, mira em
     * cabeca + velocidade * (idade_da_observacao + latencia). Custo: ZERO
     * roundtrip — tudo e matematica em cima do dado que ja chegou.
     * ==================================================================== */
    constexpr float    PRED_EMA_ALPHA     = 0.35f;  // suavizacao da velocidade (0..1)
    constexpr float    PRED_MAX_SPEED_MPS = 12.0f;  // acima disso = teleporte/lixo, zera
    constexpr LONGLONG PRED_LATENCY_MS    = 25;     // compensa roundtrip do write + consumo
    constexpr LONGLONG PRED_MAX_AGE_MS    = 200;    // nunca extrapola observacao mais velha que isso

    extern volatile LONG g_Running;

    void Start();
    void Stop();
    void UpdateViewMatrix(const Matrix4x4& matrix);

    void SetTarget(
        uintptr_t localPlayer,
        uintptr_t targetEntity
    );

    /*
     * Publicado pelo Draw a cada frame (leitura propria dele, sem custo
     * novo de ponte): 2a fonte do estado de tiro pro writer, caso o
     * IsFiring lido pelo silent falhe no pico do combate. Frescor
     * exigido pelo consumidor: 250ms.
     */
    void NotifyFiring(bool firing);

    void ClearTarget();
}
