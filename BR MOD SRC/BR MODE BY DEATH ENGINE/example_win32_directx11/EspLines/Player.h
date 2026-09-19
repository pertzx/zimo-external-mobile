#pragma once
#include "Math/Vector/Vector3.hpp"
#include <string>
#include <cstdint>

#ifndef BOOL3_H
#define BOOL3_H

class Player {
public:
    enum class Bool3 { True, False, Unknown };
    bool IsBot;
    bool IsKnown;
    bool IsDead;
    bool IsKnocked;
    bool IsVisible;
    
    float Distance;
    short Health;
    short WeaponID;
    short Pose;
    Bool3 IsTeam;
    uint32_t Address;
    std::string Name;

    // ===== BONES COMPLETOS PARA ESP SKELETON =====
    Vector3 Head;            // Cabeça
    Vector3 Neck;            // Pescoço
    Vector3 RightShoulder;   // Ombro direito
    Vector3 LeftShoulder;    // Ombro esquerdo
    Vector3 RightElbow;      // Cotovelo direito
    Vector3 LeftElbow;       // Cotovelo esquerdo
    Vector3 RightWrist;      // Pulso direito
    Vector3 LeftWrist;       // Pulso esquerdo
    Vector3 Hip;             // Quadril
    Vector3 RightAnkle;      // Tornozelo direito
    Vector3 LeftAnkle;       // Tornozelo esquerdo
    Vector3 Root;            // Raiz do corpo

    // ===== CACHE DE POSIÇÃO ANTERIOR =====
    Vector3 LastHead;      // Última posição da cabeça
    Vector3 LastHip;       // Última posição do quadril

    // ===== PREDIÇÃO DE MOVIMENTO =====
    Vector3 Velocity;      // Velocidade atual do jogador
    Vector3 LastRoot;      // Última posição conhecida para cálculo de velocidade
    float LastUpdateTime;  // Tempo da última atualização de posição
};

#endif
