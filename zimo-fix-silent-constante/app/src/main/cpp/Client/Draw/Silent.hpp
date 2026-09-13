#pragma once

#include <WindowsCompat.hpp>

#include <Math/Vectors/Vector3.hpp>
#include <Unity/Unity.hpp>

namespace Silent
{
    /*
     * JITTER do direction escrito no RayDir (HitObjectInfo). Valores pequenos
     * para o tiro cair na cabeca com desvio humano.
     */
    constexpr float SMOOTH_MIN = 0.0015f;
    constexpr float SMOOTH_MAX = 0.0035f;

    /*
     * ====================================================================
     * POR QUE O SILENT FICAVA "DE VEZ EM QUANDO" (1 tiro a cada ~10s)?
     * ====================================================================
     * O jogo REESCREVE o RayDir dele mesmo a todo frame e so CONSUME o
     * valor no instante do disparo. Quem for o ULTIMO a escrever antes
     * desse instante ganha o tiro. No laco ritmado antigo o write saia
     * misturado com leituras (IsFiring + cadeia da cabeca com 7-15 reads
     * + origem) e dormia 1-20ms entre cada passada — no combate a ponte
     * satura (ESP + aimbot + silent no mesmo socket), cada iteracao
     * inchava pra dezenas de ms e a densidade de write despencava pra
     * algumas dezenas por segundo. O jogo ganhava a corrida quase sempre:
     * o silent so "ia" quando dava a sorte.
     *
     * FIX: ATIRANDO o laco vira BURST DE ESCRITA sem pausa (o mesmo
     * principio do antigo flood que funcionava, so que continuo, controla-
     * do e sem travar o resto):
     *   - write, write, write... SEM Sleep, usando posicao em CACHE
     *   - toda checagem (IsFiring, cabeca, origem, weaponPtr, matrix)
     *     roda POR TEMPO e amortizada — nunca no caminho do write
     *   - sched_yield a cada YIELD_EVERY_WRITES pra ESP respirar
     * FORA do disparo: 1 write a cada WRITE_PACE_IDLE_MS so pra manter o
     * ray quente pro primeiro tiro sair silent.
     */
    constexpr LONGLONG WRITE_PACE_IDLE_MS    = 20;   // write "quente" fora do tiro
    constexpr LONGLONG FIRE_CHECK_FIRING_MS  = 8;    // pool do IsFiring atirando
    constexpr LONGLONG FIRE_CHECK_IDLE_MS    = 20;   // pool do IsFiring parado
    constexpr LONGLONG POS_REFRESH_FIRING_MS = 16;   // cabeca+origem atirando
    constexpr LONGLONG POS_REFRESH_IDLE_MS   = 40;   // cabeca+origem parado
    constexpr LONGLONG MATRIX_REFRESH_MS     = 33;   // view matrix (FovCheck)
    constexpr LONGLONG WEAPON_REFRESH_MS     = 100;  // re-ler m_LastAimingInfoFromWeapon
    constexpr LONGLONG POS_STALE_MAX_MS      = 250;  // escreve com posicao de ate 250ms
    constexpr uint32_t YIELD_EVERY_WRITES    = 32;   // sched_yield no burst

    extern volatile LONG g_Running;

    void Start();
    void Stop();
    void UpdateViewMatrix(const Matrix4x4& matrix);

    void SetTarget(
        uintptr_t localPlayer,
        uintptr_t targetEntity
    );

    void ClearTarget();
}
