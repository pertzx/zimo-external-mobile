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
     * CADENCIA DO LOOP (a ponte e um socket: cada read/write custa um
     * roundtrip — o volume decide a latencia, nao o brute force).
     *
     * Enquanto o player local esta ATIRANDO (Player::IsPrepareAttack):
     *   - direcao reescrita a cada 2ms (500/s)
     *   - posicao da cabeca/origem relida a cada 8ms
     * Fora do disparo (modo morno, mantem o ray "quente" pro 1o tiro):
     *   - direcao reescrita a cada 20ms (50/s)
     *   - posicao/origem relidas a cada 20ms
     * A matriz de view (FovCheck) e recopiada a cada 33ms e o estado de
     * disparo e rechecado a cada 48ms. Troca de alvo e detectada em TODA
     * iteracao (leitura atomica, custo zero).
     */
    constexpr LONGLONG WRITE_PACE_FIRING_MS = 2;
    constexpr LONGLONG WRITE_PACE_IDLE_MS = 20;
    constexpr LONGLONG POS_REFRESH_FIRING_MS = 8;
    constexpr LONGLONG POS_REFRESH_IDLE_MS = 20;
    constexpr LONGLONG MATRIX_REFRESH_MS = 33;
    constexpr LONGLONG FIRE_CHECK_MS = 48;

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