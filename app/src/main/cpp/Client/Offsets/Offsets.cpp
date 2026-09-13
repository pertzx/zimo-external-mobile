
#include <cinttypes>
#include <android/log.h>

#include "Offsets.hpp"
#include <Memory/Memory.hpp>
#include <Globals.hpp>
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
//uintptr_t Offsets::PlayerAttributes::ShootNoReload = 0;
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

    // UMAData
    UMAData::skeleton = 0;
    UMAData::isLocalPlayer = 0;
    UMAData::isTeammate = 0;

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

    LOGI("================================================");
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
        AccessClass = 0x0; // TODO(v8a): preencha

        // GameVarDef
        GameVarDef::GameVarDef_TypeInfo = 0x0; // TODO(v8a)
        GameVarDef::ShootTraceAdjustmentDistanceThreshold = 0x0; // TODO(v8a)
        GameVarDef::EnableAccelerationOnFalling = 0x0; // TODO(v8a)
        GameVarDef::EnableLowFallingSwapWeapon = 0x0; // TODO(v8a)
        GameVarDef::RotationSensitivityMin = 0x0; // TODO(v8a)
        GameVarDef::RotationSensitivityMax = 0x0; // TODO(v8a)
        GameVarDef::AimRotationSensitivityMin = 0x0; // TODO(v8a)
        GameVarDef::AimRotationSensitivityMax = 0x0; // TODO(v8a)

        // GameFacade
        GameFacade::GameFacade_TypeInfo = 0x0; // TODO(v8a)
        GameFacade::CurrentMatchGame = 0x0; // TODO(v8a)

        // MatchGame
        MatchGame::m_Match = 0x0; // TODO(v8a)
        MatchGame::m_CameraControllerManager = 0x0; // TODO(v8a)

        // Match
        Match::m_State = 0x0; // TODO(v8a)
        Match::m_LocalPlayer = 0x0; // TODO(v8a)
        Match::m_LocalObserver = 0x0; // TODO(v8a)
        Match::m_AttackableEntities = 0x0; // TODO(v8a)

        // Camera
        CameraControllerManager::m_Camera = 0x0; // TODO(v8a)
        Camera::m_CachedPtr = 0x0; // TODO(v8a)
        Camera::ViewMatrix = 0x0; // TODO(v8a)

        // Observer
        Observer::m_TargetPlayer = 0x0; // TODO(v8a)

        // Player / PlayerNetwork
        Player::IsClientBot = 0x0; // TODO(v8a)
        Player::IsFemale = 0x0; // TODO(v8a)
		Player::IsFiring = 0x0; // TODO(v8a)
		Player::UGCStartFiring = 0x0; // TODO(v8a)
        Player::IsPrepareAttack = 0x0; // TODO(v8a)
        Player::m_IsCurFrameFowardLockToAimRot = 0x0; // TODO(v8a)
        Player::m_WaitForForceSync = 0x0; // TODO(v8a)
        Player::m_TransformType = 0x0; // TODO(v8a)
        Player::m_AimRotation = 0x0; // TODO(v8a)
        Player::m_AuxAimRotation = 0x0; // TODO(v8a)
        Player::m_AimAssist = 0x0; // TODO(v8a)
        Player::m_EAimAssit = 0x0; // TODO(v8a)
        Player::m_AimAssistOnSighting = 0x0; // TODO(v8a)
        Player::m_LastAimingInfoFromWeapon = 0x0; // TODO(v8a)
        Player::MainCameraTransform = 0x0; // TODO(v8a)
        Player::m_SwapWeaponTime = 0x0; // TODO(v8a)
        Player::m_Attributes = 0x0; // TODO(v8a)
        Player::m_AvatarManager = 0x0; // TODO(v8a)
        Player::m_InventoryManager = 0x0; // TODO(v8a)
        Player::m_UserControl = 0x0; // TODO(v8a)
        Player::m_HeadCollider = 0x0; // TODO(v8a)
        Player::m_fireColliders = 0x0; // TODO(v8a)
        Player::HeadNode = 0x0; // TODO(v8a)
        Player::m_HipNode = 0x0; // TODO(v8a)
        Player::m_BloodEffectNode = 0x0; // TODO(v8a)
        Player::m_RootNode = 0x0; // TODO(v8a)
        Player::m_BoneRootNode = 0x0; // TODO(v8a)
        Player::m_WeaponMountNode = 0x0; // TODO(v8a)
        Player::m_LeftWeaponNode = 0x0; // TODO(v8a)
        Player::m_FlightNode = 0x0; // TODO(v8a)
        Player::m_RightArmNode = 0x0; // TODO(v8a)
        Player::m_LeftArmNode = 0x0; // TODO(v8a)
        Player::m_RightForeArmNode = 0x0; // TODO(v8a)
        Player::m_LeftForeArmNode = 0x0; // TODO(v8a)
        Player::m_RightHandNode = 0x0; // TODO(v8a)
        Player::m_LeftHandNode = 0x0; // TODO(v8a)
        Player::m_RightAnkleNode = 0x0; // TODO(v8a)
        Player::m_LeftAnkleNode = 0x0; // TODO(v8a)
        Player::m_RightToeNode = 0x0; // TODO(v8a)
        Player::m_LeftToeNode = 0x0; // TODO(v8a)

        // PlayerNetwork
        PlayerNetwork::m_ShadowState = 0x0; // TODO(v8a)
        PlayerNetwork::m_Profile = 0x0; // TODO(v8a)

        // Shadow
        ShadowState::TargetPhysXPose = 0x0; // TODO(v8a)

        // PlayerAttributes
        PlayerAttributes::m_EatSpeedScale = 0x0; // TODO(v8a)
        PlayerAttributes::m_FireIntervalScale = 0x0; // TODO(v8a)

        // AimAssistAutoLock
        AimAssistAutoLock::m_TargetHeuristic = 0x0; // TODO(v8a)
        AimAssistAutoLock::m_Entity = 0x0; // TODO(v8a)

        // UserControlHandler
        UserControlHandler::m_AxisData = 0x0; // TODO(v8a)
        UserControlHandler::m_FingerInDashArea = 0x0; // TODO(v8a)
        UserControlHandler::m_IsTouched = 0x0; // TODO(v8a)
        UserControlHandler::m_LockFingerInDashArea = 0x0; // TODO(v8a)
        UserControlHandler::m_DashByMovingJoystick = 0x0; // TODO(v8a)

        // AimAssistOnSighting
        AimAssistOnSighting::m_fAimAssistCurrentLerpTime = 0x0; // TODO(v8a)

        // HitObjectInfo
        HitObjectInfo::RayDir = 0x0; // TODO(v8a)
        HitObjectInfo::StartPosition = 0x0; // TODO(v8a)

        // InventoryManager
        InventoryManager::m_itemOnHand = 0x0; // TODO(v8a)

        // Avatar
        AvatarManager::m_Avatar = 0x0; // TODO(v8a)
        UMAAvatarBase::umaData = 0x0; // TODO(v8a)
        UmaAvatarSimple::IsVisible = 0x0; // TODO(v8a)

        // UMAData
        UMAData::skeleton = 0x0; // TODO(v8a)
        UMAData::isLocalPlayer = 0x0; // TODO(v8a)
        UMAData::isTeammate = 0x0; // TODO(v8a)

        // UMASkeleton
        UMASkeleton::boneHashDataLookup = 0x0; // TODO(v8a)
        UMASkeleton::boneNameHash = 0x0; // TODO(v8a)
        UMASkeleton::boneTransform = 0x0; // TODO(v8a)

        // Replication
        ReplicationEntity::m_PRIDataPool = 0x0; // TODO(v8a)
        ReplicationEntity::m_Datas = 0x0; // TODO(v8a)
        ReplicationEntity::HealthCurrentPtr = 0x0; // TODO(v8a)
        ReplicationEntity::HealthMaxPtr = 0x0; // TODO(v8a)
        ReplicationEntity::WeaponPtr = 0x0; // TODO(v8a)
        ReplicationEntity::EpPtr = 0x0; // TODO(v8a)
        ReplicationEntity::Value = 0x0; // TODO(v8a)

        // Profile
        BaseProfileInfo::AccountID = 0x0; // TODO(v8a)
        BaseProfileInfo::Level = 0x0; // TODO(v8a)
        BaseProfileInfo::NickName = 0x0; // TODO(v8a)

        // Weapon
        Weapon::FireComponent = 0x0; // TODO(v8a)
        Weapon::m_WeaponData = 0x0; // TODO(v8a)
        Weapon::m_WeaponParams = 0x0; // TODO(v8a)
        Weapon::m_FireDuration = 0x0; // TODO(v8a)
        Weapon::m_IsSighting = 0x0; // TODO(v8a)
        Weapon::tangentTheta = 0x0; // TODO(v8a)
        Weapon::IntWeaponType = 0x0; // TODO(v8a)

        // WeaponParams
        WeaponParams::FullDamageDistance = 0x0; // TODO(v8a)
        WeaponParams::PrefireDelay = 0x0; // TODO(v8a)
        WeaponParams::Range = 0x0; // TODO(v8a)

        // PlayerTransformNode
        PlayerTransformNode::Transform = 0x0; // TODO(v8a)
        PlayerTransformNode::m_CachedTransform = 0x0; // TODO(v8a)

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

	// GameVarDef
	GameVarDef::GameVarDef_TypeInfo = 0xABFF414; // TODO: update manually
	GameVarDef::ShootTraceAdjustmentDistanceThreshold = 0x674;
	GameVarDef::EnableAccelerationOnFalling = 0x27CA;
	GameVarDef::EnableLowFallingSwapWeapon = 0x2AE5;
	GameVarDef::RotationSensitivityMin = 0xF0C;
	GameVarDef::RotationSensitivityMax = 0xF10;
	GameVarDef::AimRotationSensitivityMin = 0xF14;
	GameVarDef::AimRotationSensitivityMax = 0xF18;

	// GameFacade
	GameFacade::GameFacade_TypeInfo = 0xac01850; // TODO: update manually
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

	// Colliders
	Player::m_HeadCollider = 0x4A4;
	Player::m_fireColliders = 0x760;
	//Player::LockedAimingCollider = 0x54;

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

	// UmaData
	UMAData::skeleton = 0xCC;
	UMAData::isLocalPlayer = 0x58;
	UMAData::isTeammate = 0x59;

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
	AccessClass = 0x5C; // TODO: update manually

	// GameVarDef
	GameVarDef::GameVarDef_TypeInfo = 0xABFF734; // TODO: update manually
	GameVarDef::ShootTraceAdjustmentDistanceThreshold = 0x674;
	GameVarDef::EnableAccelerationOnFalling = 0x27CA;
	GameVarDef::EnableLowFallingSwapWeapon = 0x2AE5;
	GameVarDef::RotationSensitivityMin = 0xF0C;
	GameVarDef::RotationSensitivityMax = 0xF10;
	GameVarDef::AimRotationSensitivityMin = 0xF14;
	GameVarDef::AimRotationSensitivityMax = 0xF18;

	// GameFacade
	GameFacade::GameFacade_TypeInfo = 0xac01850; // TODO: update manually
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

	// Colliders
	Player::m_HeadCollider = 0x4A4;
	Player::m_fireColliders = 0x760;
	//Player::LockedAimingCollider = 0x54;

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

	// UmaData
	UMAData::skeleton = 0xCC;
	UMAData::isLocalPlayer = 0x58;
	UMAData::isTeammate = 0x59;

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