
#include <cinttypes>
#include <android/log.h>

#include "Offsets.hpp"
#include <Memory/Memory.hpp>
#include <Globals.hpp>
#include <Shared/WindowsCompat.hpp>
#include <Unity/UTF/UTF8.hpp>

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "StormOffsets", __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "StormOffsets", __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, "StormOffsets", __VA_ARGS__)

#define OFFSET_LOGI(...) \
    __android_log_print(ANDROID_LOG_INFO, "StormOffsets", __VA_ARGS__)

#define OFFSET_LOGE(...) \
    __android_log_print(ANDROID_LOG_ERROR, "StormOffsets", __VA_ARGS__)

// ==================== Offsets ====================

uintptr_t Offsets::LibIl2Cpp = 0;
std::vector<uintptr_t> Offsets::LibIl2CppCandidates;
uintptr_t Offsets::AccessClass = 0;

bool Offsets::IsMatchActive(Offsets::MatchState s)
{
    int v = static_cast<int>(s);
    return v >= 1 && v <= 3;
}

// ==================== Game Flow ====================

// GameConfig
//uintptr_t Offsets::GameConfig::ReleaseVersion = 0;

// GameVarDef
uintptr_t Offsets::GameVarDef::GameVarDef_TypeInfo = 0;
uintptr_t Offsets::GameVarDef::ShootTraceAdjustmentDistanceThreshold = 0;
uintptr_t Offsets::GameVarDef::EnableAccelerationOnFalling = 0;
uintptr_t Offsets::GameVarDef::EnableLowFallingSwapWeapon = 0;
uintptr_t Offsets::GameVarDef::RotationSensitivityMin = 0;
uintptr_t Offsets::GameVarDef::RotationSensitivityMax = 0;
uintptr_t Offsets::GameVarDef::AimRotationSensitivityMin = 0;
uintptr_t Offsets::GameVarDef::AimRotationSensitivityMax = 0;

// GameFacade
uintptr_t Offsets::GameFacade::GameFacade_TypeInfo = 0;
uintptr_t Offsets::GameFacade::CurrentMatchGame = 0;

// (V9) BaseGame + TimeService
uintptr_t Offsets::BaseGame::m_UIScene = 0;
uintptr_t Offsets::BaseGame::m_GameTimer = 0;
uintptr_t Offsets::TimeService::m_FixedDeltaTime = 0;

// (V9) Teleport Mark UI chain
uintptr_t Offsets::UIInGameScene::m_BigMapCtrl = 0;
uintptr_t Offsets::UIBigMapController::m_MapContentCtrl = 0;
uintptr_t Offsets::UIMapContentController::m_LocalMapMarkController = 0;
uintptr_t Offsets::UIHudPlayerMarkController::m_pos = 0;

// (V9) FollowCamera (Vision Hack)
uintptr_t Offsets::FollowCamera::FOVOffset = 0;

// MatchGame
uintptr_t Offsets::MatchGame::m_Match = 0;
uintptr_t Offsets::MatchGame::m_CameraControllerManager = 0;

// Match
uintptr_t Offsets::Match::m_State = 0;
uintptr_t Offsets::Match::m_LocalPlayer = 0;
uintptr_t Offsets::Match::m_LocalObserver = 0;
uintptr_t Offsets::Match::m_AttackableEntities = 0;

// FollowCamera

// ==================== Camera ====================

uintptr_t Offsets::CameraControllerManager::m_Camera = 0;
uintptr_t Offsets::Camera::m_CachedPtr = 0;
uintptr_t Offsets::Camera::ViewMatrix = 0;

// ==================== Observer  ====================

uintptr_t Offsets::Observer::m_TargetPlayer = 0;

// ==================== Player ====================

// General
uintptr_t Offsets::Player::IsClientBot = 0;
uintptr_t Offsets::Player::IsFemale = 0;
uintptr_t Offsets::Player::IsFiring = 0;
uintptr_t Offsets::Player::UGCStartFiring = 0;
uintptr_t Offsets::Player::IsPrepareAttack = 0;
uintptr_t Offsets::Player::m_IsCurFrameFowardLockToAimRot = 0;
uintptr_t Offsets::Player::m_WaitForForceSync = 0;
uintptr_t Offsets::Player::m_TransformType = 0;

// Aim
uintptr_t Offsets::Player::m_AimRotation = 0;
uintptr_t Offsets::Player::m_AuxAimRotation = 0;
uintptr_t Offsets::Player::m_AimAssist = 0;
uintptr_t Offsets::Player::m_EAimAssit = 0;
uintptr_t Offsets::Player::m_AimAssistOnSighting = 0;
uintptr_t Offsets::Player::m_LastAimingInfoFromWeapon = 0;

// Transform / Camera
uintptr_t Offsets::Player::MainCameraTransform = 0;
uintptr_t Offsets::Player::m_SwapWeaponTime = 0;
uintptr_t Offsets::Player::m_FollowCamera = 0;

// Managers
uintptr_t Offsets::Player::m_Attributes = 0;
uintptr_t Offsets::Player::m_AvatarManager = 0;
uintptr_t Offsets::Player::m_InventoryManager = 0;

// UserControlHandler
uintptr_t Offsets::Player::m_UserControl = 0;

// Colliders
uintptr_t Offsets::Player::m_HeadCollider = 0;
uintptr_t Offsets::Player::m_fireColliders = 0;
//uintptr_t Offsets::Player::LockedAimingCollider = 0;

// Bone Nodes (ITransformNode)
uintptr_t Offsets::Player::HeadNode = 0;
uintptr_t Offsets::Player::m_HipNode = 0;
uintptr_t Offsets::Player::m_BloodEffectNode = 0;
uintptr_t Offsets::Player::m_RootNode = 0;
uintptr_t Offsets::Player::m_BoneRootNode = 0;
uintptr_t Offsets::Player::m_WeaponMountNode = 0;
uintptr_t Offsets::Player::m_LeftWeaponNode = 0;
uintptr_t Offsets::Player::m_FlightNode = 0;
uintptr_t Offsets::Player::m_RightArmNode = 0;
uintptr_t Offsets::Player::m_LeftArmNode = 0;
uintptr_t Offsets::Player::m_RightForeArmNode = 0;
uintptr_t Offsets::Player::m_LeftForeArmNode = 0;
uintptr_t Offsets::Player::m_RightHandNode = 0;
uintptr_t Offsets::Player::m_LeftHandNode = 0;
uintptr_t Offsets::Player::m_RightAnkleNode = 0;
uintptr_t Offsets::Player::m_LeftAnkleNode = 0;
uintptr_t Offsets::Player::m_RightToeNode = 0;
uintptr_t Offsets::Player::m_LeftToeNode = 0;

// ==================== PlayerNetwork ====================

uintptr_t Offsets::PlayerNetwork::m_ShadowState = 0;
uintptr_t Offsets::PlayerNetwork::m_Profile = 0;

// ==================== PlayerAttributes ====================

uintptr_t Offsets::PlayerAttributes::m_EatSpeedScale = 0;
uintptr_t Offsets::PlayerAttributes::m_FireIntervalScale = 0;
// (V9) BR MOD ports
uintptr_t Offsets::PlayerAttributes::ReloadNoConsumeAmmoclip = 0;
uintptr_t Offsets::PlayerAttributes::ShootNoReload = 0;
uintptr_t Offsets::PlayerAttributes::RunSpeedUpScale = 0;
uintptr_t Offsets::PlayerAttributes::FallingSpeedUpScale = 0;
//uintptr_t Offsets::PlayerAttributes::DamageAdditionScale = 0;
//uintptr_t Offsets::PlayerAttributes::ExecuteDamageScale = 0;
//uintptr_t Offsets::PlayerAttributes::BuffWeaponDamageScale = 0;
//uintptr_t Offsets::PlayerAttributes::ShowEnermyTargetOnMap = 0;
//uintptr_t Offsets::PlayerAttributes::ShowEnermyTargetOnHud = 0;
//uintptr_t Offsets::PlayerAttributes::ShowEnemyFootStep = 0;
//uintptr_t Offsets::PlayerAttributes::EnemyFootStepMaxDistanceDelta = 0;
//uintptr_t Offsets::PlayerAttributes::EnemyFootStepMinDistanceDelta = 0;
//uintptr_t Offsets::PlayerAttributes::EnemyFootStepLimitNumDelta = 0;

// ==================== AimAssistAutoLock ====================

uintptr_t Offsets::AimAssistAutoLock::m_TargetHeuristic = 0;
uintptr_t Offsets::AimAssistAutoLock::m_Entity = 0;

// ==================== UserControlHandler ====================

uintptr_t Offsets::UserControlHandler::m_AxisData = 0;
uintptr_t Offsets::UserControlHandler::m_FingerInDashArea = 0;
uintptr_t Offsets::UserControlHandler::m_IsTouched = 0;
uintptr_t Offsets::UserControlHandler::m_LockFingerInDashArea = 0;
uintptr_t Offsets::UserControlHandler::m_DashByMovingJoystick = 0;

// ==================== AimAssistOnSighting ====================

uintptr_t Offsets::AimAssistOnSighting::m_fAimAssistCurrentLerpTime = 0;

// ==================== HitObjectInfo ====================

uintptr_t Offsets::HitObjectInfo::RayDir = 0;
uintptr_t Offsets::HitObjectInfo::StartPosition = 0;

// ==================== InventoryManager ====================

uintptr_t Offsets::InventoryManager::m_itemOnHand = 0;

// ==================== Avatar ====================

uintptr_t Offsets::AvatarManager::m_Avatar = 0;
uintptr_t Offsets::UMAAvatarBase::umaData = 0;
uintptr_t Offsets::UmaAvatarSimple::IsVisible = 0;

// UMAData
uintptr_t Offsets::UMAData::skeleton = 0;
uintptr_t Offsets::UMAData::isLocalPlayer = 0;
uintptr_t Offsets::UMAData::isTeammate = 0;
uintptr_t Offsets::UMAData::isMeshDirty = 0;
uintptr_t Offsets::UMAData::isTextureDirty = 0;

// (V9) Skin Changer — wardrobe singleton + UMA recipes
uintptr_t Offsets::UmaAvatarSimple::m_Recipes = 0;
uintptr_t Offsets::UmaAvatarSimple::m_VisibleSlots = 0;
uintptr_t Offsets::UmaAvatarSimple::m_ChangedSlots = 0;
uintptr_t Offsets::UmaAvatarSimple::lastBuildNotFinish = 0;
uintptr_t Offsets::UmaAvatarSimple::m_CustomTextureDirty = 0;
uintptr_t Offsets::AvatarWardrobeDataManager::AvatarWardrobeDataManager_TypeInfo = 0;
uintptr_t Offsets::AvatarWardrobeDataManager::m_dictIdToWardrobeDataTree = 0;
uintptr_t Offsets::IntervalTreeDic::m_Data = 0;
uintptr_t Offsets::AvatarWardrobeData::VisualBase = 0;
uintptr_t Offsets::AvatarWardrobeData::iID = 0;
uintptr_t Offsets::AvatarWardrobeData::wardrobeType = 0;

// UMASkeleton
uintptr_t Offsets::UMASkeleton::boneHashDataLookup = 0;
uintptr_t Offsets::UMASkeleton::boneNameHash = 0;
uintptr_t Offsets::UMASkeleton::boneTransform = 0;

// ==================== Replication ====================

uintptr_t Offsets::ReplicationEntity::m_PRIDataPool = 0;
uintptr_t Offsets::ReplicationEntity::m_Datas = 0;
uintptr_t Offsets::ReplicationEntity::HealthCurrentPtr = 0;
uintptr_t Offsets::ReplicationEntity::HealthMaxPtr = 0;
uintptr_t Offsets::ReplicationEntity::WeaponPtr = 0;
uintptr_t Offsets::ReplicationEntity::EpPtr = 0;
uintptr_t Offsets::ReplicationEntity::Value = 0;

// ==================== Shadow ====================

uintptr_t Offsets::ShadowState::TargetPhysXPose = 0;

// ==================== Profile ====================

uintptr_t Offsets::BaseProfileInfo::AccountID = 0;
uintptr_t Offsets::BaseProfileInfo::Level = 0;
uintptr_t Offsets::BaseProfileInfo::NickName = 0;

// ==================== Weapon ====================

// Weapon
uintptr_t Offsets::Weapon::FireComponent = 0;
uintptr_t Offsets::Weapon::m_WeaponData = 0;
uintptr_t Offsets::Weapon::m_WeaponParams = 0;
uintptr_t Offsets::Weapon::m_FireDuration = 0;
uintptr_t Offsets::Weapon::m_IsSighting = 0;
uintptr_t Offsets::Weapon::tangentTheta = 0;
uintptr_t Offsets::Weapon::IntWeaponType = 0;

// WeaponParams
uintptr_t Offsets::WeaponParams::FullDamageDistance = 0;
uintptr_t Offsets::WeaponParams::PrefireDelay = 0;
uintptr_t Offsets::WeaponParams::Range = 0;

// ==================== Transform / Position ====================

uintptr_t Offsets::PlayerTransformNode::Transform = 0;
uintptr_t Offsets::PlayerTransformNode::m_CachedTransform = 0;

uintptr_t Offsets::GetPosWorld::transObj = 0;
uintptr_t Offsets::GetPosWorld::matrix = 0;
uintptr_t Offsets::GetPosWorld::index = 0;
uintptr_t Offsets::GetPosWorld::matrix_list = 0;
uintptr_t Offsets::GetPosWorld::matrix_indices = 0;

uintptr_t Offsets::GetPosWorld::HeadColliderMale = 0;
uintptr_t Offsets::GetPosWorld::HeadColliderFemale = 0;
uintptr_t Offsets::GetPosWorld::ColliderTransform = 0;
uintptr_t Offsets::GetPosWorld::BoundsCenter_1 = 0;
uintptr_t Offsets::GetPosWorld::BoundsCenter_2 = 0;
uintptr_t Offsets::GetPosWorld::BoundsCenter_3 = 0;

// ==================== IL2CPP Containers ====================

// UnityList
template <bool N32, typename TValue>
uintptr_t Offsets::UnityList<N32, TValue>::GetItems()
{
        uintptr_t ArrayBase = N32 ? g_FreeFireMemory.Read<uint32_t>((uintptr_t)this + 0x8) + 0x10 : g_FreeFireMemory.Read<uint64_t>((uintptr_t)this + 0x10) + 0x20;
        return ArrayBase;
}

template <bool N32, typename TValue>
int Offsets::UnityList<N32, TValue>::GetSize()
{
        int Size = N32 ? g_FreeFireMemory.Read<int>((uintptr_t)this + 0xC) : g_FreeFireMemory.Read<int>((uintptr_t)this + 0x18);
        return (Size >= 0 && Size <= 500) ? Size : 0;
}

template <bool N32, typename TValue>
TValue Offsets::UnityList<N32, TValue>::GetItem(int Index)
{
        return g_FreeFireMemory.Read<TValue>(GetItems() + (N32 ? 0x4 : 0x8) * Index);
}

// Explicit instantiations
template class Offsets::UnityList<true, uint32_t>;
template class Offsets::UnityList<true, uint64_t>;
template class Offsets::UnityList<false, uint32_t>;
template class Offsets::UnityList<false, uint64_t>;

template <bool N32, bool V31, typename TValue>
uintptr_t Offsets::UnityDictionary<N32, V31, TValue>::GetValues()
{
        if constexpr (V31)
        {
                return N32 ? g_FreeFireMemory.Read<uint32_t>((uintptr_t)this + 0xC) + 0x10 : g_FreeFireMemory.Read<uint64_t>((uintptr_t)this + 0x18) + 0x20;
        }
        else
        {
                return N32 ? g_FreeFireMemory.Read<uint32_t>((uintptr_t)this + 0x14) + 0x10 : g_FreeFireMemory.Read<uint64_t>((uintptr_t)this + 0x28) + 0x20;
        }
}

template <bool N32, bool V31, typename TValue>
int Offsets::UnityDictionary<N32, V31, TValue>::GetNumValues()
{
        int Count;
        if constexpr (V31)
        {
                Count = N32 ? g_FreeFireMemory.Read<int>((uintptr_t)this + 0x10) : g_FreeFireMemory.Read<int>((uintptr_t)this + 0x20);
        }
        else
        {
                Count = N32 ? g_FreeFireMemory.Read<int>((uintptr_t)this + 0x18) : g_FreeFireMemory.Read<int>((uintptr_t)this + 0x30);
        }
        return (Count >= 1 && Count <= 500) ? Count : 0;
}

template <bool N32, bool V31, typename TValue>
TValue Offsets::UnityDictionary<N32, V31, TValue>::GetValue(int Index)
{
        if constexpr (V31)
        {
                return g_FreeFireMemory.Read<TValue>(GetValues() + (N32 ? 0x10 * Index + 0xC : 0x18 * Index + 0x10));
        }
        else
        {
                return g_FreeFireMemory.Read<TValue>(GetValues() + (N32 ? 0x4 : 0x8) * Index);
        }
}

// Explicit instantiations
template class Offsets::UnityDictionary<true, false, uint32_t>;
template class Offsets::UnityDictionary<true, false, uint64_t>;
template class Offsets::UnityDictionary<false, false, uint32_t>;
template class Offsets::UnityDictionary<false, false, uint64_t>;
template class Offsets::UnityDictionary<true, true, uint32_t>;
template class Offsets::UnityDictionary<true, true, uint64_t>;
template class Offsets::UnityDictionary<false, true, uint32_t>;
template class Offsets::UnityDictionary<false, true, uint64_t>;

/*
 * Offsets::Loaded()
 * true quando o perfil aplicado tem os offsets-chave != 0.
 * APENAS diagnostico — NAO bloqueia mais nada. O ReadLoop soba direto.
 */
bool Offsets::Loaded()
{
    return GameFacade::GameFacade_TypeInfo          != 0 &&
           GameFacade::CurrentMatchGame             != 0 &&
           MatchGame::m_Match                       != 0 &&
           MatchGame::m_CameraControllerManager     != 0 &&
           Match::m_State                           != 0 &&
           Match::m_LocalPlayer                     != 0 &&
           Match::m_AttackableEntities              != 0 &&
           Camera::ViewMatrix                       != 0;
}

/*
 * Dump (logcat) dos offsets-chave apos aplicar o perfil escolhido.
 *
 * ATENCAO: funcao SOLTA (fora da classe Offsets). Todo acesso a membros
 * precisa do prefixo Offsets:: — sem isso o clang emite "use of
 * undeclared identifier".
 */
static void LogLoadedOffsets(const char* perfil)
{
    LOGI("--------- PERFIL CARREGADO: %s ---------", perfil);
    LOGI("  AccessClass                  = 0x%lX", (unsigned long)Offsets::AccessClass);
    LOGI("  GameFacade.TypeInfo          = 0x%lX", (unsigned long)Offsets::GameFacade::GameFacade_TypeInfo);
    LOGI("  GameFacade.CurrentMatchGame  = 0x%lX", (unsigned long)Offsets::GameFacade::CurrentMatchGame);
    LOGI("  MatchGame.m_Match            = 0x%lX", (unsigned long)Offsets::MatchGame::m_Match);
    LOGI("  MatchGame.m_CameraController = 0x%lX", (unsigned long)Offsets::MatchGame::m_CameraControllerManager);
    LOGI("  Match.m_State                = 0x%lX", (unsigned long)Offsets::Match::m_State);
    LOGI("  Match.m_LocalPlayer          = 0x%lX", (unsigned long)Offsets::Match::m_LocalPlayer);
    LOGI("  Match.m_LocalObserver        = 0x%lX", (unsigned long)Offsets::Match::m_LocalObserver);
    LOGI("  Match.m_AttackableEntities   = 0x%lX", (unsigned long)Offsets::Match::m_AttackableEntities);
    LOGI("  Camera.m_CachedPtr           = 0x%lX", (unsigned long)Offsets::Camera::m_CachedPtr);
    LOGI("  Camera.ViewMatrix            = 0x%lX", (unsigned long)Offsets::Camera::ViewMatrix);
    LOGI("  Player.m_AvatarManager       = 0x%lX", (unsigned long)Offsets::Player::m_AvatarManager);
    LOGI("  Player.MainCameraTransform   = 0x%lX", (unsigned long)Offsets::Player::MainCameraTransform);
    LOGI("------------------------------------------------");
}

/*
 * Lista QUAIS offsets-chave ficaram em 0 (ex: FFTHV8A() ainda vazio).
 * So AVISA — nao impede nada de rodar.
 */
static void LogOffsetsZerados()
{
    LOGW("  ---- OFFSETS-CHAVE ZERADOS (incompleto, mas seguindo) ----");
    if (Offsets::GameFacade::GameFacade_TypeInfo == 0)          LOGW("    GameFacade.TypeInfo              = 0");
    if (Offsets::GameFacade::CurrentMatchGame == 0)             LOGW("    GameFacade.CurrentMatchGame     = 0");
    if (Offsets::MatchGame::m_Match == 0)                       LOGW("    MatchGame.m_Match               = 0");
    if (Offsets::MatchGame::m_CameraControllerManager == 0)     LOGW("    MatchGame.m_CameraControllerMgr = 0");
    if (Offsets::Match::m_State == 0)                           LOGW("    Match.m_State                   = 0");
    if (Offsets::Match::m_LocalPlayer == 0)                     LOGW("    Match.m_LocalPlayer             = 0");
    if (Offsets::Match::m_AttackableEntities == 0)              LOGW("    Match.m_AttackableEntities      = 0");
    if (Offsets::Camera::ViewMatrix == 0)                       LOGW("    Camera.ViewMatrix               = 0");
    LOGW("  => Preencha esses valores dentro do perfil selecionado");
    LOGW("     (FFTHV7A75 / FFTHV7A76 / FFTHV8A) em Offsets.cpp");
}

/*
 * Zera TODOS os offsets antes de aplicar um perfil. Garante que trocar
 * "FF v7a" <-> "FF v8a" na Settings nao deixe valor velho de um perfil
 * vazando dentro do outro.
 */
void Offsets::ZerarOffsets()
{
    AccessClass = 0;

    // GameVarDef
    GameVarDef::GameVarDef_TypeInfo = 0;
    GameVarDef::ShootTraceAdjustmentDistanceThreshold = 0;
    GameVarDef::EnableAccelerationOnFalling = 0;
    GameVarDef::EnableLowFallingSwapWeapon = 0;
    GameVarDef::RotationSensitivityMin = 0;
    GameVarDef::RotationSensitivityMax = 0;
    GameVarDef::AimRotationSensitivityMin = 0;
    GameVarDef::AimRotationSensitivityMax = 0;

    // GameFacade
    GameFacade::GameFacade_TypeInfo = 0;
    GameFacade::CurrentMatchGame = 0;

    // (V9) BaseGame + TimeService + Teleport Mark + FollowCamera
    BaseGame::m_UIScene = 0;
    BaseGame::m_GameTimer = 0;
    TimeService::m_FixedDeltaTime = 0;
    UIInGameScene::m_BigMapCtrl = 0;
    UIBigMapController::m_MapContentCtrl = 0;
    UIMapContentController::m_LocalMapMarkController = 0;
    UIHudPlayerMarkController::m_pos = 0;
    FollowCamera::FOVOffset = 0;

    // MatchGame
    MatchGame::m_Match = 0;
    MatchGame::m_CameraControllerManager = 0;

    // Match
    Match::m_State = 0;
    Match::m_LocalPlayer = 0;
    Match::m_LocalObserver = 0;
    Match::m_AttackableEntities = 0;

    // Camera
    CameraControllerManager::m_Camera = 0;
    Camera::m_CachedPtr = 0;
    Camera::ViewMatrix = 0;

    // Observer
    Observer::m_TargetPlayer = 0;

    // Player
    Player::IsClientBot = 0;
    Player::IsFemale = 0;
        Player::IsFiring = 0;
        Player::UGCStartFiring = 0;
    Player::IsPrepareAttack = 0;
    Player::m_IsCurFrameFowardLockToAimRot = 0;
    Player::m_WaitForForceSync = 0;
    Player::m_TransformType = 0;
    Player::m_AimRotation = 0;
    Player::m_AuxAimRotation = 0;
    Player::m_AimAssist = 0;
    Player::m_EAimAssit = 0;
    Player::m_AimAssistOnSighting = 0;
    Player::m_LastAimingInfoFromWeapon = 0;
    Player::MainCameraTransform = 0;
    Player::m_SwapWeaponTime = 0;
    Player::m_FollowCamera = 0;
    Player::m_Attributes = 0;
    Player::m_AvatarManager = 0;
    Player::m_InventoryManager = 0;
    Player::m_UserControl = 0;
    Player::m_HeadCollider = 0;
    Player::m_fireColliders = 0;
    Player::HeadNode = 0;
    Player::m_HipNode = 0;
    Player::m_BloodEffectNode = 0;
    Player::m_RootNode = 0;
    Player::m_BoneRootNode = 0;
    Player::m_WeaponMountNode = 0;
    Player::m_LeftWeaponNode = 0;
    Player::m_FlightNode = 0;
    Player::m_RightArmNode = 0;
    Player::m_LeftArmNode = 0;
    Player::m_RightForeArmNode = 0;
    Player::m_LeftForeArmNode = 0;
    Player::m_RightHandNode = 0;
    Player::m_LeftHandNode = 0;
    Player::m_RightAnkleNode = 0;
    Player::m_LeftAnkleNode = 0;
    Player::m_RightToeNode = 0;
    Player::m_LeftToeNode = 0;

    // PlayerNetwork
    PlayerNetwork::m_ShadowState = 0;
    PlayerNetwork::m_Profile = 0;

    // PlayerAttributes
    PlayerAttributes::m_EatSpeedScale = 0;
    PlayerAttributes::m_FireIntervalScale = 0;
    PlayerAttributes::ReloadNoConsumeAmmoclip = 0;
    PlayerAttributes::ShootNoReload = 0;
    PlayerAttributes::RunSpeedUpScale = 0;
    PlayerAttributes::FallingSpeedUpScale = 0;

    // AimAssistAutoLock
    AimAssistAutoLock::m_TargetHeuristic = 0;
    AimAssistAutoLock::m_Entity = 0;

    // UserControlHandler
    UserControlHandler::m_AxisData = 0;
    UserControlHandler::m_FingerInDashArea = 0;
    UserControlHandler::m_IsTouched = 0;
    UserControlHandler::m_LockFingerInDashArea = 0;
    UserControlHandler::m_DashByMovingJoystick = 0;

    // AimAssistOnSighting
    AimAssistOnSighting::m_fAimAssistCurrentLerpTime = 0;

    // HitObjectInfo
    HitObjectInfo::RayDir = 0;
    HitObjectInfo::StartPosition = 0;

    // InventoryManager
    InventoryManager::m_itemOnHand = 0;

    // Avatar
    AvatarManager::m_Avatar = 0;
    UMAAvatarBase::umaData = 0;
    UmaAvatarSimple::IsVisible = 0;
    UmaAvatarSimple::m_Recipes = 0;
    UmaAvatarSimple::m_VisibleSlots = 0;
    UmaAvatarSimple::m_ChangedSlots = 0;
    UmaAvatarSimple::lastBuildNotFinish = 0;
    UmaAvatarSimple::m_CustomTextureDirty = 0;

    // UMAData
    UMAData::skeleton = 0;
    UMAData::isLocalPlayer = 0;
    UMAData::isTeammate = 0;
    UMAData::isMeshDirty = 0;
    UMAData::isTextureDirty = 0;

    // (V9) Skin Changer wardrobe
    AvatarWardrobeDataManager::AvatarWardrobeDataManager_TypeInfo = 0;
    AvatarWardrobeDataManager::m_dictIdToWardrobeDataTree = 0;
    IntervalTreeDic::m_Data = 0;
    AvatarWardrobeData::VisualBase = 0;
    AvatarWardrobeData::iID = 0;
    AvatarWardrobeData::wardrobeType = 0;

    // UMASkeleton
    UMASkeleton::boneHashDataLookup = 0;
    UMASkeleton::boneNameHash = 0;
    UMASkeleton::boneTransform = 0;

    // Replication
    ReplicationEntity::m_PRIDataPool = 0;
    ReplicationEntity::m_Datas = 0;
    ReplicationEntity::HealthCurrentPtr = 0;
    ReplicationEntity::HealthMaxPtr = 0;
    ReplicationEntity::WeaponPtr = 0;
    ReplicationEntity::EpPtr = 0;
    ReplicationEntity::Value = 0;

    // Shadow
    ShadowState::TargetPhysXPose = 0;

    // Profile
    BaseProfileInfo::AccountID = 0;
    BaseProfileInfo::Level = 0;
    BaseProfileInfo::NickName = 0;

    // Weapon
    Weapon::FireComponent = 0;
    Weapon::m_WeaponData = 0;
    Weapon::m_WeaponParams = 0;
    Weapon::m_FireDuration = 0;
    Weapon::m_IsSighting = 0;
    Weapon::tangentTheta = 0;
    Weapon::IntWeaponType = 0;

    // WeaponParams
    WeaponParams::FullDamageDistance = 0;
    WeaponParams::PrefireDelay = 0;
    WeaponParams::Range = 0;

    // PlayerTransformNode
    PlayerTransformNode::Transform = 0;
    PlayerTransformNode::m_CachedTransform = 0;

    // GetPosWorld
    GetPosWorld::transObj = 0;
    GetPosWorld::matrix = 0;
    GetPosWorld::index = 0;
    GetPosWorld::matrix_list = 0;
    GetPosWorld::matrix_indices = 0;
    GetPosWorld::HeadColliderMale = 0;
    GetPosWorld::HeadColliderFemale = 0;
    GetPosWorld::ColliderTransform = 0;
    GetPosWorld::BoundsCenter_1 = 0;
    GetPosWorld::BoundsCenter_2 = 0;
    GetPosWorld::BoundsCenter_3 = 0;
}

/*
 * GameConfig() — aplica DIRETO o perfil escolhido na Settings.
 *
 * SEM probe, SEM validacao de versao, SEM candidatos, SEM deteccao.
 * A UNICA coisa definida automaticamente e a base da libil2cpp.so,
 * que o Memory::Initialize() ja achou com FindModuleBase() e deixou
 * aqui em Offsets::LibIl2Cpp.
 *
 * Tudo e ditado por g_Globals.General.GameProfile:
 *   0 = FF v7a b75 -> FFTHV7A75()   (32-bit, ponteiros de 4 bytes)
 *   1 = FF v7a b76 -> FFTHV7A76()   (32-bit, ponteiros de 4 bytes)
 *   2 = FF v8a     -> FFTHV8A()     (64-bit, ponteiros de 8 bytes)
 *
 * O tamanho da leitura de ponteiros (4/8 bytes) NAO e escolhido aqui:
 * o ReadLoop le com o template N32 (v7a -> Read<uint32_t>, v8a ->
 * Read<uint64_t>) e o N32 e derivado do GameProfile no Initialize.
 */
void Offsets::GameConfig()
{
    const int profile = g_Globals.General.GameProfile;

    LOGI("================================================");
    LOGI("GameConfig() — aplicando perfil escolhido na Settings");
    LOGI("  GameProfile = %d (0=v7a b75 | 1=v7a b76 | 2=v8a)", profile);
    LOGI("  LibIl2Cpp   = 0x%lX (definida pelo Memory::Initialize)",
         (unsigned long)LibIl2Cpp);

    ZerarOffsets();

    switch (profile)
    {
    case 2:
        LOGW("  Perfil: FF v8a (64-bit, ponteiros de 8 bytes)");
        FFTHV8A();
        LogLoadedOffsets("FF v8a");
        break;

    case 1:
        LOGW("  Perfil: FF v7a build 76 (32-bit, ponteiros de 4 bytes)");
        FFTHV7A76();
        LogLoadedOffsets("FF v7a b76");
        break;

    case 0:
    default:
        LOGW("  Perfil: FF v7a build 75 (32-bit, ponteiros de 4 bytes)");
        FFTHV7A75();
        LogLoadedOffsets("FF v7a b75");
        break;
    }

    /*
     * Aviso rapido se o perfil escolhido ainda tem offsets-chave em 0
     * (ex: FFTHV8A() vazio). NAO bloqueia nada — o ReadThread sobe do
     * mesmo jeito e o log da cadeia mostra onde parar.
     */
    if (!Loaded())
        LogOffsetsZerados();

    /*
     * (V9.2) AUTO-TI REMOVIDO: os RVAs de TypeInfo sao hardcoded no
     * perfil (o usuario mantem). O il2cpp novo (v8a) inicializa esses
     * slots de forma LAZY — o slot guarda um token encoded ate o jogo
     * usar a classe, e isso e estado NORMAL, nao slot velho. Scan/
     * resolver nao apressa o jogo e so gerava ruido ("SEM RESPOSTA").
     * O daemon segue sendo apenas ponte de read/write.
     */

    LOGI("================================================");
}

/*
 * (V9) AUTO-RESOLVE DE TYPEINFO.
 *
 * O slot global de um TypeInfo (GameFacade_TypeInfo etc.) fica na
 * .data da libil2cpp.so e MUDA DE RVA a cada atualizacao do jogo —
 * os valores manuais do Offsets.cpp ficam velhos e a cadeia morre no
 * AccessClass. Aqui o daemon varre os mapeamentos legiveis da lib
 * procurando os slots pelo NOME da classe (campo name do Il2CppClass)
 * e devolve os RVAs reais. Cada classe so e varrida UMA vez por pid
 * (cache no proprio daemon).
 */
bool Offsets::AutoResolveTypeInfos()
{
    struct TypeInfoJob
    {
        const char* className;
        uintptr_t* slot;
        const char* label;
    };

    TypeInfoJob jobs[] =
    {
        { "GameFacade",                &GameFacade::GameFacade_TypeInfo,                          "GameFacade_TypeInfo" },
        { "GameVarDef",                &GameVarDef::GameVarDef_TypeInfo,                          "GameVarDef_TypeInfo" },
        { "AvatarWardrobeDataManager", &AvatarWardrobeDataManager::AvatarWardrobeDataManager_TypeInfo, "Wardrobe_TypeInfo" },
    };

    static LONGLONG s_LastRunMs = 0;
    static int s_LastPid = -1;
    static bool s_LastResult = false;

    LONGLONG now = (LONGLONG)GetTickCount64();

    /*
     * Re-run permitido: troca de pid (restart do jogo) ou primeira vez.
     * Em caso de falha, deixa retry a cada 30 s (ponte pode subir depois).
     */
    if (Memory::GetTargetPid() == s_LastPid &&
        s_LastPid > 0 &&
        s_LastResult &&
        now - s_LastRunMs < 60000)
    {
        return s_LastResult;
    }

    if (!s_LastResult &&
        s_LastPid > 0 &&
        Memory::GetTargetPid() == s_LastPid &&
        now - s_LastRunMs < 30000)
    {
        return s_LastResult;
    }

    s_LastRunMs = now;
    s_LastPid = Memory::GetTargetPid();

    if (LibIl2Cpp == 0 || s_LastPid <= 0)
    {
        LOGW("[AUTO-TI] skip: sem pid/base da libil2cpp (ponte conectada? %d)",
             Memory::IsBridgeConnected() ? 1 : 0);
        s_LastResult = false;
        return false;
    }

    bool allOk = true;
    int changed = 0;

    for (const TypeInfoJob& j : jobs)
    {
        std::vector<Memory::TypeInfoHit> hits;

        if (!Memory::FindTypeInfoRvas(j.className, hits))
        {
            LOGW("[AUTO-TI] '%s': scan nao encontrou slots — mantendo manual 0x%lX",
                 j.className, (unsigned long)*j.slot);
            allOk = false;
            continue;
        }

        /*
         * Valida os hits lendo o slot de verdade: *(lib + rva) tem que
         * ser um ponteiro de klass plausivel (nao-nulo, fora da lib).
         */
        uintptr_t chosen = 0;

        for (const Memory::TypeInfoHit& h : hits)
        {
            if (h.Rva == 0)
                continue;

            uintptr_t klass = g_Globals.General.N32
                ? (uintptr_t)g_FreeFireMemory.Read<uint32_t>(LibIl2Cpp + h.Rva)
                : (uintptr_t)g_FreeFireMemory.Read<uint64_t>(LibIl2Cpp + h.Rva);

            if (klass == 0)
                continue;

            if (klass >= LibIl2Cpp && klass < LibIl2Cpp + 0x100000000ULL)
                continue;   /* klass vive fora da lib */

            chosen = h.Rva;
            break;
        }

        if (chosen == 0)
        {
            LOGW("[AUTO-TI] '%s': %zu hits mas nenhum slot legivel agora — mantendo manual",
                 j.className, hits.size());
            allOk = false;
            continue;
        }

        if (*j.slot != chosen)
        {
            LOGW("[AUTO-TI] %s: manual 0x%lX -> REAL 0x%lX (auto-resolvido!)",
                 j.label, (unsigned long)*j.slot, (unsigned long)chosen);
            *j.slot = chosen;
            ++changed;
        }
        else
        {
            LOGI("[AUTO-TI] %s: manual 0x%lX confere com o scan (ok)",
                 j.label, (unsigned long)*j.slot);
        }
    }

    s_LastResult = allOk;

    if (changed > 0)
    {
        LOGW("[AUTO-TI] %d TypeInfo(s) corrigidos automaticamente — cadeia deve passar do AccessClass agora", changed);
    }

    return allOk;
}

/*
 * FFTHV8A() — Free Fire arm64-v8a (64-bit, ponteiros de 8 bytes).
 *
 * PREENCHA com o dump da SUA lib v8a (dumpster/Ghidra). Todos os campos
 * sao da mesma lista do perfil v7a — so mudam os valores. Enquanto
 * estiver tudo 0, o painel avisa "OFFSETS-CHAVE ZERADOS" no logcat e a
 * cadeia de leitura nao encontra nada, mas nada trava.
 *
 * Lembrete: em v8a a leitura de ponteiro ja e automatica (8 bytes via
 * Read<uint64_t>) porque o GameProfile=2 deixa N32=false.
 */
void Offsets::FFTHV8A() // 64-bit
{
        AccessClass = 0xB8; // MANUAL (mantido) — fora da dump

        // (V9) BaseGame / TimeService / Teleport Mark / FollowCamera
        BaseGame::m_UIScene = 0x10; // dump novo
        BaseGame::m_GameTimer = 0x18; // dump novo
        TimeService::m_FixedDeltaTime = 0x2c; // dump novo
        UIInGameScene::m_BigMapCtrl = 0x458; // dump novo
        UIBigMapController::m_MapContentCtrl = 0xa8; // dump novo (herdado UIMapBaseController)
        UIMapContentController::m_LocalMapMarkController = 0xf8; // dump novo
        UIHudPlayerMarkController::m_pos = 0xb0; // dump novo
        FollowCamera::FOVOffset = 0x84; // dump novo

        // GameVarDef
        GameVarDef::GameVarDef_TypeInfo = 0xac1e810; // MANUAL (mantido) — typeinfo fora da dump
        GameVarDef::ShootTraceAdjustmentDistanceThreshold = 0x7d0; // dump novo
        GameVarDef::EnableAccelerationOnFalling = 0x2cdf; // dump novo
        GameVarDef::EnableLowFallingSwapWeapon = 0x3055; // dump novo
        GameVarDef::RotationSensitivityMin = 0x1140; // dump novo
        GameVarDef::RotationSensitivityMax = 0x1144; // dump novo
        GameVarDef::AimRotationSensitivityMin = 0x1148; // dump novo
        GameVarDef::AimRotationSensitivityMax = 0x114c; // dump novo

        // GameFacade
        GameFacade::GameFacade_TypeInfo = 0xac1e768; // MANUAL (mantido) — typeinfo fora da dump
        GameFacade::CurrentMatchGame = 0x8; // dump novo

        // MatchGame
        MatchGame::m_Match = 0x90; // dump novo
        MatchGame::m_CameraControllerManager = 0xd8; // dump novo

        // Match
        Match::m_State = 0xcc; // dump novo
        Match::m_LocalPlayer = 0xd8; // dump novo
        Match::m_LocalObserver = 0x100; // dump novo
        Match::m_AttackableEntities = 0x200; // dump novo

        // Camera
        CameraControllerManager::m_Camera = 0x20; // dump novo
        Camera::m_CachedPtr = 0x10; // dump novo
        Camera::ViewMatrix = 0x128; // MANUAL (mantido) — interno da Unity (estimativa)

        // Observer
        Observer::m_TargetPlayer = 0x30; // dump novo

        // Player / PlayerNetwork
        // General
        Player::IsClientBot = 0x4a0; // dump novo
        Player::IsFemale = 0xc14; // dump novo
        Player::IsFiring = 0x848; // dump novo
        Player::UGCStartFiring = 0x259; // dump novo
        Player::IsPrepareAttack = 0x848; // dump novo = IsFiring
        Player::m_IsCurFrameFowardLockToAimRot = 0x340; // dump novo
        Player::m_WaitForForceSync = 0x80c; // dump novo
        Player::m_TransformType = 0x127c; // dump novo

        // Aim
        Player::m_AimRotation = 0x614; // dump novo
        Player::m_AuxAimRotation = 0x628; // dump novo
        Player::m_AimAssist = 0x638; // dump novo
        Player::m_EAimAssit = 0x660; // dump novo
        Player::m_AimAssistOnSighting = 0x668; // dump novo
        Player::m_LastAimingInfoFromWeapon = 0xe50; // dump novo

        // Transform / Camera
        Player::MainCameraTransform = 0x3e8; // dump novo
        Player::m_SwapWeaponTime = 0x804; // dump novo

        // Managers
        Player::m_Attributes = 0x768; // dump novo
        Player::m_AvatarManager = 0x770; // dump novo
        Player::m_InventoryManager = 0x740; // dump novo

        // UserControlHandler
        Player::m_UserControl = 0x4d0; // dump novo
        Player::m_FollowCamera = 0x690; // dump novo (V9 — Vision Hack)

        // Colliders
        Player::m_HeadCollider = 0x738; // dump novo
        Player::m_fireColliders = 0xb58; // dump novo

        // Bone Nodes
        Player::HeadNode = 0x6a0; // dump novo
        Player::m_HipNode = 0x6a8; // dump novo
        Player::m_BloodEffectNode = 0x6b0; // dump novo
        Player::m_RootNode = 0x6c8; // dump novo
        Player::m_BoneRootNode = 0x6d0; // dump novo
        Player::m_WeaponMountNode = 0x698; // dump novo
        Player::m_LeftWeaponNode = 0x6f8; // dump novo
        Player::m_FlightNode = 0x6c0; // dump novo
        Player::m_RightArmNode = 0x710; // dump novo
        Player::m_LeftArmNode = 0x708; // dump novo
        Player::m_RightForeArmNode = 0x720; // dump novo
        Player::m_LeftForeArmNode = 0x730; // dump novo
        Player::m_RightHandNode = 0x718; // dump novo
        Player::m_LeftHandNode = 0x728; // dump novo
        Player::m_RightAnkleNode = 0x6e0; // dump novo
        Player::m_LeftAnkleNode = 0x6d8; // dump novo
        Player::m_RightToeNode = 0x6f0; // dump novo
        Player::m_LeftToeNode = 0x6e8; // dump novo

        // PlayerNetwork
        PlayerNetwork::m_ShadowState = 0x24a8; // dump novo
        PlayerNetwork::m_Profile = 0x24d0; // dump novo

        // Shadow
        ShadowState::TargetPhysXPose = 0x80; // dump novo

        // PlayerAttributes
        PlayerAttributes::m_EatSpeedScale = 0xd4; // dump novo
        PlayerAttributes::m_FireIntervalScale = 0x258; // dump novo
        PlayerAttributes::ReloadNoConsumeAmmoclip = 0x110; // dump novo (V9)
        PlayerAttributes::ShootNoReload = 0x111; // dump novo (V9)
        PlayerAttributes::RunSpeedUpScale = 0x2c0; // dump novo (V9)
        PlayerAttributes::FallingSpeedUpScale = 0x2bc; // dump novo (V9)

        // AimAssistAutoLock
        AimAssistAutoLock::m_TargetHeuristic = 0x10; // dump novo
        AimAssistAutoLock::m_Entity = 0x18; // dump novo

        // UserControlHandler
        UserControlHandler::m_AxisData = 0x68; // dump novo
        UserControlHandler::m_FingerInDashArea = 0x90; // dump novo
        UserControlHandler::m_IsTouched = 0x4b; // dump novo
        UserControlHandler::m_LockFingerInDashArea = 0x94; // dump novo
        UserControlHandler::m_DashByMovingJoystick = 0x9c; // dump novo

        // AimAssistOnSighting
        AimAssistOnSighting::m_fAimAssistCurrentLerpTime = 0x80; // dump novo

        // HitObjectInfo
        HitObjectInfo::RayDir = 0x40; // dump novo
        HitObjectInfo::StartPosition = 0x4c; // dump novo

        // InventoryManager
        InventoryManager::m_itemOnHand = 0xa0; // dump novo

        // Avatar
        AvatarManager::m_Avatar = 0x138; // dump novo
        UMAAvatarBase::umaData = 0x28; // dump novo
        UmaAvatarSimple::IsVisible = 0x101; // dump novo

        // (V9) Skin Changer — UMA
        UmaAvatarSimple::m_Recipes = 0xd0; // dump novo (Int32[])
        UmaAvatarSimple::m_VisibleSlots = 0xd8; // dump novo
        UmaAvatarSimple::m_ChangedSlots = 0xdc; // dump novo
        UmaAvatarSimple::lastBuildNotFinish = 0xe0; // dump novo (NAO ESCREVER)
        UmaAvatarSimple::m_CustomTextureDirty = 0x100; // dump novo

        // (V9) Skin Changer — wardrobe singleton
        AvatarWardrobeDataManager::AvatarWardrobeDataManager_TypeInfo = 0xac1e878; // MANUAL (auto-resolve no GameConfig)
        AvatarWardrobeDataManager::m_dictIdToWardrobeDataTree = 0x18; // dump novo
        IntervalTreeDic::m_Data = 0x18; // 64-bit: m_Tree 0x10, m_Data 0x18
        AvatarWardrobeData::VisualBase = 0x10; // 4 x Int32 (0x10..0x20)
        AvatarWardrobeData::iID = 0x30; // dump novo
        AvatarWardrobeData::wardrobeType = 0x34; // dump novo

        // UMAData
        UMAData::skeleton = 0x138; // dump novo
        UMAData::isLocalPlayer = 0x80; // dump novo
        UMAData::isTeammate = 0x81; // dump novo
        UMAData::isMeshDirty = 0x5e; // dump novo (V9)
        UMAData::isTextureDirty = 0x60; // dump novo (V9)

        // UMASkeleton
        UMASkeleton::boneHashDataLookup = 0x28; // dump novo
        UMASkeleton::boneNameHash = 0x10; // dump novo — (BoneData)
        UMASkeleton::boneTransform = 0x18; // dump novo — (BoneData)

        // Replication
        ReplicationEntity::m_PRIDataPool = 0x70; // dump novo
        ReplicationEntity::m_Datas = 0x10; // dump novo
        ReplicationEntity::HealthCurrentPtr = 0x20; // dump novo
        ReplicationEntity::HealthMaxPtr = 0x28; // dump novo
        ReplicationEntity::WeaponPtr = 0x40; // dump novo
        ReplicationEntity::EpPtr = 0x50; // dump novo
        ReplicationEntity::Value = 0x18; // MANUAL (mantido) — 64-bit

        // Profile
        BaseProfileInfo::AccountID = 0x10; // dump novo
        BaseProfileInfo::Level = 0x20; // dump novo
        BaseProfileInfo::NickName = 0x28; // dump novo

        // Weapon
        Weapon::FireComponent = 0x80; // dump novo
        Weapon::m_WeaponData = 0x98; // dump novo
        Weapon::m_WeaponParams = 0xa8; // dump novo
        Weapon::m_FireDuration = 0x644; // dump novo
        Weapon::m_IsSighting = 0x8a0; // dump novo
        Weapon::tangentTheta = 0x18; // dump novo
        Weapon::IntWeaponType = 0xc8; // dump novo

        // WeaponParams
        WeaponParams::FullDamageDistance = 0x50; // dump novo
        WeaponParams::PrefireDelay = 0x150; // dump novo
        WeaponParams::Range = 0x4c; // dump novo

        // PlayerTransformNode
        PlayerTransformNode::Transform = 0x10; // dump novo
        PlayerTransformNode::m_CachedTransform = 0x58; // dump novo

        // get_position_Injected (fixed — so mudam se a Unity mudar)
        GetPosWorld::transObj = 0x10; // fixed v8a
        GetPosWorld::matrix = 0x40; // fixed v8a
        GetPosWorld::index = 0x48; // fixed v8a
        GetPosWorld::matrix_list = 0x30; // fixed v8a
        GetPosWorld::matrix_indices = 0x38; // fixed v8a

        // GetHeadPosition (fixed — so mudam se a Unity mudar)
        GetPosWorld::HeadColliderMale = 0x70; // fixed v8a
        GetPosWorld::HeadColliderFemale = 0x78; // fixed v8a
        GetPosWorld::ColliderTransform = 0x10; // fixed v8a
        GetPosWorld::BoundsCenter_1 = 0x50; // fixed v8a
        GetPosWorld::BoundsCenter_2 = 0x28; // fixed v8a
        GetPosWorld::BoundsCenter_3 = 0xC0; // fixed v8a
}

void Offsets::FFTHV7A75() // v75 32-bit
{
        AccessClass = 0x5C; // TODO: update manually

        // (V9) BaseGame / TimeService / Teleport Mark / FollowCamera (dump ponte)
        BaseGame::m_UIScene = 0x8;
        BaseGame::m_GameTimer = 0xC;
        TimeService::m_FixedDeltaTime = 0x24;
        UIInGameScene::m_BigMapCtrl = 0x218;
        UIBigMapController::m_MapContentCtrl = 0x54;
        UIMapContentController::m_LocalMapMarkController = 0x90;
        UIHudPlayerMarkController::m_pos = 0x58;
        FollowCamera::FOVOffset = 0x48;

        // GameVarDef
        GameVarDef::GameVarDef_TypeInfo = 0xa5bc398; // TODO: update manually
        GameVarDef::ShootTraceAdjustmentDistanceThreshold = 0x674;
        GameVarDef::EnableAccelerationOnFalling = 0x27CA;
        GameVarDef::EnableLowFallingSwapWeapon = 0x2AE5;
        GameVarDef::RotationSensitivityMin = 0xF0C;
        GameVarDef::RotationSensitivityMax = 0xF10;
        GameVarDef::AimRotationSensitivityMin = 0xF14;
        GameVarDef::AimRotationSensitivityMax = 0xF18;

        // GameFacade
        GameFacade::GameFacade_TypeInfo = 0xa5bc344; // TODO: update manually
        GameFacade::CurrentMatchGame = 0x4;

        // MatchGame
        MatchGame::m_Match = 0x50;
        MatchGame::m_CameraControllerManager = 0x74;

        // Match
        Match::m_State = 0x8C;
        Match::m_LocalPlayer = 0x94;
        Match::m_LocalObserver = 0xB4;
        Match::m_AttackableEntities = 0x140;

        // Camera
        CameraControllerManager::m_Camera = 0x10;
        Camera::m_CachedPtr = 0x8;
        Camera::ViewMatrix = 0xE8; // V7A: 0xE8 | FF MAX: 0xE4

        // Observer
        Observer::m_TargetPlayer = 0x28;

        // Player / PlayerNetwork
        // General
        Player::IsClientBot = 0x2E4;
        Player::IsFemale = 0x7D8;
        Player::IsFiring = 0x540;
        Player::UGCStartFiring = 0x13d;
        Player::IsPrepareAttack = 0x540;
        Player::m_IsCurFrameFowardLockToAimRot = 0x1D4;
        Player::m_WaitForForceSync = 0x520;
        Player::m_TransformType = 0xC4C;

        // Aim
        Player::m_AimRotation = 0x400;
        Player::m_AuxAimRotation = 0x410;
        Player::m_AimAssist = 0x420;
        Player::m_EAimAssit = 0x438;
        Player::m_AimAssistOnSighting = 0x43C;
        Player::m_LastAimingInfoFromWeapon = 0x978;

        // Transform / Camera
        Player::MainCameraTransform = 0x24C;
        Player::m_SwapWeaponTime = 0x51C;

        // Managers
        Player::m_Attributes = 0x4BC;
        Player::m_AvatarManager = 0x4C0;
        Player::m_InventoryManager = 0x4A8;

        // UserControlHandler
        Player::m_UserControl = 0x304;
        Player::m_FollowCamera = 0x450; // dump ponte (V9 — Vision Hack)

        // Colliders
        Player::m_HeadCollider = 0x4A4;
        Player::m_fireColliders = 0x760;
        // Player::LockedAimingCollider = 0x54;

        // Bone Nodes
        Player::HeadNode = 0x458;
        Player::m_HipNode = 0x45C;
        Player::m_BloodEffectNode = 0x460;
        Player::m_RootNode = 0x46C;
        Player::m_BoneRootNode = 0x470;
        Player::m_WeaponMountNode = 0x454;
        Player::m_LeftWeaponNode = 0x484;
        Player::m_FlightNode = 0x468;
        Player::m_RightArmNode = 0x490;
        Player::m_LeftArmNode = 0x48C;
        Player::m_RightForeArmNode = 0x498;
        Player::m_LeftForeArmNode = 0x4A0;
        Player::m_RightHandNode = 0x494;
        Player::m_LeftHandNode = 0x49C;
        Player::m_RightAnkleNode = 0x478;
        Player::m_LeftAnkleNode = 0x474;
        Player::m_RightToeNode = 0x480;
        Player::m_LeftToeNode = 0x47C;

        // PlayerNetwork
        PlayerNetwork::m_ShadowState = 0x18B8;
        PlayerNetwork::m_Profile = 0x18CC;

        // Shadow
        ShadowState::TargetPhysXPose = 0x78;

        // PlayerAttributes
        PlayerAttributes::m_EatSpeedScale = 0x60;
        PlayerAttributes::m_FireIntervalScale = 0x18C;
        PlayerAttributes::ReloadNoConsumeAmmoclip = 0x98; // dump ponte (V9)
        PlayerAttributes::ShootNoReload = 0x99; // dump ponte (V9)
        PlayerAttributes::RunSpeedUpScale = 0x1D8; // dump ponte (V9)
        PlayerAttributes::FallingSpeedUpScale = 0x1D4; // dump ponte (V9)

        // AimAssistAutoLock
        AimAssistAutoLock::m_TargetHeuristic = 0xC;
        AimAssistAutoLock::m_Entity = 0xC;

        // UserControlHandler
        UserControlHandler::m_AxisData = 0x34;
        UserControlHandler::m_FingerInDashArea = 0x4C;
        UserControlHandler::m_IsTouched = 0x37;
        UserControlHandler::m_LockFingerInDashArea = 0x50;
        UserControlHandler::m_DashByMovingJoystick = 0x58;

        // AimAssistOnSighting
        AimAssistOnSighting::m_fAimAssistCurrentLerpTime = 0x44;

        // HitObjectInfo
        HitObjectInfo::RayDir = 0x2C;
        HitObjectInfo::StartPosition = 0x38;

        // InventoryManager
        InventoryManager::m_itemOnHand = 0x54;

        // Avatar
        AvatarManager::m_Avatar = 0xA8;
        UMAAvatarBase::umaData = 0x14;
        UmaAvatarSimple::IsVisible = 0x95;

        // (V9) Skin Changer — UMA + wardrobe (dump ponte)
        UmaAvatarSimple::m_Recipes = 0x6C;
        UmaAvatarSimple::m_VisibleSlots = 0x70;
        UmaAvatarSimple::m_ChangedSlots = 0x74;
        UmaAvatarSimple::lastBuildNotFinish = 0x78;
        UmaAvatarSimple::m_CustomTextureDirty = 0x94;
        AvatarWardrobeDataManager::AvatarWardrobeDataManager_TypeInfo = 0xa5bc3ec; // MANUAL (auto-resolve no GameConfig)
        AvatarWardrobeDataManager::m_dictIdToWardrobeDataTree = 0xC;
        IntervalTreeDic::m_Data = 0xC; // 32-bit: m_Tree 0x8, m_Data 0xC
        AvatarWardrobeData::VisualBase = 0x8; // 4 x Int32 (0x8..0x18)
        AvatarWardrobeData::iID = 0x28;
        AvatarWardrobeData::wardrobeType = 0x2C;

        // UmaData
        UMAData::skeleton = 0xCC;
        UMAData::isLocalPlayer = 0x58;
        UMAData::isTeammate = 0x59;
        UMAData::isMeshDirty = 0x36;
        UMAData::isTextureDirty = 0x38;

        // Skeleton
        UMASkeleton::boneHashDataLookup = 0x18;
        UMASkeleton::boneNameHash = 0x8;
        UMASkeleton::boneTransform = 0x10;

        // Replication
        ReplicationEntity::m_PRIDataPool = 0x48;
        ReplicationEntity::m_Datas = 0x8;
        ReplicationEntity::HealthCurrentPtr = 0x10;
        ReplicationEntity::HealthMaxPtr = 0x14;
        ReplicationEntity::WeaponPtr = 0x20;
        ReplicationEntity::EpPtr = 0x28;
        ReplicationEntity::Value = 0x10;

        // Profile
        BaseProfileInfo::AccountID = 0x8;
        BaseProfileInfo::Level = 0x14;
        BaseProfileInfo::NickName = 0x18;

        // Weapon
        Weapon::FireComponent = 0x58;
        Weapon::m_WeaponData = 0x64;
        Weapon::m_WeaponParams = 0x6C;
        Weapon::m_FireDuration = 0x4BC;
        Weapon::m_IsSighting = 0x5E4;
        Weapon::tangentTheta = 0xC;
        Weapon::IntWeaponType = 0xB8;

        // WeaponParams
        WeaponParams::FullDamageDistance = 0x48;
        WeaponParams::PrefireDelay = 0x144;
        WeaponParams::Range = 0x44;

        // PlayerTransformNode
        PlayerTransformNode::Transform = 0x8;
        PlayerTransformNode::m_CachedTransform = 0x38;

        // get_position_Injected
        GetPosWorld::transObj = 0x8; // fixed (may change if Unity updates)
        GetPosWorld::matrix = 0x20; // fixed (may change if Unity updates)
        GetPosWorld::index = 0x24; // fixed (may change if Unity updates)
        GetPosWorld::matrix_list = 0x18; // fixed (may change if Unity updates)
        GetPosWorld::matrix_indices = 0x1C; // fixed (may change if Unity updates)

        // GetHeadPosition
        GetPosWorld::HeadColliderMale = 0x38; // fixed (may change if Unity updates)
        GetPosWorld::HeadColliderFemale = 0x3C; // fixed (may change if Unity updates)
        GetPosWorld::ColliderTransform = 0x8; // fixed (may change if Unity updates)
        GetPosWorld::BoundsCenter_1 = 0x28; // fixed (may change if Unity updates)
        GetPosWorld::BoundsCenter_2 = 0x14; // fixed (may change if Unity updates)
        GetPosWorld::BoundsCenter_3 = 0x60; // fixed (may change if Unity updates)
}

void Offsets::FFTHV7A76() // v76 32-bit
{
        AccessClass = 0x5C; // MANUAL (mantido) — fora da dump

        // (V9) BaseGame / TimeService / Teleport Mark / FollowCamera (dump novo)
        BaseGame::m_UIScene = 0x8;
        BaseGame::m_GameTimer = 0xc;
        TimeService::m_FixedDeltaTime = 0x24;
        UIInGameScene::m_BigMapCtrl = 0x240;
        UIBigMapController::m_MapContentCtrl = 0x5c;
        UIMapContentController::m_LocalMapMarkController = 0x94;
        UIHudPlayerMarkController::m_pos = 0x60;
        FollowCamera::FOVOffset = 0x50;

        // GameVarDef
        GameVarDef::GameVarDef_TypeInfo = 0xa5bc398; // MANUAL (mantido) — typeinfo fora da dump
        GameVarDef::ShootTraceAdjustmentDistanceThreshold = 0x72c; // dump novo
        GameVarDef::EnableAccelerationOnFalling = 0x28f3; // dump novo
        GameVarDef::EnableLowFallingSwapWeapon = 0x2c3d; // dump novo
        GameVarDef::RotationSensitivityMin = 0xfd4; // dump novo
        GameVarDef::RotationSensitivityMax = 0xfd8; // dump novo
        GameVarDef::AimRotationSensitivityMin = 0xfdc; // dump novo
        GameVarDef::AimRotationSensitivityMax = 0xfe0; // dump novo

        // GameFacade
        GameFacade::GameFacade_TypeInfo = 0xa5bc344; // MANUAL (mantido) — typeinfo fora da dump
        GameFacade::CurrentMatchGame = 0x4; // dump novo

        // MatchGame
        MatchGame::m_Match = 0x50; // dump novo
        MatchGame::m_CameraControllerManager = 0x74; // dump novo

        // Match
        Match::m_State = 0x8c; // dump novo
        Match::m_LocalPlayer = 0x94; // dump novo
        Match::m_LocalObserver = 0xb4; // dump novo
        Match::m_AttackableEntities = 0x140; // dump novo

        // Camera
        CameraControllerManager::m_Camera = 0x10; // dump novo
        Camera::m_CachedPtr = 0x8; // dump novo
        Camera::ViewMatrix = 0xE8; // MANUAL (mantido) — interno da Unity

        // Observer
        Observer::m_TargetPlayer = 0x28; // dump novo

        // Player / PlayerNetwork
        // General
        Player::IsClientBot = 0x324; // dump novo
        Player::IsFemale = 0x830; // dump novo
        Player::IsFiring = 0x58c; // dump novo
        Player::UGCStartFiring = 0x17d; // dump novo
        Player::IsPrepareAttack = 0x58c; // dump novo = IsFiring
        Player::m_IsCurFrameFowardLockToAimRot = 0x214; // dump novo
        Player::m_WaitForForceSync = 0x56c; // dump novo
        Player::m_TransformType = 0xcc8; // dump novo
        Player::m_AimRotation = 0x440; // dump novo
        Player::m_AuxAimRotation = 0x454; // dump novo
        Player::m_AimAssist = 0x464; // dump novo
        Player::m_EAimAssit = 0x47c; // dump novo
        Player::m_AimAssistOnSighting = 0x480; // dump novo
        Player::m_LastAimingInfoFromWeapon = 0x9d0; // dump novo
        Player::MainCameraTransform = 0x28c; // dump novo
        Player::m_SwapWeaponTime = 0x564; // dump novo
        Player::m_Attributes = 0x500; // dump novo
        Player::m_AvatarManager = 0x504; // dump novo
        Player::m_InventoryManager = 0x4ec; // dump novo
        Player::m_UserControl = 0x344; // dump novo
        Player::m_FollowCamera = 0x494; // dump novo (V9 — Vision Hack)
        Player::m_HeadCollider = 0x4e8; // dump novo
        Player::m_fireColliders = 0x7b4; // dump novo
        Player::HeadNode = 0x49c; // dump novo
        Player::m_HipNode = 0x4a0; // dump novo
        Player::m_BloodEffectNode = 0x4a4; // dump novo
        Player::m_RootNode = 0x4b0; // dump novo
        Player::m_BoneRootNode = 0x4b4; // dump novo
        Player::m_WeaponMountNode = 0x498; // dump novo
        Player::m_LeftWeaponNode = 0x4c8; // dump novo
        Player::m_FlightNode = 0x4ac; // dump novo
        Player::m_RightArmNode = 0x4d4; // dump novo
        Player::m_LeftArmNode = 0x4d0; // dump novo
        Player::m_RightForeArmNode = 0x4dc; // dump novo
        Player::m_LeftForeArmNode = 0x4e4; // dump novo
        Player::m_RightHandNode = 0x4d8; // dump novo
        Player::m_LeftHandNode = 0x4e0; // dump novo
        Player::m_RightAnkleNode = 0x4bc; // dump novo
        Player::m_LeftAnkleNode = 0x4b8; // dump novo
        Player::m_RightToeNode = 0x4c4; // dump novo
        Player::m_LeftToeNode = 0x4c0; // dump novo

        // PlayerNetwork
        PlayerNetwork::m_ShadowState = 0x1a60; // dump novo
        PlayerNetwork::m_Profile = 0x1a74; // dump novo

        // Shadow
        ShadowState::TargetPhysXPose = 0x78; // dump novo

        // PlayerAttributes
        PlayerAttributes::m_EatSpeedScale = 0x88; // dump novo
        PlayerAttributes::m_FireIntervalScale = 0x1c4; // dump novo
        PlayerAttributes::ReloadNoConsumeAmmoclip = 0xc0; // dump novo (V9)
        PlayerAttributes::ShootNoReload = 0xc1; // dump novo (V9)
        PlayerAttributes::RunSpeedUpScale = 0x210; // dump novo (V9)
        PlayerAttributes::FallingSpeedUpScale = 0x20c; // dump novo (V9)

        // AimAssistAutoLock
        AimAssistAutoLock::m_TargetHeuristic = 0x8; // dump novo
        AimAssistAutoLock::m_Entity = 0xc; // dump novo

        // UserControlHandler
        UserControlHandler::m_AxisData = 0x34; // dump novo
        UserControlHandler::m_FingerInDashArea = 0x4c; // dump novo
        UserControlHandler::m_IsTouched = 0x37; // dump novo
        UserControlHandler::m_LockFingerInDashArea = 0x50; // dump novo
        UserControlHandler::m_DashByMovingJoystick = 0x58; // dump novo

        // AimAssistOnSighting
        AimAssistOnSighting::m_fAimAssistCurrentLerpTime = 0x44; // dump novo

        // HitObjectInfo
        HitObjectInfo::RayDir = 0x2c; // dump novo
        HitObjectInfo::StartPosition = 0x38; // dump novo

        // InventoryManager
        InventoryManager::m_itemOnHand = 0x54; // dump novo

        // Avatar
        AvatarManager::m_Avatar = 0xa8; // dump novo
        UMAAvatarBase::umaData = 0x14; // dump novo
        UmaAvatarSimple::IsVisible = 0x95; // dump novo

        // (V9) Skin Changer — UMA + wardrobe (dump novo)
        UmaAvatarSimple::m_Recipes = 0x6c;
        UmaAvatarSimple::m_VisibleSlots = 0x70;
        UmaAvatarSimple::m_ChangedSlots = 0x74;
        UmaAvatarSimple::lastBuildNotFinish = 0x78;
        UmaAvatarSimple::m_CustomTextureDirty = 0x94;
        AvatarWardrobeDataManager::AvatarWardrobeDataManager_TypeInfo = 0xa5bc3ec; // MANUAL (auto-resolve no GameConfig)
        AvatarWardrobeDataManager::m_dictIdToWardrobeDataTree = 0xc;
        IntervalTreeDic::m_Data = 0xc; // 32-bit: m_Tree 0x8, m_Data 0xc
        AvatarWardrobeData::VisualBase = 0x8; // 4 x Int32 (0x8..0x18)
        AvatarWardrobeData::iID = 0x28;
        AvatarWardrobeData::wardrobeType = 0x2c;

        // UmaData
        UMAData::skeleton = 0xcc; // dump novo
        UMAData::isLocalPlayer = 0x58; // dump novo
        UMAData::isTeammate = 0x59; // dump novo
        UMAData::isMeshDirty = 0x36; // dump novo (V9)
        UMAData::isTextureDirty = 0x38; // dump novo (V9)

        // Skeleton
        UMASkeleton::boneHashDataLookup = 0x18; // dump novo
        UMASkeleton::boneNameHash = 0x8; // dump novo — (BoneData)
        UMASkeleton::boneTransform = 0x10; // dump novo — (BoneData)

        // Replication
        ReplicationEntity::m_PRIDataPool = 0x48; // dump novo
        ReplicationEntity::m_Datas = 0x8; // dump novo
        ReplicationEntity::HealthCurrentPtr = 0x10; // dump novo
        ReplicationEntity::HealthMaxPtr = 0x14; // dump novo
        ReplicationEntity::WeaponPtr = 0x20; // dump novo
        ReplicationEntity::EpPtr = 0x28; // dump novo
        ReplicationEntity::Value = 0x10; // MANUAL (mantido) — validado em campo na V8.2

        // Profile
        BaseProfileInfo::AccountID = 0x8; // dump novo
        BaseProfileInfo::Level = 0x14; // dump novo
        BaseProfileInfo::NickName = 0x18; // dump novo

        // Weapon
        Weapon::FireComponent = 0x58; // dump novo
        Weapon::m_WeaponData = 0x64; // dump novo
        Weapon::m_WeaponParams = 0x6c; // dump novo
        Weapon::m_FireDuration = 0x500; // dump novo
        Weapon::m_IsSighting = 0x670; // dump novo
        Weapon::tangentTheta = 0xc; // dump novo
        Weapon::IntWeaponType = 0xb8; // dump novo

        // WeaponParams
        WeaponParams::FullDamageDistance = 0x48; // dump novo
        WeaponParams::PrefireDelay = 0x144; // dump novo
        WeaponParams::Range = 0x44; // dump novo

        // PlayerTransformNode
        PlayerTransformNode::Transform = 0x8; // dump novo
        PlayerTransformNode::m_CachedTransform = 0x38; // dump novo

        // get_position_Injected (fixed — so mudam se a Unity mudar)
        GetPosWorld::transObj = 0x8; // fixed
        GetPosWorld::matrix = 0x20; // fixed
        GetPosWorld::index = 0x24; // fixed
        GetPosWorld::matrix_list = 0x18; // fixed
        GetPosWorld::matrix_indices = 0x1C; // fixed

        // GetHeadPosition (fixed — so mudam se a Unity mudar)
        GetPosWorld::HeadColliderMale = 0x38; // fixed
        GetPosWorld::HeadColliderFemale = 0x3C; // fixed
        GetPosWorld::ColliderTransform = 0x8; // fixed
        GetPosWorld::BoundsCenter_1 = 0x28; // fixed
        GetPosWorld::BoundsCenter_2 = 0x14; // fixed
        GetPosWorld::BoundsCenter_3 = 0x60; // fixed
}
