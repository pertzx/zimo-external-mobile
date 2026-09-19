#include "Data.hpp"
#include <string>
#include <algorithm> // Para std::sort
#include <src/Globals.hpp>
#include <EspLines/Memory/Memory.hpp>
#include <EspLines/Offsets.hpp>
#include <EspLines/Math/TMatrix.hpp>
#include <map>
#include <EspLines/Math/Vector/Vector2.hpp>
#include <EspLines/BrutalSilent/BrutalSilent.hpp>
#include <EspLines/Math/WordToScreen.hpp>
#include <EspLines/Math/AimB.hpp>
#include <EspLines\Aimbot\Aimbot.hpp>
#include <EspLines/AimLock/AimLock.hpp>
#include <EspLines/Exploits/Weapons/NoReload.hpp>
#include <EspLines/Exploits/Weapons/NoRecoil.hpp>
#include <EspLines/Exploits/Weapons/VisionHack.hpp>
#include <EspLines/Exploits/Weapons/Sniper.hpp>
#include <vector>
#include <algorithm>
#define NOMINMAX
#include <Windows.h>
#undef min
#include <Psapi.h>

namespace FWork {
    void Data::Work() {
        BrutalSilentFunction::BrutalSilent::Start();
        GameContext ctx;
        ctx.currentGame = GetCurrentGame();
        if (!ctx.currentGame) {
            g_Globals.EspConfig.LastMatchId = 0;
            if (!g_Globals.EspConfig.Entities.empty()) {
                g_Globals.EspConfig.Entities.clear();
            }
            Mem.Cache.clear();
            return;
        }

        ctx.currentMatch = GetCurrentMatch(ctx.currentGame);
        if (!ctx.currentMatch) {
            g_Globals.EspConfig.LastMatchId = 0;
            if (!g_Globals.EspConfig.Entities.empty()) {
                g_Globals.EspConfig.Entities.clear();
            }
            Mem.Cache.clear();
            return;
        }

        // DETECCAO DE MUDANCA DE PARTIDA: Limpa entidades antigas
        uint32_t currentMatchId = (uint32_t)ctx.currentMatch;
        if (g_Globals.EspConfig.LastMatchId != currentMatchId) {
            g_Globals.EspConfig.LastMatchId = currentMatchId;
            if (!g_Globals.EspConfig.Entities.empty()) {
                g_Globals.EspConfig.Entities.clear();
            }
        }

        ctx.localPlayer = Mem.Read<uint32_t>(ctx.currentMatch + Offsets::LocalPlayer);
        if (!ctx.localPlayer || !SetupLocalPlayerAndCamera(ctx.currentMatch)) {
            if (!g_Globals.EspConfig.Entities.empty()) {
                g_Globals.EspConfig.Entities.clear();
            }
            Mem.Cache.clear();
            return;
        }

        ProcessEntities(ctx);

        // Os exploits foram removidos deste fluxo; o Spin Bot permanece
        // inicializado pelo loop principal em main.cpp.
        Aim::Aimbot::LegitAimbot();
        Aim::Aimbot::AimBotVisibleSafe();
        NoReloadFunction::NoReload::Run();
        NoRecoilFunction::NoRecoil::Run();
        VisionHackFunction::VisionHack::Run();
        SniperFunction::Sniper::Aimbot();
    }

    bool IsInsideFOV(const ImVec2& pos) {
        int centerX = g_Globals.EspConfig.Width / 2;
        int centerY = g_Globals.EspConfig.Height / 2;
        int radius = g_Globals.AimBot.Fov;

        int dx = (int)pos.x - centerX;
        int dy = (int)pos.y - centerY;

        return (dx * dx + dy * dy) <= (radius * radius);
    }

    uint32_t Data::GetCurrentGame() {
        uint32_t baseGameFacade = Mem.Read<uint32_t>(Offsets::Il2Cpp + Offsets::InitBase);
        if (!baseGameFacade) return 0;
        uint32_t gameFacade = Mem.Read<uint32_t>(baseGameFacade);
        if (!gameFacade) return 0;
        uint32_t staticGameFacade = Mem.Read<uint32_t>(gameFacade + Offsets::StaticClass);
        if (!staticGameFacade) return 0;
        return Mem.Read<uint32_t>(staticGameFacade);
    }

    uint32_t Data::GetCurrentMatch(uint32_t currentGame) {
        if (!currentGame) return 0;
        uint32_t currentMatch = Mem.Read<uint32_t>(currentGame + Offsets::CurrentMatch);
        if (!currentMatch) return 0;
        uint32_t matchStatus = Mem.Read<uint32_t>(currentMatch + Offsets::MatchStatus);
        return (matchStatus == 1) ? currentMatch : 0;
    }

    bool Data::SetupLocalPlayerAndCamera(uint32_t currentMatch) {
        if (!currentMatch) return false;
        uint32_t localPlayer = Mem.Read<uint32_t>(currentMatch + Offsets::LocalPlayer);
        if (!localPlayer) return false;
        g_Globals.EspConfig.LocalPlayer = localPlayer;

        uint32_t mainTransform = Mem.Read<uint32_t>(localPlayer + Offsets::MainCameraTransform);
        if (!mainTransform) return false;
        TransformUtils::GetPosition(mainTransform, g_Globals.EspConfig.MainCamera);

        uint32_t followCamera = Mem.Read<uint32_t>(localPlayer + Offsets::FollowCamera);
        if (!followCamera) return false;
        uint32_t camera = Mem.Read<uint32_t>(followCamera + Offsets::Camera);
        if (!camera) return false;
        uint32_t cameraBase = Mem.Read<uint32_t>(camera + 0x8);
        if (!cameraBase) return false;

        g_Globals.EspConfig.ViewMatrix = Mem.Read<Matrix4x4>(cameraBase + Offsets::ViewMatrix);
        g_Globals.EspConfig.Matrix = true;
        return true;
    }

    void Data::ProcessEntities(const GameContext& ctx) {
        uint32_t entityDictionary = Mem.Read<uint32_t>(ctx.currentGame + Offsets::DictionaryEntities);
        if (!entityDictionary) return;

        // ========== APENAS PLAYERS REAIS (offset 0x10) ==========
        uint32_t entries = Mem.Read<uint32_t>(entityDictionary + 0xC);
        if (!entries) return;

        uint32_t entities = entries + 0x10;
        uint32_t entitiesCount = Mem.Read<uint32_t>(entityDictionary + 0x10);
        g_Globals.EspConfig.previousCount = entitiesCount;

        if (entitiesCount < 1 || entitiesCount > 10000) return;

        Vector3 mainPos = g_Globals.EspConfig.MainCamera;
        std::vector<uint32_t> entitiesToRemove;
        float currentTime = (float)GetTickCount64() / 1000.0f;

        for (uint32_t i = 0; i < entitiesCount; ++i) {
            uint32_t entry = entities + (i * 0x10);

            int hash = 0;
            if (!Mem.Read(entry + 0x0, hash) || hash < 0)
                continue;

            uint32_t entity = 0;
            if (!Mem.Read(entry + 0xC, entity) || entity == 0 || entity == ctx.localPlayer)
                continue;

            try {
                // VALIDACAO 1: Verificar se avatarData existe
                uint32_t avatarManager = Mem.Read<uint32_t>(entity + Offsets::AvatarManager);
                uint32_t avatar = avatarManager ? Mem.Read<uint32_t>(avatarManager + Offsets::Avatar) : 0;
                uint32_t avatarData = avatar ? Mem.Read<uint32_t>(avatar + Offsets::Avatar_Data) : 0;
                if (!avatarData) {
                    continue;
                }

                Player& player = g_Globals.EspConfig.Entities[entity];
                player.Address = entity;

                // Bots fazem parte da mesma coleção de entidades do ESP.
                // O campo precisava ser preenchido antes dos módulos de mira;
                // caso contrário, ficava indefinido e os bots não eram tratados
                // de forma consistente entre os módulos.
                player.IsBot = Mem.Read<bool>(entity + Offsets::IsClientBot);
                // Nome é opcional para entidades válidas; isso também cobre bots
                // que não possuem uma string de nome disponível.
                player.IsKnown = true;

                // VALIDACAO 2: Verificar se e visível
                player.IsVisible = Mem.Read<bool>(avatar + Offsets::Avatar_IsVisible);
                if (!player.IsVisible) {
                    entitiesToRemove.push_back(entity);
                    continue;
                }

                // VALIDACAO 3: Verificar se e time
                bool isTeam = Mem.Read<bool>(avatarData + Offsets::Avatar_Data_IsTeam);
                player.IsTeam = isTeam ? Player::Bool3::True : Player::Bool3::False;
                if (player.IsTeam == Player::Bool3::True) {
                    entitiesToRemove.push_back(entity);
                    continue;
                }

                // VALIDACAO 4: Verificar se esta morto
                player.IsDead = Mem.Read<bool>(entity + Offsets::Player_IsDead);
                if (player.IsDead) {
                    entitiesToRemove.push_back(entity);
                    continue;
                }

                // VALIDACAO 5: Verificar se esta knocked
                uint32_t shadowBase = Mem.Read<uint32_t>(entity + Offsets::Player_ShadowBase);
                player.IsKnocked = shadowBase ? (Mem.Read<int>(shadowBase + Offsets::XPose) == 8) : false;

                // VALIDACAO 6: Ler vida com validacao rigorosa
                uint32_t dataPool = Mem.Read<uint32_t>(entity + Offsets::Player_Data);
                if (!dataPool) {
                    entitiesToRemove.push_back(entity);
                    continue;
                }

                uint32_t poolObj = Mem.Read<uint32_t>(dataPool + 0x8);
                if (!poolObj) {
                    entitiesToRemove.push_back(entity);
                    continue;
                }

                uint32_t pool = Mem.Read<uint32_t>(poolObj + 0x10);
                if (!pool) {
                    entitiesToRemove.push_back(entity);
                    continue;
                }

                player.Health = Mem.Read<short>(pool + Offsets::Vida);
                if (player.Health <= 0 || player.Health > 200) {
                    entitiesToRemove.push_back(entity);
                    continue;
                }

                // Le weapon ID
                uint32_t weaponptr = Mem.Read<uint32_t>(poolObj + 0x20);
                player.WeaponID = weaponptr ? Mem.Read<short>(weaponptr + Offsets::WeaponOnHand) : (short)0;

                // VALIDACAO 7: Ler nome
                uint32_t nameAddr = Mem.Read<uint32_t>(entity + Offsets::Player_Name);
                if (nameAddr) {
                    int nameLen = Mem.Read<int>(nameAddr + 0x8);
                    if (nameLen > 0 && nameLen < 128) {
                        player.Name = Mem.String(nameAddr + 0xC, nameLen * 2, true);
                        player.IsKnown = true;
                    }
                }

                // VALIDACAO 8: Ler bones com validacao rigorosa
                uint32_t boneAddr = 0;
                bool validHead = false, validHip = false;

                if (Mem.Read(entity + Offsets::Bones::Head, boneAddr) && boneAddr) {
                    TransformUtils::GetNodePosition(boneAddr, player.Head);
                    validHead = (player.Head != Vector3::Zero() && 
                                std::isfinite(player.Head.X) && 
                                std::isfinite(player.Head.Y) && 
                                std::isfinite(player.Head.Z) &&
                                player.Head.Y > -500.0f &&
                                player.Head.Y < 500.0f);
                }

                if (Mem.Read(entity + Offsets::Bones::Hip, boneAddr) && boneAddr) {
                    TransformUtils::GetNodePosition(boneAddr, player.Hip);
                    validHip = (player.Hip != Vector3::Zero() && 
                               std::isfinite(player.Hip.X) && 
                               std::isfinite(player.Hip.Y) && 
                               std::isfinite(player.Hip.Z) &&
                               player.Hip.Y > -500.0f &&
                               player.Hip.Y < 500.0f);
                }

                if (Mem.Read(entity + Offsets::Bones::Root, boneAddr) && boneAddr) {
                    TransformUtils::GetNodePosition(boneAddr, player.Root);
                }

                // ===== LER TODOS OS OUTROS OSSOS PARA ESP SKELETON =====
                if (Mem.Read(entity + Offsets::Bones::Neck, boneAddr) && boneAddr) {
                    TransformUtils::GetNodePosition(boneAddr, player.Neck);
                }
                if (Mem.Read(entity + Offsets::Bones::LeftShoulder, boneAddr) && boneAddr) {
                    TransformUtils::GetNodePosition(boneAddr, player.LeftShoulder);
                }
                if (Mem.Read(entity + Offsets::Bones::RightShoulder, boneAddr) && boneAddr) {
                    TransformUtils::GetNodePosition(boneAddr, player.RightShoulder);
                }
                if (Mem.Read(entity + Offsets::Bones::LeftElbow, boneAddr) && boneAddr) {
                    TransformUtils::GetNodePosition(boneAddr, player.LeftElbow);
                }
                if (Mem.Read(entity + Offsets::Bones::RightElbow, boneAddr) && boneAddr) {
                    TransformUtils::GetNodePosition(boneAddr, player.RightElbow);
                }
                if (Mem.Read(entity + Offsets::Bones::LeftWrist, boneAddr) && boneAddr) {
                    TransformUtils::GetNodePosition(boneAddr, player.LeftWrist);
                }
                if (Mem.Read(entity + Offsets::Bones::RightWrist, boneAddr) && boneAddr) {
                    TransformUtils::GetNodePosition(boneAddr, player.RightWrist);
                }
                if (Mem.Read(entity + Offsets::Bones::LeftAnkle, boneAddr) && boneAddr) {
                    TransformUtils::GetNodePosition(boneAddr, player.LeftAnkle);
                }
                if (Mem.Read(entity + Offsets::Bones::RightAnkle, boneAddr) && boneAddr) {
                    TransformUtils::GetNodePosition(boneAddr, player.RightAnkle);
                }

                // VALIDACAO 9: Precisa de pelo menos Head ou Hip validos
                if (!validHead && !validHip) {
                    entitiesToRemove.push_back(entity);
                    continue;
                }

                // VALIDACAO 10: Verificar se Head/Hip estao dentro de range razoavel
                if (std::fabs(player.Head.X) > 10000.0f || 
                    std::fabs(player.Head.Y) > 10000.0f || 
                    std::fabs(player.Head.Z) > 10000.0f) {
                    entitiesToRemove.push_back(entity);
                    continue;
                }

                // VALIDACAO 11: Calcular distancia com validacao
                player.Distance = Vector3::Distance(mainPos, validHead ? player.Head : player.Hip);

                // VALIDACAO 12: Filtro anti-fantasma (distance razoavel)
                if (player.Distance < 0.5f || player.Distance > 500.0f) {
                    entitiesToRemove.push_back(entity);
                    continue;
                }

                // VALIDACAO 13: Verificar diferenca entre Head e Hip (devem estar proximos)
                if (validHead && validHip) {
                    float headHipDist = Vector3::Distance(player.Head, player.Hip);
                    float maxDist = player.IsKnocked ? 6.0f : 3.0f;
                    if (headHipDist < 0.1f || headHipDist > maxDist) {
                        entitiesToRemove.push_back(entity);
                        continue;
                    }
                }

                // CACHE E TIMESTAMP
                player.LastHead = player.Head;
                player.LastHip = player.Hip;
                player.LastUpdateTime = currentTime;
            }
            catch (...) {
                entitiesToRemove.push_back(entity);
            }
        }

        // APAGAR ENTITIES INVALIDAS
        for (uint32_t entityToRemove : entitiesToRemove) {
            g_Globals.EspConfig.Entities.erase(entityToRemove);
        }

        // LIMPEZA AGRESSIVA: Remove entidades que sumiram (timeout 0.05s)
        for (auto it = g_Globals.EspConfig.Entities.begin(); it != g_Globals.EspConfig.Entities.end(); ) {
            if (currentTime - it->second.LastUpdateTime > 0.05f) {
                it = g_Globals.EspConfig.Entities.erase(it);
            }
            else {
                ++it;
            }
        }
    }

    void Data::Reset() { 
        g_Globals.EspConfig.Entities.clear(); 
    }
}
