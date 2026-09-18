#!/usr/bin/env python3
"""v8a audit via line-mirrored dumps: v7a and v8a dumps share identical line
structure, so grep field names within known class line-ranges of dump_v8a.cs
and compare offsets against Offsets::FFTHV8A."""
import re, os, subprocess

D8 = os.environ.get('ZIMO_REPO', '/home/z/my-project/repo-zimo') + '/dump_v8a.cs'

# (symbol, class_range_start, class_range_end, field_regex, expected)
CHECKS = [
 ("GameFacade::CurrentMatchGame",        294027, 294090, r"MatchGame CurrentMatchGame", 0x8),
 ("MatchGame::m_Match",                  295601, 295700, r"EMKJHAJNPDH m_Match",          0x90),
 ("MatchGame::m_CameraControllerManager",295601, 295700, r"CameraControllerManager m_CameraControllerManager", 0xD8),
 ("Match::m_State",                      1016731,1016900,r"NJMHKHCLBNN MAOHIOEAMEA",      0xCC),
 ("Match::m_LocalPlayer",                1016731,1016900,r"Player PDBGEOANOEP",           0xD8),
 ("Match::m_LocalObserver",              1016731,1016900,r"PHLHIEGPMMK MPMAGJDHNBI",      0x100),
 ("Match::m_AttackableEntities",         1016731,1016900,r"List`1 HCLFEIEFKHP",           0x200),
 ("CameraControllerManager::m_Camera",   1011986,1011995,r"Camera BAGLCCLIOEK",           0x20),
 ("Observer::m_TargetPlayer",            1110421,1110520,r"Player JAGIFDNJJFD",           0x30),
 ("Player::IsClientBot",                 1001788,1011800,r"Boolean IsClientBot",          0x438),
 ("Player::IsFemale <DABCKMCKKOO>",      1001788,1011800,r"DABCKMCKKOO>k__BackingField",  0xB8C),
 ("Player::IsFiring <NNFKGNCILNK>",      1001788,1011800,r"NNFKGNCILNK>k__BackingField",  0x7D0),
 ("Player::m_Attributes",                1001788,1011800,r"PlayerAttributes KDJHNBAECLM", 0x700),
 ("Player::m_AvatarManager",             1001788,1011800,r"AvatarManager KPMDIPJINJO",    0x708),
 ("Player::m_InventoryManager",          1001788,1011800,r"OMELKCOGCBK LPEALCPGJBL",      0x6D8),
 ("Player::m_HeadCollider",              1001788,1011800,r"Collider NFDNMIOPILM",         0x6D0),
 ("Player::m_fireColliders",             1001788,1011800,r"List`1 CGMPIMANBNC",           0xAD8),
 ("Player::HeadNode",                    1001788,1011800,r"ITransformNode PEMOFNFCLFB",   0x638),
 ("Player::m_RootNode",                  1001788,1011800,r"ITransformNode KNFKIDHJCCO",   0x660),
 ("Player::m_BoneRootNode",              1001788,1011800,r"ITransformNode HNFBCFKKCJP",   0x668),
 ("Player::m_WeaponMountNode",           1001788,1011800,r"ITransformNode GOLAIKOPNJK",   0x630),
 ("Player::m_HipNode",                   1001788,1011800,r"ITransformNode DIDHPFKMJJE",   0x640),
 ("Player::m_FlightNode",                1001788,1011800,r"ITransformNode CLOEKEADCHD",   0x658),
 ("Player::m_UserControl",               1001788,1011800,r"UserControlHandler LAHBMONIOOI",0x304),
 ("PlayerNetwork::m_ShadowState",        1023884,1024700,r"LJBAALIHDEE m_ShadowState",    0x2278),
 ("PlayerNetwork::m_Profile",            1023884,1024700,r"BaseProfileInfo KAKEEBABBIP",  0x22A0),
 ("ShadowState::TargetPhysXPose",        1023763,1023790,r"EOGPGNIDOKF BGDKLEHDFJO",      0x80),
 ("AvatarManager::m_Avatar",             995004, 995140, r"IUmaAvatar GIAMMAADHFN",       0x138),
 ("UMAAvatarBase::umaData",              30552,  30620,  r"UMAData umaData",              0x28),
 ("UmaAvatarSimple::IsVisible",          33354,  33500,  r"Boolean IsVisible",            0x101),
 ("UMAData::skeleton",                   31006,  31120,  r"UMASkeleton skeleton",         0x138),
 ("UMAData::isLocalPlayer",              31006,  31120,  r"Boolean isLocalPlayer",        0x80),
 ("UMAData::isTeammate",                 31006,  31120,  r"Boolean isTeammate",           0x81),
 ("ReplicationEntity::m_PRIDataPool",    1178779,1178880,r"IPRIDataPool m_PRIDataPool",   0x70),
 ("Entity::m_CachedTransform",           1178526,1178600,r"Transform m_CachedTransform",  0x58),
 ("TransformNode::<transform>",          1178462,1178470,r"Transform <transform>",        0x10),
 ("ReplicationDataPoolUnsafe::m_Datas",  1190085,1190110,r"ReplicationDataUnsafe\[\] m_Datas", 0x10),
 ("ReplicationDataUnsafe::Value(ptr)",   1190070,1190076,r"Void\* Value",                 0x10),
 ("ReplicationData(safe)::Value(union)", 1189752,1189758,r"ReplicationDataValueUnion Value", 0x18),
 ("BaseProfileInfo::AccountID",          706255, 706330, r"UInt64 AccountID",             0x10),
 ("BaseProfileInfo::Level",              706255, 706330, r"UInt32 Level",                 0x20),
 ("BaseProfileInfo::NickName",           706255, 706330, r"String NickName",              0x28),
]

LINE = re.compile(r'; // (0x[0-9a-f]+)\s*$')

with open(D8, encoding='utf-8', errors='replace') as f:
    lines = f.readlines()

ok = bad = miss = 0
print(f"{'symbol':40s} {'code':>7s} {'dump':>7s}  verdict")
print("─" * 74)
for sym, a, b, pat, exp in CHECKS:
    found = None
    for i in range(a - 1, min(b, len(lines))):
        if re.search(pat, lines[i]):
            m = LINE.search(lines[i])
            if m:
                found = int(m.group(1), 16)
                break
    if found is None:
        miss += 1
        print(f"{sym:40s} {exp:#07x} {'—':>7s}  NOT FOUND")
    elif found == exp:
        ok += 1
        print(f"{sym:40s} {exp:#07x} {found:#07x}  OK")
    else:
        bad += 1
        print(f"{sym:40s} {exp:#07x} {found:#07x}  ✗ MISMATCH")
print("─" * 74)
print(f"OK={ok}  MISMATCH={bad}  NOTFOUND={miss}")
