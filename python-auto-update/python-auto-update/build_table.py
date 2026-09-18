#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
build_table.py — tabela final de offsets: code_v76 | novo v7a | novo v8a
para TODOS os campos usados pelo Offsets.cpp, com verificacao.

Saidas:
  - imprime tabela de comparacao
  - grava /home/z/my-project/scripts/offsets_result.json
"""
import sys
import os
import json

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from extract_fields import (B, V7, V8, resolve, align, player, bridge_lookup)

PB, P7, P8 = resolve(B, "Player"), resolve(V7, "Player"), resolve(V8, "Player")


def align_map(bcls, n7, cache={}):
    key = bcls.name if bcls else None
    if key not in cache:
        cache[key] = align(bcls, n7) if (bcls and n7) else {}
    return cache[key]


def via_bridge(bcls, field_name, n7, n8):
    """Retorna (f7, f8, info) para o campo bridge field_name alinhado.
    n7/n8 sao OBJETOS Cls das classes novas."""
    if bcls is None or n7 is None or n8 is None:
        return None, None, "cls None"
    bf = bcls.f(field_name)
    if bf is None:
        return None, None, "bridge field N/A"
    bi = bcls.fields.index(bf)
    mp = align_map(bcls, n7)
    j = mp.get(bi)
    if j is None:
        # fallback: vizinho mapeado mais proximo
        lower = [k for k in mp if k < bi]
        if lower:
            k0 = max(lower)
            j = mp[k0] + (bi - k0)
            info = f"~{k0}->{mp[k0]}"
        else:
            return None, None, "sem match"
    else:
        info = f"{bi}->{j}"
    if j >= len(n7.fields):
        return None, None, "idx fora"
    return n7.fields[j], n8.fields[j], info


def player_aligned(field_name):
    bf, bi = bridge_lookup("Player", field_name)
    if bf is None:
        return None, None, "bridge N/A"
    mp = align_map(PB, P7)
    j = mp.get(bi)
    if j is None:
        lower = [k for k in mp if k < bi]
        if lower:
            k0 = max(lower)
            j = mp[k0] + (bi - k0)
            return P7.fields[j], P8.fields[j], f"~{k0}->{mp[k0]}"
        return None, None, "sem match"
    return P7.fields[j], P8.fields[j], f"{bi}->{j}"


RESULT = {}
TABELA = []


def reg(codename, f7, f8, info, oldv76, oldv8a, classe="", tipo=""):
    v7 = hex(f7.offset) if f7 is not None else None
    v8 = hex(f8.offset) if f8 is not None else None
    t7 = f7.type if f7 is not None else "?"
    RESULT[codename] = {"v7a": v7, "v8a": v8, "info": info, "tipo": t7, "classe": classe}
    TABELA.append((classe, codename, oldv76, oldv8a, v7, v8, info, tipo or t7))


def hx(v):
    return hex(v) if isinstance(v, int) else str(v)


# ============================================================
# 1. PLAYER
# ============================================================
PLAYER_BRIDGE = {
    "IsClientBot": "IsClientBot",
    "IsFemale": "<DABCKMCKKOO>k__BackingField",
    "IsFiring": "<NNFKGNCILNK>k__BackingField",
    "UGCStartFiring": "UGCStartFiring",
    "m_IsCurFrameFowardLockToAimRot": "JFIGCLAAMMO",
    "m_WaitForForceSync": "OFEBBCHKOAJ",
    "m_TransformType": "JONOLCHJCJH",
    "m_AimRotation": "<MDCADLIAJIH>k__BackingField",
    "m_AuxAimRotation": "<MPNEBFFFAMP>k__BackingField",
    "m_AimAssist": "m_AimAssist",
    "m_EAimAssit": "FGEAKHHPKCC",
    "m_AimAssistOnSighting": "BJPAKMNJHKM",
    "m_LastAimingInfoFromWeapon": "AKFLHNOIHED",
    "MainCameraTransform": "MainCameraTransform",
    "m_SwapWeaponTime": "KDNABNMDIPA",
    "m_Attributes": "KDJHNBAECLM",
    "m_AvatarManager": "KPMDIPJINJO",
    "m_InventoryManager": "LPEALCPGJBL",
    "m_UserControl": "ODIGILJGPAK",
    "m_HeadCollider": "NFDNMIOPILM",
    "m_fireColliders": "CGMPIMANBNC",
    "HeadNode": "PEMOFNFCLFB",
    "m_HipNode": "DIDHPFKMJJE",
    "m_BloodEffectNode": "KAKOKIHEPCF",
    "m_RootNode": "KNFKIDHJCCO",
    "m_BoneRootNode": "HNFBCFKKCJP",
    "m_WeaponMountNode": "GOLAIKOPNJK",
    "m_LeftWeaponNode": "OEHAGFIGILO",
    "m_FlightNode": "CLOEKEADCHD",
    "m_RightArmNode": "OEJFBHIIBBG",
    "m_LeftArmNode": "NBHOEOOCIIG",
    "m_RightForeArmNode": "PNPBBNDANEM",
    "m_LeftForeArmNode": "KNBJLEHOPIL",
    "m_RightHandNode": "DIHJDDNIJHP",
    "m_LeftHandNode": "KMIANNCLNOJ",
    "m_RightAnkleNode": "BIPBNNIFCNO",
    "m_LeftAnkleNode": "BOHFCEHMJBD",
    "m_RightToeNode": "INHGPBHOKPF",
    "m_LeftToeNode": "JLLMBADGKJP",
}

CODE_V76 = {
    "IsClientBot": 0x2E4, "IsFemale": 0x7D8, "IsFiring": 0x540,
    "UGCStartFiring": 0x13D, "m_IsCurFrameFowardLockToAimRot": 0x1D4,
    "m_WaitForForceSync": 0x520, "m_TransformType": 0xC4C,
    "m_AimRotation": 0x400, "m_AuxAimRotation": 0x410, "m_AimAssist": 0x420,
    "m_EAimAssit": 0x438, "m_AimAssistOnSighting": 0x43C,
    "m_LastAimingInfoFromWeapon": 0x978, "MainCameraTransform": 0x24C,
    "m_SwapWeaponTime": 0x51C, "m_Attributes": 0x4BC, "m_AvatarManager": 0x4C0,
    "m_InventoryManager": 0x4A8, "m_UserControl": 0x304, "m_HeadCollider": 0x4A4,
    "m_fireColliders": 0x760, "HeadNode": 0x458, "m_HipNode": 0x45C,
    "m_BloodEffectNode": 0x460, "m_RootNode": 0x46C, "m_BoneRootNode": 0x470,
    "m_WeaponMountNode": 0x454, "m_LeftWeaponNode": 0x484, "m_FlightNode": 0x468,
    "m_RightArmNode": 0x490, "m_LeftArmNode": 0x48C, "m_RightForeArmNode": 0x498,
    "m_LeftForeArmNode": 0x4A0, "m_RightHandNode": 0x494, "m_LeftHandNode": 0x49C,
    "m_RightAnkleNode": 0x478, "m_LeftAnkleNode": 0x474, "m_RightToeNode": 0x480,
    "m_LeftToeNode": 0x47C,
}
CODE_V8A = {
    "IsClientBot": 0x438, "IsFemale": 0xB8C, "IsFiring": 0x7D0,
    "UGCStartFiring": 0x1F1, "m_IsCurFrameFowardLockToAimRot": 0x2D8,
    "m_WaitForForceSync": 0x798, "m_TransformType": 0x11C4,
    "m_AimRotation": 0x5AC, "m_AuxAimRotation": 0x5BC, "m_AimAssist": 0x5D0,
    "m_EAimAssit": 0x5F8, "m_AimAssistOnSighting": 0x600,
    "m_LastAimingInfoFromWeapon": 0xDC8, "MainCameraTransform": 0x380,
    "m_SwapWeaponTime": 0x794, "m_Attributes": 0x700, "m_AvatarManager": 0x708,
    "m_InventoryManager": 0x6D8, "m_UserControl": 0x468, "m_HeadCollider": 0x6D0,
    "m_fireColliders": 0xAD8, "HeadNode": 0x638, "m_HipNode": 0x640,
    "m_BloodEffectNode": 0x648, "m_RootNode": 0x660, "m_BoneRootNode": 0x668,
    "m_WeaponMountNode": 0x630, "m_LeftWeaponNode": 0x690, "m_FlightNode": 0x658,
    "m_RightArmNode": 0x6A8, "m_LeftArmNode": 0x6A0, "m_RightForeArmNode": 0x6B8,
    "m_LeftForeArmNode": 0x6C8, "m_RightHandNode": 0x6B0, "m_LeftHandNode": 0x6C0,
    "m_RightAnkleNode": 0x678, "m_LeftAnkleNode": 0x670, "m_RightToeNode": 0x688,
    "m_LeftToeNode": 0x680,
}

for code, bname in PLAYER_BRIDGE.items():
    f7, f8, info = player_aligned(bname)
    reg(code, f7, f8, info, CODE_V76.get(code), CODE_V8A.get(code), "Player")

# CORRECAO manual: m_WaitForForceSync (bool protegido) — interpolacao caiu num
# Single inserido; o bool real ficou 1 posicao depois (idx 356: @0x56c v7a / 0x80c v8a)
mw7, mw8 = P7.fields[356], P8.fields[356]
assert mw7.type == "Boolean" and mw8.type == "Boolean", "override m_WaitForForceSync invalido"
for t in TABELA:
    if t[1] == "m_WaitForForceSync":
        TABELA.remove(t)
        break
RESULT.pop("m_WaitForForceSync", None)
reg("m_WaitForForceSync", mw7, mw8, "override idx356", CODE_V76["m_WaitForForceSync"],
    CODE_V8A["m_WaitForForceSync"], "Player")


def main():
    # ============================================================
    # 2. CLASSES COM NOMES REAIS — lookup direto nos dois dumps
    # ============================================================
    NOME_DIRETO = [
        # (classe, campo, code_v76, code_v8a)
        ("GameFacade", "CurrentMatchGame", 0x4, 0x8),
        ("MatchGame", "m_Match", 0x50, 0x90),
        ("MatchGame", "m_CameraControllerManager", 0x74, 0xD8),
        ("GameVarDef", "ShootTraceAdjustmentDistanceThreshold", 0x674, 0x708),
        ("GameVarDef", "EnableAccelerationOnFalling", 0x27CA, 0x2BA6),
        ("GameVarDef", "EnableLowFallingSwapWeapon", 0x2AE5, 0x2EE1),
        ("GameVarDef", "RotationSensitivityMin", 0xF0C, 0x1068),
        ("GameVarDef", "RotationSensitivityMax", 0xF10, 0x106C),
        ("GameVarDef", "AimRotationSensitivityMin", 0xF14, 0x1070),
        ("GameVarDef", "AimRotationSensitivityMax", 0xF18, 0x1074),
        ("PlayerNetwork", "m_ShadowState", 0x18B8, 0x2278),
        ("BaseProfileInfo", "AccountID", 0x8, 0x10),
        ("BaseProfileInfo", "Level", 0x14, 0x20),
        ("BaseProfileInfo", "NickName", 0x18, 0x28),
        ("ReplicationEntity", "m_PRIDataPool", 0x48, 0x70),
        ("ReplicationDataPoolUnsafe", "m_Datas", 0x8, 0x10),
        ("ReplicationDataPoolUnsafe", "m_Int8Handlers", 0x10, 0x20),
        ("ReplicationDataPoolUnsafe", "m_UInt8Handlers", 0x14, 0x28),
        ("ReplicationDataPoolUnsafe", "m_Int32Handlers", 0x20, 0x40),
        ("ReplicationDataPoolUnsafe", "m_Int64Handlers", 0x28, 0x50),
        ("UmaAvatarSimple", "IsVisible", 0x95, 0x101),
        ("UMAAvatarBase", "umaData", 0x14, 0x28),
        ("UMAData", "skeleton", 0xCC, 0x138),
        ("UMAData", "isLocalPlayer", 0x58, 0x80),
        ("UMAData", "isTeammate", 0x59, 0x81),
        ("UMASkeleton", "boneHashDataLookup", 0x18, 0x28),
        ("BoneData", "boneNameHash", 0x8, 0x10),
        ("BoneData", "boneTransform", 0x10, 0x18),
        ("TransformNode", "<transform>k__BackingField", 0x8, 0x10),
        ("Entity", "m_CachedTransform", 0x38, 0x58),
        ("UserControlHandler", "<DashByMovingJoystick>k__BackingField", 0x58, 0x9C),
        ("Camera", "onPostRender", 0x8, 0x10),
    ]
    for cls, fn, o76, o8 in NOME_DIRETO:
        c7, c8 = resolve(V7, cls), resolve(V8, cls)
        f7 = c7.f(fn) if c7 else None
        f8 = c8.f(fn) if c8 else None
        key = f"{cls}.{fn}"
        reg(key, f7, f8, "nome direto", o76, o8, cls)

    # ============================================================
    # 3. TYPE-CHASE (campos identificados pelo TIPO)
    # ============================================================
    def chase(d, cls, ftype, n=0):
        c = resolve(d, cls)
        if not c:
            return None
        hits = [f for f in c.fields if f.type == ftype]
        return hits[n] if n < len(hits) else None

    def chase_pair(cls, ftype, n, o76, o8, key, info=""):
        reg(key, chase(V7, cls, ftype, n), chase(V8, cls, ftype, n), info or "type-chase", o76, o8, cls)

    mg7, mg8 = resolve(V7, "MatchGame"), resolve(V8, "MatchGame")
    match7 = resolve(V7, mg7.f("m_Match").type)
    match8 = resolve(V8, mg8.f("m_Match").type)
    obs7 = resolve(V7, match7.f("DGDPMNMOAFP").type)
    obs8 = resolve(V8, match8.f("DGDPMNMOAFP").type)
    chase_pair("MatchGame", mg7.f("m_Match").type, 0, 0x50, 0x90, "MatchGame.m_Match", "match class")
    # Match: m_State / m_LocalPlayer / m_LocalObserver / m_AttackableEntities
    reg("Match.m_State", match7.f("LOEPAKMFNJO"), match8.f("LOEPAKMFNJO"), "align Match", 0x8C, 0xCC, "Match")
    chase_pair(match7.name, "Player", 0, 0x94, 0xD8, "Match.m_LocalPlayer", "Player-typed")
    chase_pair(match7.name, obs7.name, 0, 0xB4, 0x100, "Match.m_LocalObserver", "observer-typed")
    # m_AttackableEntities: campo JONHEMLEHHL (List`1 @0x140 — fixado por posicao)
    reg("Match.m_AttackableEntities", match7.f("JONHEMLEHHL"), match8.f("JONHEMLEHHL"), "align Match", 0x140, 0x200, "Match")
    # AvatarManager.m_Avatar: unico campo IUmaAvatar-typed
    am7, am8 = resolve(V7, "AvatarManager"), resolve(V8, "AvatarManager")
    av7 = [f for f in am7.fields if f.type == "IUmaAvatar"]
    av8 = [f for f in am8.fields if f.type == "IUmaAvatar"]
    reg("AvatarManager.m_Avatar", av7[0] if av7 else None, av8[0] if av8 else None,
        "IUmaAvatar-typed", 0xA8, 0x138, "AvatarManager")
    # Observer.m_TargetPlayer
    chase_pair(obs7.name, "Player", 0, 0x28, 0x30, "Observer.m_TargetPlayer", "Player-typed")
    # PlayerNetwork.m_Profile
    chase_pair("PlayerNetwork", "BaseProfileInfo", 0, 0x18CC, 0x22A0, "PlayerNetwork.m_Profile", "BaseProfileInfo-typed")
    # CameraControllerManager.m_Camera (Camera-typed)
    chase_pair("CameraControllerManager", "Camera", 0, 0x10, 0x20, "CameraControllerManager.m_Camera", "Camera-typed")

    # ============================================================
    # 4. CLASSES OFUSCADAS VIA BRIDGE
    # ============================================================
    pn7, pn8 = resolve(V7, "PlayerNetwork"), resolve(V8, "PlayerNetwork")
    ss7 = resolve(V7, pn7.f("m_ShadowState").type)
    ss8 = resolve(V8, pn8.f("m_ShadowState").type)
    ssb = resolve(B, "PlayerNetwork")
    ssb_cls = B.cls.get(ssb.f("m_ShadowState").type)
    f7, f8, info = via_bridge(ssb_cls, "BGDKLEHDFJO", ss7, ss8)
    reg("ShadowState.TargetPhysXPose", f7, f8, info, 0x78, 0x80, "ShadowState")

    # PlayerAttributes
    pab, pa7, pa8 = resolve(B, "PlayerAttributes"), resolve(V7, "PlayerAttributes"), resolve(V8, "PlayerAttributes")
    for bname, code, o76, o8 in [("PNLLLKKNBOG", "m_EatSpeedScale", 0x60, 0x9C),
                                 ("BONAJJEOPNF", "m_FireIntervalScale", 0x18C, 0x208)]:
        f7, f8, info = via_bridge(pab, bname, pa7, pa8)
        reg(f"PlayerAttributes.{code}", f7, f8, info, o76, o8, "PlayerAttributes")

    # UserControlHandler (classe real nos 3 dumps)
    ucb, uc7, uc8 = resolve(B, "UserControlHandler"), resolve(V7, "UserControlHandler"), resolve(V8, "UserControlHandler")
    mpUC = align_map(ucb, uc7)
    for bname, code, o76, o8 in [("m_AxisData", "m_AxisData", 0x34, 0x68),
                                 ("m_FingerInDashArea", "m_FingerInDashArea", 0x4C, 0x90),
                                 ("m_LockFingerInDashArea", "m_LockFingerInDashArea", 0x50, 0x94)]:
        bf = ucb.f(bname)
        if bf is None:
            reg(f"UserControlHandler.{code}", None, None, "bridge N/A", o76, o8, "UserControlHandler")
            continue
        j = mpUC.get(ucb.fields.index(bf))
        if j is None:
            reg(f"UserControlHandler.{code}", None, None, "sem match", o76, o8, "UserControlHandler")
            continue
        reg(f"UserControlHandler.{code}", uc7.fields[j], uc8.fields[j], f"align {j}", o76, o8, "UserControlHandler")

    # UserControlAxisData.m_IsTouched
    uadb = next(c for c in B.order if not c.dup and c.name == "UserControlAxisData")
    uad7 = next(c for c in V7.order if not c.dup and c.name == "UserControlAxisData")
    uad8 = next(c for c in V8.order if not c.dup and c.name == "UserControlAxisData")
    f7, f8, info = via_bridge(uadb, "m_IsTouched", uad7, uad8)
    reg("UserControlAxisData.m_IsTouched", f7, f8, info, 0x37, 0x4B, "UserControlAxisData")

    # AimAssistOnSighting (bridge EMFEPBOHGOJ -> novo DCIDAEHLHPI)
    aab = B.cls.get("EMFEPBOHGOJ")
    aas7 = resolve(V7, "DCIDAEHLHPI")
    aas8 = resolve(V8, "DCIDAEHLHPI")
    f7, f8, info = via_bridge(aab, "NAKKFIIIGNO", aas7, aas8)
    reg("AimAssistOnSighting.m_fAimAssistCurrentLerpTime", f7, f8, info, 0x44, 0x80, "AimAssistOnSighting")

    # HitObjectInfo (bridge GMPGMPFNMFP -> novo CGKJLKPMGDJ, mesma estrutura)
    hib = B.cls.get("GMPGMPFNMFP")
    hi7 = resolve(V7, "CGKJLKPMGDJ")
    hi8 = resolve(V8, "CGKJLKPMGDJ")
    for bn, code, o76, o8 in [("IKDEGKIICJP", "RayDir", 0x2C, 0x40), ("LMAEGPEAECO", "StartPosition", 0x38, 0x4C)]:
        f7, f8, info = via_bridge(hib, bn, hi7, hi8)
        reg(f"HitObjectInfo.{code}", f7, f8, info, o76, o8, "HitObjectInfo")

    # AimAssistAutoLock (bridge KBCJOEFJEFJ -> novo: subclasse de NLIMDENAFNN)
    alb = B.cls.get("KBCJOEFJEFJ")
    als7 = [c for c in V7.order if c.bases and "NLIMDENAFNN" in c.bases]
    als8 = [c for c in V8.order if c.bases and "NLIMDENAFNN" in c.bases]
    al7 = als7[0] if als7 else None
    al8 = V8.cls.get(al7.name) if al7 else None
    f7, f8, info = via_bridge(alb, "KOLIMPJEBPC", al7, al8)
    reg("AimAssistAutoLock.m_TargetHeuristic", f7, f8, info, 0xC, 0x10, "AimAssistAutoLock")
    # TargetInfo.m_Entity: nested class
    tib = B.cls.get("KLNCOMCJJGK")
    if f7 is not None:
        ti7 = resolve(V7, f7.type)
        ti8 = resolve(V8, f8.type)
        f7e, f8e, infoe = via_bridge(tib, "LDNBCNLCIGP", ti7, ti8)
        reg("AimAssistAutoLock.m_Entity", f7e, f8e, infoe, 0xC, 0x18, "AimAssistAutoLock.TargetInfo")
    else:
        reg("AimAssistAutoLock.m_Entity", None, None, "dep falhou", 0xC, 0x18, "AimAssistAutoLock.TargetInfo")

    # InventoryManager (bridge OMELKCOGCBK -> novo GMOCOOEIFMK)
    imb = B.cls.get("OMELKCOGCBK")
    im7 = resolve(V7, "GMOCOOEIFMK")
    im8 = resolve(V8, "GMOCOOEIFMK")
    f7, f8, info = via_bridge(imb, "CHAFOMFBKEG", im7, im8)
    reg("InventoryManager.m_itemOnHand", f7, f8, info, 0x54, 0xA0, "InventoryManager")

    # Weapon (bridge FDAEPHMIEPC -> novo HBIBDMMOOOK)
    wb, w7, w8 = B.cls.get("FDAEPHMIEPC"), resolve(V7, "HBIBDMMOOOK"), resolve(V8, "HBIBDMMOOOK")
    mpW = align_map(wb, w7)
    for bn, code, o76, o8 in [("<FLCGCBLDMLK>k__BackingField", "FireComponent", 0x58, 0x80),
                              ("DJMMOHAJFPB", "m_WeaponData", 0x64, 0x98),
                              ("JJMOJGHDLIF", "m_WeaponParams", 0x6C, 0xA8),
                              ("NAGGOHBGHKK", "m_FireDuration", 0x4BC, 0x5F0),
                              ("PBJJMLOOLGH", "m_IsSighting", 0x5E4, 0x7C0)]:
        bf = wb.f(bn)
        j = mpW.get(wb.fields.index(bf)) if bf else None
        if j is None:
            reg(f"Weapon.{code}", None, None, "sem match", o76, o8, "Weapon")
            continue
        reg(f"Weapon.{code}", w7.fields[j], w8.fields[j], f"align {j}", o76, o8, "Weapon")

    # tangentTheta: bridge BNFFFLEJGMA (1 campo Single @0xC) -> novo MBJCPLOAJJA
    tc7 = resolve(V7, "MBJCPLOAJJA")
    tc8 = resolve(V8, "MBJCPLOAJJA")
    tnb = B.cls.get("BNFFFLEJGMA")
    f7, f8, info = via_bridge(tnb, "HKNJLOBGIDP", tc7, tc8)
    reg("WeaponFireComponent.tangentTheta", f7, f8, info, 0xC, 0x18, "WeaponFireComponent")

    # IntWeaponType: WeaponData bridge tipo de DJMMOHAJFPB -> novo IOKEALMHBKN
    wd_b = B.cls.get(wb.f("DJMMOHAJFPB").type) if wb.f("DJMMOHAJFPB") else None
    wd7 = resolve(V7, "IOKEALMHBKN")
    wd8 = resolve(V8, "IOKEALMHBKN")
    f7, f8, info = via_bridge(wd_b, "EJKCLOFONLG", wd7, wd8)
    reg("WeaponData.IntWeaponType", f7, f8, info, 0xB8, 0xC8, "WeaponData")

    # WeaponParams: bridge tipo de JJMOJGHDLIF -> novo LBNIHCJFPGI
    wp_b = B.cls.get(wb.f("JJMOJGHDLIF").type) if wb.f("JJMOJGHDLIF") else None
    wp7 = resolve(V7, "LBNIHCJFPGI")
    wp8 = resolve(V8, "LBNIHCJFPGI")
    for bn, code, o76, o8 in [("LDMJIJODACB", "FullDamageDistance", 0x48, 0x50),
                              ("HCPMOLBCKIM", "PrefireDelay", 0x144, 0x150),
                              ("JOLEGACBIKJ", "Range", 0x44, 0x4C)]:
        f7, f8, info = via_bridge(wp_b, bn, wp7, wp8)
        reg(f"WeaponParams.{code}", f7, f8, info, o76, o8, "WeaponParams")

    # Camera.m_CachedPtr: sem instancia no dump; mantem ancorado no onPostRender
    cam7, cam8 = resolve(V7, "Camera"), resolve(V8, "Camera")
    f7 = cam7.f("onPostRender") if cam7 else None
    f8 = cam8.f("onPostRender") if cam8 else None
    reg("Camera.m_CachedPtr", f7, f8, "onPostRender anchor", 0x8, 0x10, "Camera")

    # ============================================================
    # Impressao
    # ============================================================
    for t in TABELA:
        cls, code, o76, o8, v7, v8, info, tipo = t
        chg = ""
        if v7 and int(v7, 16) != o76:
            chg += " V76!"
        if v8 and int(v8, 16) != o8:
            chg += " V8A!"
        print(f"{cls:24s} {code:42s} v76={hx(o76):>7s} v8a={hx(o8):>7s} -> v7a={str(v7):>7s} v8a={str(v8):>7s} [{info}] {chg if chg else 'ok'}")
    out_json = os.path.join(os.path.dirname(os.path.abspath(__file__)), "offsets_result.json")
    with open(out_json, "w") as f:
        json.dump(RESULT, f, indent=1)
    n_fail = sum(1 for r in RESULT.values() if r["v7a"] is None or r["v8a"] is None)
    print(f"\n{len(RESULT)} campos ({n_fail} falhas) -> offsets_result.json")


if __name__ == "__main__":
    main()
