class Offsets {
public:
    static inline uintptr_t Il2Cpp = 0x0; //OK FIXO PONTO
    static inline uintptr_t InitBase = 0xA986E9C; //OK
    static inline uintptr_t StaticClass = 0x5C; //OK
    static inline uintptr_t CurrentMatch = 0x50; //OK
    static inline uintptr_t MatchStatus = 0x8c; //OK
    static inline uintptr_t LocalPlayer = 0x94;  //OK
    static inline uintptr_t DictionaryEntities = 0x68;  //OK
    static inline uintptr_t Player_IsDead = 0x50; // OU 
    static inline uintptr_t Player_Name = 0x2DC; //OK
    static inline uintptr_t Player_Data = 0x48; //OK
    static inline uintptr_t Player_ShadowBase = 0x18B8; //OK
    static inline uintptr_t XPose = 0x78; //OK
    static inline uintptr_t AvatarManager = 0x4C0;  //OK
    static inline uintptr_t Avatar = 0xA8; //OK
    static inline uintptr_t Avatar_IsVisible = 0x95;  //OK
    static inline uintptr_t Avatar_Data = 0x14; //OK
    static inline uintptr_t Avatar_Data_IsTeam = 0x59; //OK
    static inline uintptr_t FollowCamera = 0x450; //OK
    static inline uintptr_t Camera = 0x18;  //OK
    static inline uintptr_t AimRotation = 0x400; //OK
    static inline uintptr_t MainCameraTransform = 0x24C; //OK
    static inline uintptr_t ViewMatrix = 0xE8; //OK
    static inline uintptr_t Vida = 0x10; //CALCULO VIDA

    // ===== CLASSE DE OSSOS MELHORADA =====
    // Agora contém TODOS os ossos necessários para desenho correto do esqueleto
    // Isso corrige o problema de inimigos não sendo detectados na espline
    class Bones {
    public:
        // Ossos principais
        static inline uintptr_t Head = 0x458;              // Cabea
        static inline uintptr_t Neck = 0x460;             // Pescoo
        static inline uintptr_t Spine = 0x464;            // Coluna
        static inline uintptr_t Hip = 0x45C;              // Quadril
        static inline uintptr_t Root = 0x46C;            // Raiz do corpo

        // Ombros
        static inline uintptr_t LeftShoulder = 0x48C;     // Ombro esquerdo
        static inline uintptr_t RightShoulder = 0x490;    // Ombro direito

        // Cotovelos
        static inline uintptr_t LeftElbow = 0x4A0;        // Cotovelo esquerdo
        static inline uintptr_t RightElbow = 0x49C;       // Cotovelo direito

        // Pulsos / Mos
        static inline uintptr_t LeftWrist = 0x498;        // Pulso esquerdo
        static inline uintptr_t RightWrist = 0x484;      // Pulso direito
        static inline uintptr_t LeftHand = 0x498;        // Mo esquerda (alias)
        static inline uintptr_t RightHand = 0x494;       // Mo direita

        // Pernas / Tornozelos
        static inline uintptr_t LeftAnkle = 0x474;        // Tornozelo esquerdo
        static inline uintptr_t RightAnkle = 0x478;      // Tornozelo direito
        static inline uintptr_t LeftCalf = 0x474;        // Panturrilha esquerda (alias)
        static inline uintptr_t RightCalf = 0x478;       // Panturrilha direita (alias)
        static inline uintptr_t LeftFoot = 0x47C;        // P esquerdo
        static inline uintptr_t RightFoot = 0x480;       // P direito

        // Aliases adicionais
        static inline uintptr_t Pelvis = 0x45C;          // Pelve (alias do Hip)
        static inline uintptr_t HipCenter = 0x45C;       // Centro do quadril
    };

    // ===== OBSERVER =====
    static inline uintptr_t CurrentObserver = 0xB4;
    static inline uintptr_t ObserverPlayer = 0x28;

    // ===== WEAPON =====
    static inline uintptr_t Weapon = 0x3F4;
    static inline uintptr_t WeaponData = 0x58;
    static inline uintptr_t WeaponRecoil = 0xC;      // recuo da arma (zerar = no recoil real)
    static inline uintptr_t WeaponOnHand = 0x54;
    static inline uintptr_t FireComponent = 0x58;
    static inline uintptr_t RayDir = 0x2C;
    static inline uintptr_t IsFiring = 0x540;
    static inline uintptr_t NoRecoilFlag = 0x548;    // flag de no recoil do Krishu
    static inline uintptr_t StartPosition = 0x38;
    static inline uintptr_t Weapon_IsSighting = 0x5CC;
    static inline uintptr_t Weapon_LastPullBoltTime = 0x440;
    static inline uintptr_t Player_SecondaryWeapon = 0x3F8;
    static inline uintptr_t Weapon_ItemId = 0x494;

    // ===== AIMBOT VISIBLE =====
    static inline uintptr_t LockedAimingCollider = 0x54;
    static inline uintptr_t Collider = 0x4A4;
    static inline uintptr_t HeadCollider = 0x4A4;

    // ===== SILENT BRUTAL CONVERTIDO =====
    static inline uintptr_t BrutalSilentWeaponInfo = 0x978;
    static inline uintptr_t BrutalSilentGunTipPosition = 0x38;
    static inline uintptr_t BrutalSilentBulletHit = 0x2C;

    // ===== FODA V1 (convertido do C#) =====
    static inline uintptr_t WeaponDataFoda = 0x64;          // weapon + 0x64 (C# Offsets.WeaponData)
    static inline uintptr_t PlayerAttributesFoda = 0x4BC;   // localPlayer + 0x4BC (C# Offsets.PlayerAttributes)
    static inline uintptr_t NoReloadFoda = 0x99;            // playerAttributes + 0x99 (C# Offsets.NoReload)

    // ===== MISC =====
    static inline uintptr_t LastAimingInfoFromWeapon = 0x978;
    static inline uintptr_t IsClientBot = 0x2E4;
    static inline uintptr_t Pool_Health = 0x10;

    // ===== EXPLOITS COMPATIBILITY (mantidas para exploits existentes) =====
    static inline uintptr_t PlayerAttributes = 0x404;
    static inline uintptr_t GameTimer = 0xb0;
    static inline uintptr_t NoReload2 = 0x89;
    static inline uintptr_t RightcameraOffset = 0x38;
    static inline uintptr_t Phase1CameraEulerAnglesY = 0x48;
    static inline uintptr_t ColliderHECFNHJKOMN = 0x4A4;
    static inline uintptr_t ColliderINICDNFOFJB = 0x54;

    // ===== SPEED TIMER (currentGame -> timer object -> FixedDeltaTime) =====
    static inline uintptr_t GameFacadeTimer = 0x10;
    static inline uintptr_t TimerFixedDeltaTime = 0x24;

    // ===== NO GRAVITY FLY =====
    static inline uintptr_t MovementComponent = 0x139C;
    static inline uintptr_t Position = 0x20;
    static inline uintptr_t VSpeed = 0x2C;
    static inline uintptr_t IsGrounded = 0x13F0;

    // ===== VISION HACK =====
    static inline uintptr_t FollowCameraFOVOffset = 0x48;

    // ===== SEMI TELA PARADA (UserControlHandler) =====
    static inline uintptr_t UserControl = 0x304;              // Player -> protected UserControlHandler m_UserControl
    static inline uintptr_t AxisData = 0x34;                  // private UserControlAxisData[] m_AxisData
    static inline uintptr_t FingerInDashArea = 0x4C;          // private int m_FingerInDashArea
    static inline uintptr_t IsTouched = 0x37;                 // class UserControlAxisData -> bool m_IsTouched
    static inline uintptr_t LockFingerInDashArea = 0x50;      // private bool m_LockFingerInDashArea
    static inline uintptr_t DashByMovingJoystick = 0x58;      // private bool <DashByMovingJoystick>k__BackingField

    // ===== BACK JUMP (GameVarDef) - auto-detected at runtime, see BackJump.cpp =====
    static inline uintptr_t EnableAccelerationOnFalling = 0x27CA;   // GameVarDef -> bool EnableAccelerationOnFalling
    static inline uintptr_t EnableLowFallingSwapWeapon = 0x2AE5;    // GameVarDef -> bool EnableLowFallingSwapWeapon
};
