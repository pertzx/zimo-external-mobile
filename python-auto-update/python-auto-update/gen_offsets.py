#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
gen_offsets.py — gera os novos blocos FFTHV7A76() e FFTHV8A() do
Offsets.cpp a partir de offsets_result.json, mantendo valores manuais
(typeinfo/AccessClass/ViewMatrix/GetPosWorld) e a ordem original.
Uso: 1) python3 build_table.py   (gera offsets_result.json)
     2) python3 gen_offsets.py   (gera new_v76.cpp / new_v8a.cpp)
"""
import json
import os
import re

BASE = os.path.dirname(os.path.abspath(__file__))
RES = json.load(open(os.path.join(BASE, "offsets_result.json")))


def v(profile, key, default="0"):
    e = RES.get(key)
    if not e or not e.get(profile):
        return default
    return e[profile]


def line(prefix, key, comment, profile, manual=None):
    if manual is not None:
        val = manual
        tag = "MANUAL (mantido)"
    else:
        val = v(profile, key)
        tag = "dump novo"
    return f"        {prefix} = {val}; // {tag} — {comment}"


def block_v76():
    o = []
    o.append("void Offsets::FFTHV7A76() // v76 32-bit")
    o.append("{")
    o.append("        AccessClass = 0x5C; // MANUAL (mantido) — fora da dump")
    o.append("")
    o.append("        // GameVarDef")
    o.append("        GameVarDef::GameVarDef_TypeInfo = 0xa5bc398; // MANUAL (mantido) — typeinfo fora da dump")
    o.append(line("GameVarDef::ShootTraceAdjustmentDistanceThreshold", "GameVarDef.ShootTraceAdjustmentDistanceThreshold", "", "v7a"))
    o.append(line("GameVarDef::EnableAccelerationOnFalling", "GameVarDef.EnableAccelerationOnFalling", "", "v7a"))
    o.append(line("GameVarDef::EnableLowFallingSwapWeapon", "GameVarDef.EnableLowFallingSwapWeapon", "", "v7a"))
    o.append(line("GameVarDef::RotationSensitivityMin", "GameVarDef.RotationSensitivityMin", "", "v7a"))
    o.append(line("GameVarDef::RotationSensitivityMax", "GameVarDef.RotationSensitivityMax", "", "v7a"))
    o.append(line("GameVarDef::AimRotationSensitivityMin", "GameVarDef.AimRotationSensitivityMin", "", "v7a"))
    o.append(line("GameVarDef::AimRotationSensitivityMax", "GameVarDef.AimRotationSensitivityMax", "", "v7a"))
    o.append("")
    o.append("        // GameFacade")
    o.append("        GameFacade::GameFacade_TypeInfo = 0xa5bc344; // MANUAL (mantido) — typeinfo fora da dump")
    o.append(line("GameFacade::CurrentMatchGame", "GameFacade.CurrentMatchGame", "", "v7a"))
    o.append("")
    o.append("        // MatchGame")
    o.append(line("MatchGame::m_Match", "MatchGame.m_Match", "", "v7a"))
    o.append(line("MatchGame::m_CameraControllerManager", "MatchGame.m_CameraControllerManager", "", "v7a"))
    o.append("")
    o.append("        // Match")
    o.append(line("Match::m_State", "Match.m_State", "", "v7a"))
    o.append(line("Match::m_LocalPlayer", "Match.m_LocalPlayer", "", "v7a"))
    o.append(line("Match::m_LocalObserver", "Match.m_LocalObserver", "", "v7a"))
    o.append(line("Match::m_AttackableEntities", "Match.m_AttackableEntities", "", "v7a"))
    o.append("")
    o.append("        // Camera")
    o.append(line("CameraControllerManager::m_Camera", "CameraControllerManager.m_Camera", "", "v7a"))
    o.append(line("Camera::m_CachedPtr", "Camera.m_CachedPtr", "", "v7a"))
    o.append("        Camera::ViewMatrix = 0xE8; // MANUAL (mantido) — interno da Unity")
    o.append("")
    o.append("        // Observer")
    o.append(line("Observer::m_TargetPlayer", "Observer.m_TargetPlayer", "", "v7a"))
    o.append("")
    o.append("        // Player / PlayerNetwork")
    P = [
        ("IsClientBot", "IsClientBot"), ("IsFemale", "IsFemale"), ("IsFiring", "IsFiring"),
        ("UGCStartFiring", "UGCStartFiring"), ("IsPrepareAttack", "IsFiring"),
        ("m_IsCurFrameFowardLockToAimRot", "m_IsCurFrameFowardLockToAimRot"),
        ("m_WaitForForceSync", "m_WaitForForceSync"), ("m_TransformType", "m_TransformType"),
        ("m_AimRotation", "m_AimRotation"), ("m_AuxAimRotation", "m_AuxAimRotation"),
        ("m_AimAssist", "m_AimAssist"), ("m_EAimAssit", "m_EAimAssit"),
        ("m_AimAssistOnSighting", "m_AimAssistOnSighting"),
        ("m_LastAimingInfoFromWeapon", "m_LastAimingInfoFromWeapon"),
        ("MainCameraTransform", "MainCameraTransform"), ("m_SwapWeaponTime", "m_SwapWeaponTime"),
        ("m_Attributes", "m_Attributes"), ("m_AvatarManager", "m_AvatarManager"),
        ("m_InventoryManager", "m_InventoryManager"), ("m_UserControl", "m_UserControl"),
        ("m_HeadCollider", "m_HeadCollider"), ("m_fireColliders", "m_fireColliders"),
        ("HeadNode", "HeadNode"), ("m_HipNode", "m_HipNode"),
        ("m_BloodEffectNode", "m_BloodEffectNode"), ("m_RootNode", "m_RootNode"),
        ("m_BoneRootNode", "m_BoneRootNode"), ("m_WeaponMountNode", "m_WeaponMountNode"),
        ("m_LeftWeaponNode", "m_LeftWeaponNode"), ("m_FlightNode", "m_FlightNode"),
        ("m_RightArmNode", "m_RightArmNode"), ("m_LeftArmNode", "m_LeftArmNode"),
        ("m_RightForeArmNode", "m_RightForeArmNode"), ("m_LeftForeArmNode", "m_LeftForeArmNode"),
        ("m_RightHandNode", "m_RightHandNode"), ("m_LeftHandNode", "m_LeftHandNode"),
        ("m_RightAnkleNode", "m_RightAnkleNode"), ("m_LeftAnkleNode", "m_LeftAnkleNode"),
        ("m_RightToeNode", "m_RightToeNode"), ("m_LeftToeNode", "m_LeftToeNode"),
    ]
    o.append("        // General")
    for code, key in P[:5]:
        cmt = " = IsFiring" if code == "IsPrepareAttack" else ""
        o.append(f"        Player::{code} = {v('v7a', key)}; // dump novo{cmt}")
    for code, key in P[5:]:
        o.append(f"        Player::{code} = {v('v7a', key)}; // dump novo")
    o.append("")
    o.append("        // PlayerNetwork")
    o.append(line("PlayerNetwork::m_ShadowState", "PlayerNetwork.m_ShadowState", "", "v7a"))
    o.append(line("PlayerNetwork::m_Profile", "PlayerNetwork.m_Profile", "", "v7a"))
    o.append("")
    o.append("        // Shadow")
    o.append(line("ShadowState::TargetPhysXPose", "ShadowState.TargetPhysXPose", "", "v7a"))
    o.append("")
    o.append("        // PlayerAttributes")
    o.append(line("PlayerAttributes::m_EatSpeedScale", "PlayerAttributes.m_EatSpeedScale", "", "v7a"))
    o.append(line("PlayerAttributes::m_FireIntervalScale", "PlayerAttributes.m_FireIntervalScale", "", "v7a"))
    o.append("")
    o.append("        // AimAssistAutoLock")
    o.append(line("AimAssistAutoLock::m_TargetHeuristic", "AimAssistAutoLock.m_TargetHeuristic", "", "v7a"))
    o.append(line("AimAssistAutoLock::m_Entity", "AimAssistAutoLock.m_Entity", "", "v7a"))
    o.append("")
    o.append("        // UserControlHandler")
    o.append(line("UserControlHandler::m_AxisData", "UserControlHandler.m_AxisData", "", "v7a"))
    o.append(line("UserControlHandler::m_FingerInDashArea", "UserControlHandler.m_FingerInDashArea", "", "v7a"))
    o.append(line("UserControlHandler::m_IsTouched", "UserControlAxisData.m_IsTouched", "", "v7a"))
    o.append(line("UserControlHandler::m_LockFingerInDashArea", "UserControlHandler.m_LockFingerInDashArea", "", "v7a"))
    o.append(line("UserControlHandler::m_DashByMovingJoystick", "UserControlHandler.<DashByMovingJoystick>k__BackingField", "", "v7a"))
    o.append("")
    o.append("        // AimAssistOnSighting")
    o.append(line("AimAssistOnSighting::m_fAimAssistCurrentLerpTime", "AimAssistOnSighting.m_fAimAssistCurrentLerpTime", "", "v7a"))
    o.append("")
    o.append("        // HitObjectInfo")
    o.append(line("HitObjectInfo::RayDir", "HitObjectInfo.RayDir", "", "v7a"))
    o.append(line("HitObjectInfo::StartPosition", "HitObjectInfo.StartPosition", "", "v7a"))
    o.append("")
    o.append("        // InventoryManager")
    o.append(line("InventoryManager::m_itemOnHand", "InventoryManager.m_itemOnHand", "", "v7a"))
    o.append("")
    o.append("        // Avatar")
    o.append(line("AvatarManager::m_Avatar", "AvatarManager.m_Avatar", "", "v7a"))
    o.append(line("UMAAvatarBase::umaData", "UMAAvatarBase.umaData", "", "v7a"))
    o.append(line("UmaAvatarSimple::IsVisible", "UmaAvatarSimple.IsVisible", "", "v7a"))
    o.append("")
    o.append("        // UmaData")
    o.append(line("UMAData::skeleton", "UMAData.skeleton", "", "v7a"))
    o.append(line("UMAData::isLocalPlayer", "UMAData.isLocalPlayer", "", "v7a"))
    o.append(line("UMAData::isTeammate", "UMAData.isTeammate", "", "v7a"))
    o.append("")
    o.append("        // Skeleton")
    o.append(line("UMASkeleton::boneHashDataLookup", "UMASkeleton.boneHashDataLookup", "", "v7a"))
    o.append(line("UMASkeleton::boneNameHash", "BoneData.boneNameHash", "(BoneData)", "v7a"))
    o.append(line("UMASkeleton::boneTransform", "BoneData.boneTransform", "(BoneData)", "v7a"))
    o.append("")
    o.append("        // Replication")
    o.append(line("ReplicationEntity::m_PRIDataPool", "ReplicationEntity.m_PRIDataPool", "", "v7a"))
    o.append(line("ReplicationEntity::m_Datas", "ReplicationDataPoolUnsafe.m_Datas", "", "v7a"))
    o.append(line("ReplicationEntity::HealthCurrentPtr", "ReplicationDataPoolUnsafe.m_Int8Handlers", "", "v7a"))
    o.append(line("ReplicationEntity::HealthMaxPtr", "ReplicationDataPoolUnsafe.m_UInt8Handlers", "", "v7a"))
    o.append(line("ReplicationEntity::WeaponPtr", "ReplicationDataPoolUnsafe.m_Int32Handlers", "", "v7a"))
    o.append(line("ReplicationEntity::EpPtr", "ReplicationDataPoolUnsafe.m_Int64Handlers", "", "v7a"))
    o.append("        ReplicationEntity::Value = 0x10; // MANUAL (mantido) — validado em campo na V8.2")
    o.append("")
    o.append("        // Profile")
    o.append(line("BaseProfileInfo::AccountID", "BaseProfileInfo.AccountID", "", "v7a"))
    o.append(line("BaseProfileInfo::Level", "BaseProfileInfo.Level", "", "v7a"))
    o.append(line("BaseProfileInfo::NickName", "BaseProfileInfo.NickName", "", "v7a"))
    o.append("")
    o.append("        // Weapon")
    o.append(line("Weapon::FireComponent", "Weapon.FireComponent", "", "v7a"))
    o.append(line("Weapon::m_WeaponData", "Weapon.m_WeaponData", "", "v7a"))
    o.append(line("Weapon::m_WeaponParams", "Weapon.m_WeaponParams", "", "v7a"))
    o.append(line("Weapon::m_FireDuration", "Weapon.m_FireDuration", "", "v7a"))
    o.append(line("Weapon::m_IsSighting", "Weapon.m_IsSighting", "", "v7a"))
    o.append(line("Weapon::tangentTheta", "WeaponFireComponent.tangentTheta", "", "v7a"))
    o.append(line("Weapon::IntWeaponType", "WeaponData.IntWeaponType", "", "v7a"))
    o.append("")
    o.append("        // WeaponParams")
    o.append(line("WeaponParams::FullDamageDistance", "WeaponParams.FullDamageDistance", "", "v7a"))
    o.append(line("WeaponParams::PrefireDelay", "WeaponParams.PrefireDelay", "", "v7a"))
    o.append(line("WeaponParams::Range", "WeaponParams.Range", "", "v7a"))
    o.append("")
    o.append("        // PlayerTransformNode")
    o.append(line("PlayerTransformNode::Transform", "TransformNode.<transform>k__BackingField", "", "v7a"))
    o.append(line("PlayerTransformNode::m_CachedTransform", "Entity.m_CachedTransform", "", "v7a"))
    o.append("")
    o.append("        // get_position_Injected (fixed — so mudam se a Unity mudar)")
    o.append("        GetPosWorld::transObj = 0x8; // fixed")
    o.append("        GetPosWorld::matrix = 0x20; // fixed")
    o.append("        GetPosWorld::index = 0x24; // fixed")
    o.append("        GetPosWorld::matrix_list = 0x18; // fixed")
    o.append("        GetPosWorld::matrix_indices = 0x1C; // fixed")
    o.append("")
    o.append("        // GetHeadPosition (fixed — so mudam se a Unity mudar)")
    o.append("        GetPosWorld::HeadColliderMale = 0x38; // fixed")
    o.append("        GetPosWorld::HeadColliderFemale = 0x3C; // fixed")
    o.append("        GetPosWorld::ColliderTransform = 0x8; // fixed")
    o.append("        GetPosWorld::BoundsCenter_1 = 0x28; // fixed")
    o.append("        GetPosWorld::BoundsCenter_2 = 0x14; // fixed")
    o.append("        GetPosWorld::BoundsCenter_3 = 0x60; // fixed")
    o.append("}")
    return "\n".join(o) + "\n"


def block_v8a():
    o = []
    o.append("void Offsets::FFTHV8A() // 64-bit")
    o.append("{")
    o.append("        AccessClass = 0xB8; // MANUAL (mantido) — fora da dump")
    o.append("")
    o.append("        // GameVarDef")
    o.append("        GameVarDef::GameVarDef_TypeInfo = 0xac1e810; // MANUAL (mantido) — typeinfo fora da dump")
    o.append(line("GameVarDef::ShootTraceAdjustmentDistanceThreshold", "GameVarDef.ShootTraceAdjustmentDistanceThreshold", "", "v8a"))
    o.append(line("GameVarDef::EnableAccelerationOnFalling", "GameVarDef.EnableAccelerationOnFalling", "", "v8a"))
    o.append(line("GameVarDef::EnableLowFallingSwapWeapon", "GameVarDef.EnableLowFallingSwapWeapon", "", "v8a"))
    o.append(line("GameVarDef::RotationSensitivityMin", "GameVarDef.RotationSensitivityMin", "", "v8a"))
    o.append(line("GameVarDef::RotationSensitivityMax", "GameVarDef.RotationSensitivityMax", "", "v8a"))
    o.append(line("GameVarDef::AimRotationSensitivityMin", "GameVarDef.AimRotationSensitivityMin", "", "v8a"))
    o.append(line("GameVarDef::AimRotationSensitivityMax", "GameVarDef.AimRotationSensitivityMax", "", "v8a"))
    o.append("")
    o.append("        // GameFacade")
    o.append("        GameFacade::GameFacade_TypeInfo = 0xac1e768; // MANUAL (mantido) — typeinfo fora da dump")
    o.append(line("GameFacade::CurrentMatchGame", "GameFacade.CurrentMatchGame", "", "v8a"))
    o.append("")
    o.append("        // MatchGame")
    o.append(line("MatchGame::m_Match", "MatchGame.m_Match", "", "v8a"))
    o.append(line("MatchGame::m_CameraControllerManager", "MatchGame.m_CameraControllerManager", "", "v8a"))
    o.append("")
    o.append("        // Match")
    o.append(line("Match::m_State", "Match.m_State", "", "v8a"))
    o.append(line("Match::m_LocalPlayer", "Match.m_LocalPlayer", "", "v8a"))
    o.append(line("Match::m_LocalObserver", "Match.m_LocalObserver", "", "v8a"))
    o.append(line("Match::m_AttackableEntities", "Match.m_AttackableEntities", "", "v8a"))
    o.append("")
    o.append("        // Camera")
    o.append(line("CameraControllerManager::m_Camera", "CameraControllerManager.m_Camera", "", "v8a"))
    o.append(line("Camera::m_CachedPtr", "Camera.m_CachedPtr", "", "v8a"))
    o.append("        Camera::ViewMatrix = 0x128; // MANUAL (mantido) — interno da Unity (estimativa)")
    o.append("")
    o.append("        // Observer")
    o.append(line("Observer::m_TargetPlayer", "Observer.m_TargetPlayer", "", "v8a"))
    o.append("")
    o.append("        // Player / PlayerNetwork")
    P = [
        ("IsClientBot", "IsClientBot"), ("IsFemale", "IsFemale"), ("IsFiring", "IsFiring"),
        ("UGCStartFiring", "UGCStartFiring"), ("IsPrepareAttack", "IsFiring"),
        ("m_IsCurFrameFowardLockToAimRot", "m_IsCurFrameFowardLockToAimRot"),
        ("m_WaitForForceSync", "m_WaitForForceSync"), ("m_TransformType", "m_TransformType"),
        ("m_AimRotation", "m_AimRotation"), ("m_AuxAimRotation", "m_AuxAimRotation"),
        ("m_AimAssist", "m_AimAssist"), ("m_EAimAssit", "m_EAimAssit"),
        ("m_AimAssistOnSighting", "m_AimAssistOnSighting"),
        ("m_LastAimingInfoFromWeapon", "m_LastAimingInfoFromWeapon"),
        ("MainCameraTransform", "MainCameraTransform"), ("m_SwapWeaponTime", "m_SwapWeaponTime"),
        ("m_Attributes", "m_Attributes"), ("m_AvatarManager", "m_AvatarManager"),
        ("m_InventoryManager", "m_InventoryManager"), ("m_UserControl", "m_UserControl"),
        ("m_HeadCollider", "m_HeadCollider"), ("m_fireColliders", "m_fireColliders"),
        ("HeadNode", "HeadNode"), ("m_HipNode", "m_HipNode"),
        ("m_BloodEffectNode", "m_BloodEffectNode"), ("m_RootNode", "m_RootNode"),
        ("m_BoneRootNode", "m_BoneRootNode"), ("m_WeaponMountNode", "m_WeaponMountNode"),
        ("m_LeftWeaponNode", "m_LeftWeaponNode"), ("m_FlightNode", "m_FlightNode"),
        ("m_RightArmNode", "m_RightArmNode"), ("m_LeftArmNode", "m_LeftArmNode"),
        ("m_RightForeArmNode", "m_RightForeArmNode"), ("m_LeftForeArmNode", "m_LeftForeArmNode"),
        ("m_RightHandNode", "m_RightHandNode"), ("m_LeftHandNode", "m_LeftHandNode"),
        ("m_RightAnkleNode", "m_RightAnkleNode"), ("m_LeftAnkleNode", "m_LeftAnkleNode"),
        ("m_RightToeNode", "m_RightToeNode"), ("m_LeftToeNode", "m_LeftToeNode"),
    ]
    o.append("        // General")
    for code, key in P[:5]:
        cmt = " = IsFiring" if code == "IsPrepareAttack" else ""
        o.append(f"        Player::{code} = {v('v8a', key)}; // dump novo{cmt}")
    for code, key in P[5:]:
        o.append(f"        Player::{code} = {v('v8a', key)}; // dump novo")
    o.append("")
    o.append("        // PlayerNetwork")
    o.append(line("PlayerNetwork::m_ShadowState", "PlayerNetwork.m_ShadowState", "", "v8a"))
    o.append(line("PlayerNetwork::m_Profile", "PlayerNetwork.m_Profile", "", "v8a"))
    o.append("")
    o.append("        // Shadow")
    o.append(line("ShadowState::TargetPhysXPose", "ShadowState.TargetPhysXPose", "", "v8a"))
    o.append("")
    o.append("        // PlayerAttributes")
    o.append(line("PlayerAttributes::m_EatSpeedScale", "PlayerAttributes.m_EatSpeedScale", "", "v8a"))
    o.append(line("PlayerAttributes::m_FireIntervalScale", "PlayerAttributes.m_FireIntervalScale", "", "v8a"))
    o.append("")
    o.append("        // AimAssistAutoLock")
    o.append(line("AimAssistAutoLock::m_TargetHeuristic", "AimAssistAutoLock.m_TargetHeuristic", "", "v8a"))
    o.append(line("AimAssistAutoLock::m_Entity", "AimAssistAutoLock.m_Entity", "", "v8a"))
    o.append("")
    o.append("        // UserControlHandler")
    o.append(line("UserControlHandler::m_AxisData", "UserControlHandler.m_AxisData", "", "v8a"))
    o.append(line("UserControlHandler::m_FingerInDashArea", "UserControlHandler.m_FingerInDashArea", "", "v8a"))
    o.append(line("UserControlHandler::m_IsTouched", "UserControlAxisData.m_IsTouched", "", "v8a"))
    o.append(line("UserControlHandler::m_LockFingerInDashArea", "UserControlHandler.m_LockFingerInDashArea", "", "v8a"))
    o.append(line("UserControlHandler::m_DashByMovingJoystick", "UserControlHandler.<DashByMovingJoystick>k__BackingField", "", "v8a"))
    o.append("")
    o.append("        // AimAssistOnSighting")
    o.append(line("AimAssistOnSighting::m_fAimAssistCurrentLerpTime", "AimAssistOnSighting.m_fAimAssistCurrentLerpTime", "", "v8a"))
    o.append("")
    o.append("        // HitObjectInfo")
    o.append(line("HitObjectInfo::RayDir", "HitObjectInfo.RayDir", "", "v8a"))
    o.append(line("HitObjectInfo::StartPosition", "HitObjectInfo.StartPosition", "", "v8a"))
    o.append("")
    o.append("        // InventoryManager")
    o.append(line("InventoryManager::m_itemOnHand", "InventoryManager.m_itemOnHand", "", "v8a"))
    o.append("")
    o.append("        // Avatar")
    o.append(line("AvatarManager::m_Avatar", "AvatarManager.m_Avatar", "", "v8a"))
    o.append(line("UMAAvatarBase::umaData", "UMAAvatarBase.umaData", "", "v8a"))
    o.append(line("UmaAvatarSimple::IsVisible", "UmaAvatarSimple.IsVisible", "", "v8a"))
    o.append("")
    o.append("        // UMAData")
    o.append(line("UMAData::skeleton", "UMAData.skeleton", "", "v8a"))
    o.append(line("UMAData::isLocalPlayer", "UMAData.isLocalPlayer", "", "v8a"))
    o.append(line("UMAData::isTeammate", "UMAData.isTeammate", "", "v8a"))
    o.append("")
    o.append("        // UMASkeleton")
    o.append(line("UMASkeleton::boneHashDataLookup", "UMASkeleton.boneHashDataLookup", "", "v8a"))
    o.append(line("UMASkeleton::boneNameHash", "BoneData.boneNameHash", "(BoneData)", "v8a"))
    o.append(line("UMASkeleton::boneTransform", "BoneData.boneTransform", "(BoneData)", "v8a"))
    o.append("")
    o.append("        // Replication")
    o.append(line("ReplicationEntity::m_PRIDataPool", "ReplicationEntity.m_PRIDataPool", "", "v8a"))
    o.append(line("ReplicationEntity::m_Datas", "ReplicationDataPoolUnsafe.m_Datas", "", "v8a"))
    o.append(line("ReplicationEntity::HealthCurrentPtr", "ReplicationDataPoolUnsafe.m_Int8Handlers", "", "v8a"))
    o.append(line("ReplicationEntity::HealthMaxPtr", "ReplicationDataPoolUnsafe.m_UInt8Handlers", "", "v8a"))
    o.append(line("ReplicationEntity::WeaponPtr", "ReplicationDataPoolUnsafe.m_Int32Handlers", "", "v8a"))
    o.append(line("ReplicationEntity::EpPtr", "ReplicationDataPoolUnsafe.m_Int64Handlers", "", "v8a"))
    o.append("        ReplicationEntity::Value = 0x18; // MANUAL (mantido) — 64-bit")
    o.append("")
    o.append("        // Profile")
    o.append(line("BaseProfileInfo::AccountID", "BaseProfileInfo.AccountID", "", "v8a"))
    o.append(line("BaseProfileInfo::Level", "BaseProfileInfo.Level", "", "v8a"))
    o.append(line("BaseProfileInfo::NickName", "BaseProfileInfo.NickName", "", "v8a"))
    o.append("")
    o.append("        // Weapon")
    o.append(line("Weapon::FireComponent", "Weapon.FireComponent", "", "v8a"))
    o.append(line("Weapon::m_WeaponData", "Weapon.m_WeaponData", "", "v8a"))
    o.append(line("Weapon::m_WeaponParams", "Weapon.m_WeaponParams", "", "v8a"))
    o.append(line("Weapon::m_FireDuration", "Weapon.m_FireDuration", "", "v8a"))
    o.append(line("Weapon::m_IsSighting", "Weapon.m_IsSighting", "", "v8a"))
    o.append(line("Weapon::tangentTheta", "WeaponFireComponent.tangentTheta", "", "v8a"))
    o.append(line("Weapon::IntWeaponType", "WeaponData.IntWeaponType", "", "v8a"))
    o.append("")
    o.append("        // WeaponParams")
    o.append(line("WeaponParams::FullDamageDistance", "WeaponParams.FullDamageDistance", "", "v8a"))
    o.append(line("WeaponParams::PrefireDelay", "WeaponParams.PrefireDelay", "", "v8a"))
    o.append(line("WeaponParams::Range", "WeaponParams.Range", "", "v8a"))
    o.append("")
    o.append("        // PlayerTransformNode")
    o.append(line("PlayerTransformNode::Transform", "TransformNode.<transform>k__BackingField", "", "v8a"))
    o.append(line("PlayerTransformNode::m_CachedTransform", "Entity.m_CachedTransform", "", "v8a"))
    o.append("")
    o.append("        // get_position_Injected (fixed — so mudam se a Unity mudar)")
    o.append("        GetPosWorld::transObj = 0x10; // fixed v8a")
    o.append("        GetPosWorld::matrix = 0x40; // fixed v8a")
    o.append("        GetPosWorld::index = 0x48; // fixed v8a")
    o.append("        GetPosWorld::matrix_list = 0x30; // fixed v8a")
    o.append("        GetPosWorld::matrix_indices = 0x38; // fixed v8a")
    o.append("")
    o.append("        // GetHeadPosition (fixed — so mudam se a Unity mudar)")
    o.append("        GetPosWorld::HeadColliderMale = 0x70; // fixed v8a")
    o.append("        GetPosWorld::HeadColliderFemale = 0x78; // fixed v8a")
    o.append("        GetPosWorld::ColliderTransform = 0x10; // fixed v8a")
    o.append("        GetPosWorld::BoundsCenter_1 = 0x50; // fixed v8a")
    o.append("        GetPosWorld::BoundsCenter_2 = 0x28; // fixed v8a")
    o.append("        GetPosWorld::BoundsCenter_3 = 0xC0; // fixed v8a")
    o.append("}")
    return "\n".join(o) + "\n"


if __name__ == "__main__":
    open(os.path.join(BASE, "new_v76.cpp"), "w").write(block_v76())
    open(os.path.join(BASE, "new_v8a.cpp"), "w").write(block_v8a())
    print("gerado: new_v76.cpp / new_v8a.cpp")
    # amostra
    print(block_v76().splitlines()[1][:60], "...")
    print(block_v8a().splitlines()[1][:60], "...")
