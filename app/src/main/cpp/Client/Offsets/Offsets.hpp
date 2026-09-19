#pragma once
#include <cstdint>
#include <vector>
#include <type_traits>

class Offsets
{
public:
        static uintptr_t LibIl2Cpp;
        static std::vector<uintptr_t> LibIl2CppCandidates;
        static uintptr_t AccessClass;

        // ==================== Match State ====================

        enum class MatchState : int
        {
                Lobby = -1,
                NotStarted = 0,
                Running = 1,
                WaitingForEnd = 2,
                EatingChickenDelayEnd = 3,
                MatchEnd = 4,
                Unknown = 5
        };

        static bool IsMatchActive(MatchState s);

        // ==================== Game Flow ====================

        class GameVarDef
        {
        public:
                static uintptr_t GameVarDef_TypeInfo;
                static uintptr_t ShootTraceAdjustmentDistanceThreshold; // ShootTraceAdjustmentDistanceThreshold
                static uintptr_t EnableAccelerationOnFalling; // EnableAccelerationOnFalling
                static uintptr_t EnableLowFallingSwapWeapon; // EnableLowFallingSwapWeapon
                static uintptr_t RotationSensitivityMin; // RotationSensitivityMin
                static uintptr_t RotationSensitivityMax; // RotationSensitivityMax
                static uintptr_t AimRotationSensitivityMin; // AimRotationSensitivityMin
                static uintptr_t AimRotationSensitivityMax; // AimRotationSensitivityMax
        };

        class GameFacade
        {
        public:
                static uintptr_t GameFacade_TypeInfo;
                static uintptr_t CurrentMatchGame; // public static MatchGame CurrentMatchGame; // 0x4
        };

        /*
         * (V9) BaseGame = GameFacade.CurrentGame (statics[0]) — porta para
         * o TimeService (Speed Lite) e para a UIBaseScene (Teleport Mark).
         */
        class BaseGame
        {
        public:
                static uintptr_t m_UIScene;     // protected UIBaseScene m_UIScene;
                static uintptr_t m_GameTimer;   // protected TimeService m_GameTimer;
        };

        class TimeService
        {
        public:
                static uintptr_t m_FixedDeltaTime; // private float m_FixedDeltaTime;
        };

        class MatchGame
        {
        public:
                static uintptr_t m_Match; // protected Match m_Match; - protected NFJPHMKKEBF m_Match;
                static uintptr_t m_CameraControllerManager; // protected CameraControllerManager m_CameraControllerManager;
        };

        class Match
        {
        public:
                static uintptr_t m_State; // protected Match.MatchState m_State; - protected NFJPHMKKEBF.LICPHHNNPPF ILGECLEFCCO;
                static uintptr_t m_LocalPlayer; // protected Player m_LocalPlayer; - protected Player FJPEHEGICBO;
                static uintptr_t m_LocalObserver; // protected Observer m_LocalObserver; - protected FNCMBMMKLLI BGGJJKKKFDC;
                static uintptr_t m_AttackableEntities; // protected List<IAttackableEntity> m_AttackableEntities; - protected List<OPILIBBOEAC> OKKANALNMMB;
        };

        /*
         * (V9) Cadeia do TELEPORT MARK (marca do mapa):
         * BaseGame.m_UIScene (UIInGameScene) -> m_BigMapCtrl ->
         * m_MapContentCtrl -> m_LocalMapMarkController -> m_pos (Vector3).
         */
        class UIInGameScene
        {
        public:
                static uintptr_t m_BigMapCtrl;
        };

        class UIBigMapController
        {
        public:
                static uintptr_t m_MapContentCtrl; // herdado de UIMapBaseController
        };

        class UIMapContentController
        {
        public:
                static uintptr_t m_LocalMapMarkController;
        };

        class UIHudPlayerMarkController
        {
        public:
                static uintptr_t m_pos; // private Vector3 m_pos;
        };

        // ==================== Camera ====================

        /*
         * (V9) FollowCamera do Player (Vision Hack / FOV):
         * Player.m_FollowCamera -> followCam + FOVOffset (float).
         */
        class FollowCamera
        {
        public:
                static uintptr_t FOVOffset; // public float FOVOffset;
        };

        class CameraControllerManager
        {
        public:
                static uintptr_t m_Camera; // private Camera m_Camera; - private Camera PEAACFHPIFG;
        };

        class Camera : public CameraControllerManager
        {
        public:
                static uintptr_t m_CachedPtr; // private IntPtr m_CachedPtr;
                static uintptr_t ViewMatrix;
        };

        // ==================== Observer / Spectator ====================

        class Observer
        {
        public:
                static uintptr_t m_TargetPlayer; // private Player m_TargetPlayer; - private Player NJMDHHGDNPJ;
        };

        // ==================== Player ====================

        class Player
        {
        public:
                // General
                static uintptr_t IsClientBot; // public bool IsClientBot;
                static uintptr_t IsFemale; // private bool<IsFemale>k__BackingField; - private bool <CDOBMFNCJHD>k__BackingField;
                static uintptr_t IsFiring; // private Boolean <NNFKGNCILNK>k__BackingField; // abaixo de TBTAction
                static uintptr_t UGCStartFiring; // public Boolean UGCStartFiring; 
                static uintptr_t IsPrepareAttack; // private bool <IsPrepareAttack>k__BackingField; - private bool <LPEIEILIKGC>k__BackingField;
                static uintptr_t m_IsCurFrameFowardLockToAimRot; // protected bool m_IsCurFrameFowardLockToAimRot; - protected bool JEPFNELBGID;
                static uintptr_t m_WaitForForceSync; // protected bool m_WaitForForceSync; - protected bool LJHKFOOOPBF;
                static uintptr_t m_TransformType; // private Player.TransformType m_TransformType; - private Player.EPDJJKGINPC EFCMKJAKCLM

                // Aim
                static uintptr_t m_AimRotation; // private Quaternion <m_AimRotation>k__BackingField; - private Quaternion <KCFEHMAIINO>k__BackingField;
                static uintptr_t m_AuxAimRotation; // private Quaternion <m_AuxAimRotation>k__BackingField; - private Quaternion <DJEKDGNFJJG>k__BackingField;
                static uintptr_t m_AimAssist; // public IAimAssist m_AimAssist; - public GINHBFJPFBP m_AimAssist;
                static uintptr_t m_EAimAssit; // private EAimAssist m_EAimAssit; - private EAimAssist GNBLLOPNPNG;
                static uintptr_t m_AimAssistOnSighting; // private AimAssistOnSighting m_AimAssistOnSighting; - private FFNBBHKEDAE CPOKMKOBMGM;
                static uintptr_t m_LastAimingInfoFromWeapon; // private HitObjectInfo m_LastAimingInfoFromWeapon; - private MADMMIICBNN GEGFCFDGGGP;

                // Transform / Camera
                static uintptr_t MainCameraTransform; // public Transform MainCameraTransform;
                static uintptr_t m_SwapWeaponTime; // private float m_SwapWeaponTime; - private float MNDBDFDOLNL;
                static uintptr_t m_FollowCamera; // public Transform MainCameraTransform;

                // Managers
                static uintptr_t m_Attributes; // protected PlayerAttributes m_Attributes; - protected PlayerAttributes JKPFFNEMJIF;
                static uintptr_t m_AvatarManager; // protected AvatarManager m_AvatarManager; - protected AvatarManager FOGJNGDMJKJ;
                static uintptr_t m_InventoryManager; // protected InventoryManager m_InventoryManager; - protected NPCNMJAGIKI COLEAPKGFLK;

                // UserControlHandler
                static uintptr_t m_UserControl; // protected UserControlHandler m_UserControl; - protected UserControlHandler LAHBMONIOOI;

                // Colliders
                static uintptr_t m_HeadCollider; // protected Collider m_HeadCollider; - protected Collider HECFNHJKOMN;
                static uintptr_t m_fireColliders; // private List<CapsuleCollider> m_fireColliders; - private List<CapsuleCollider> AIDDOCAPFKA;

                // Bone Nodes (ITransformNode)
                static uintptr_t HeadNode; // protected ITransformNode OLCJOGDHJJJ; - HeadNode
                static uintptr_t m_HipNode; // protected ITransformNode OLJBCONDGLO; - m_HipNode
                static uintptr_t m_BloodEffectNode; // protected ITransformNode HCLMADAFLPD; - m_BloodEffectNode
                static uintptr_t m_RootNode; // protected ITransformNode MPJBGDJJJMJ; - m_RootNode
                static uintptr_t m_BoneRootNode; // protected ITransformNode JPBJIMCDBHN; - m_BoneRootNode
                static uintptr_t m_WeaponMountNode; // protected ITransformNode GCMICMFEAKI; - m_WeaponMountNode
                static uintptr_t m_LeftWeaponNode; // protected ITransformNode KOCDBPLKMBI; - m_LeftWeaponNode
                static uintptr_t m_FlightNode; // protected ITransformNode CENAIGAFGAG; - m_FlightNode
                static uintptr_t m_RightArmNode; // protected ITransformNode HDEPJIBNIIK; - m_RightArmNode
                static uintptr_t m_LeftArmNode; // protected ITransformNode LIBEIIIAGIK; - m_LeftArmNode
                static uintptr_t m_RightForeArmNode; // protected ITransformNode JBACCHNMGNJ; - m_RightForeArmNode
                static uintptr_t m_LeftForeArmNode; // protected ITransformNode FGECMMJKFNC; - m_LeftForeArmNode
                static uintptr_t m_RightHandNode; // protected ITransformNode NJDDAPKPILB; - m_RightHandNode
                static uintptr_t m_LeftHandNode; // protected ITransformNode JHIBMHEMJOL; - m_LeftHandNode
                static uintptr_t m_RightAnkleNode; // protected ITransformNode AGHJLIMNPJA; - m_RightAnkleNode
                static uintptr_t m_LeftAnkleNode; // protected ITransformNode BMGCHFGEDDA; - m_LeftAnkleNode
                static uintptr_t m_RightToeNode; // protected ITransformNode CKABHDJDMAP; - m_RightToeNode
                static uintptr_t m_LeftToeNode; // protected ITransformNode FDMBKCKMODA; - m_LeftToeNode

                // UI HUd COntroller
                static uintptr_t m_IsFiring; //private Boolean m_IsFiring; 
        };

        class PlayerNetwork : public Player
        {
        public:
                static uintptr_t m_ShadowState; // public PlayerNetwork.ShadowState m_ShadowState; - public PlayerNetwork.HHCBNAPCKHF m_ShadowState;
                static uintptr_t m_Profile; // protected BaseProfileInfo m_Profile; // protected BaseProfileInfo OJAFLKJINPJ;
        };

        class PlayerAttributes : public Player
        {
        public:
                static uintptr_t m_EatSpeedScale; // private float m_EatSpeedScale; - private float EBDDCIOAHNN;
                static uintptr_t m_FireIntervalScale; // 0x184

                // (V9) BR MOD ports
                static uintptr_t ReloadNoConsumeAmmoclip; // public bool ReloadNoConsumeAmmoclip;
                static uintptr_t ShootNoReload;           // public bool ShootNoReload;
                static uintptr_t RunSpeedUpScale;         // public float RunSpeedUpScale;
                static uintptr_t FallingSpeedUpScale;     // public float FallingSpeedUpScale;
        };

        class AimAssistAutoLock : public Player // class CNIKONPMDHF : GINHBFJPFBP
        {
        public:
                static uintptr_t m_TargetHeuristic; // AimAssistAutoLock.TargetInfo m_TargetHeuristic; - protected CNIKONPMDHF.DBMHPOCGCCO EPAIGEIOGLP;
                static uintptr_t m_Entity; // public IAttackableEntity m_Entity; - public OPILIBBOEAC KGHCKHAIPIG; class AimAssistAutoLock.TargetInfo : ObjectPoolCallbackBase -> public class CNIKONPMDHF.DBMHPOCGCCO : ObjectPoolCallbackBase
        };

        class UserControlHandler : public Player
        {
        public:
                static uintptr_t m_AxisData; // private UserControlAxisData[] m_AxisData;
                static uintptr_t m_FingerInDashArea; // private int m_FingerInDashArea;
                static uintptr_t m_IsTouched; // private bool m_IsTouched; - class UserControlAxisData
                static uintptr_t m_LockFingerInDashArea; // private bool m_LockFingerInDashArea;
                static uintptr_t m_DashByMovingJoystick; // private bool <DashByMovingJoystick>k__BackingField;
        };

        class AimAssistOnSighting : public Player // class FFNBBHKEDAE
        {
        public:
                static uintptr_t m_fAimAssistCurrentLerpTime; // private float m_fAimAssistCurrentLerpTime; - private float DLPBFMOFDNH;
        };

        class HitObjectInfo : public Player // class MADMMIICBNN
        {
        public:
                static uintptr_t RayDir; // public Vector3 RayDir; - public Vector3 NHKKHPLFMNG;
                static uintptr_t StartPosition; // public Vector3 StartPosition; - public Vector3 BOGOIAMJFDN;
        };

        class InventoryManager : public Player // class NPCNMJAGIKI
        {
        public:
                static uintptr_t m_itemOnHand; // private Item m_itemOnHand; - private AAHMJHHPECM LFEPIIENLAF;
        };

        // ==================== Avatar ====================

        class AvatarManager : public Player
        {
        public:
                static uintptr_t m_Avatar; // internal IUmaAvatar m_Avatar; - internal IUmaAvatar EEAGBKBMBLD;
        };

        class UmaAvatarSimple : public AvatarManager
        {
        public:
                static uintptr_t IsVisible; // private bool IsVisible;

                /*
                 * (V9) SKIN CHANGER: m_Recipes é Int32[] (array cru —
                 * len no +0xC/+0x18, dados no +0x10/+0x20, índice = slot
                 * do wardrobe). m_VisibleSlots/m_ChangedSlots são bitmasks.
                 * NUNCA escreva lastBuildNotFinish (o loader trava em 99%).
                 */
                static uintptr_t m_Recipes;             // private Int32[] m_Recipes;
                static uintptr_t m_VisibleSlots;        // private Int32 m_VisibleSlots;
                static uintptr_t m_ChangedSlots;        // private Int32 m_ChangedSlots;
                static uintptr_t lastBuildNotFinish;    // private bool lastBuildNotFinish (LEITURA apenas)
                static uintptr_t m_CustomTextureDirty;  // private bool m_CustomTextureDirty;
        };

        class UMAAvatarBase : public UmaAvatarSimple
        {
        public:
                static uintptr_t umaData; // public UMAData umaData;
        };

        class UMAData : UMAAvatarBase
        {
        public:
                static uintptr_t skeleton; // public UMASkeleton skeleton;
                static uintptr_t isLocalPlayer; // public bool isLocalPlayer;
                static uintptr_t isTeammate; // public bool isTeammate;

                // (V9) SKIN CHANGER: dirty flags que forçam rebuild do avatar
                static uintptr_t isMeshDirty;    // public bool isMeshDirty;
                static uintptr_t isTextureDirty; // public bool isTextureDirty;
        };

        /*
         * (V9) SKIN CHANGER — AvatarWardrobeDataManager (singleton):
         * TypeInfo resolvido automaticamente pelo scan do daemon (V9).
         * statics[0] = instância; instância + m_dictIdToWardrobeDataTree =
         * IntervalTreeDic; tree + m_Data = List<AvatarWardrobeData>;
         * items = List._items (array); entrada: iID +0x28/0x30,
         * wardrobeType +0x2C/0x34, bloco visual (4 ints de recipe hash)
         * +0x8/0x10 (0x10 bytes).
         */
        class AvatarWardrobeDataManager
        {
        public:
                static uintptr_t AvatarWardrobeDataManager_TypeInfo;
                static uintptr_t m_dictIdToWardrobeDataTree;
        };

        class IntervalTreeDic
        {
        public:
                static uintptr_t m_Data; // public List`1 m_Data; (após m_Tree)
        };

        class AvatarWardrobeData
        {
        public:
                static uintptr_t VisualBase;    // recipeHashInLobby (4 x Int32, 0x10 bytes)
                static uintptr_t iID;           // public UInt32 iID;
                static uintptr_t wardrobeType;  // public Byte wardrobeType;
        };

        class UMASkeleton
        {
        public:
                static uintptr_t boneHashDataLookup; // private Dictionary<int, UMASkeleton.BoneData> boneHashDataLookup;
                static uintptr_t boneNameHash; // public int boneNameHash; (UMASkeleton.BoneData)
                static uintptr_t boneTransform; // public Transform boneTransform; (UMASkeleton.BoneData)
        };

        // ==================== Replication ====================

        class ReplicationEntity
        {
        public:
                static uintptr_t m_PRIDataPool; // protected IPRIDataPool m_PRIDataPool;
                static uintptr_t m_Datas; // protected ReplicationData[] m_Datas // 0x8
                static uintptr_t HealthCurrentPtr; // protected Dictionary<uint, DataChangedHanlder<sbyte>> m_Int8Handlers; // 0x10
                static uintptr_t HealthMaxPtr; // protected Dictionary<uint, DataChangedHanlder<byte>> m_UInt8Handlers; // 0x14
                static uintptr_t WeaponPtr; // protected Dictionary<uint, DataChangedHanlder<int>> m_Int32Handlers; // 0x20
                static uintptr_t EpPtr; // protected Dictionary<uint, DataChangedHanlder<long>> m_Int64Handlers; // 0x28
                static uintptr_t Value; // public ReplicationDataValueUnion Value; // 0x10
        };

        // ==================== Shadow ====================

        class ShadowState
        {
        public:
                static uintptr_t TargetPhysXPose; // public EPlayerPhysXPose TargetPhysXPose; - public FBCAHNCLMDC ADFIDIPODGK
        };

        // ==================== Profile ====================

        class BaseProfileInfo
        {
        public:
                static uintptr_t AccountID; // public ulong AccountID;
                static uintptr_t Level; // public uint Level;
                static uintptr_t NickName; // public string NickName;
        };

        // ==================== Weapon ====================

        class Weapon : public InventoryManager // class GPBDEDFKJNA
        {
        public:
                static uintptr_t FireComponent; // private WeaponFireComponent <FireComponent>k__BackingField; - private CHEJCCHHDMH <NOAOCMKGLAH>k__BackingField;
                static uintptr_t m_WeaponData; // protected WeaponData m_WeaponData; - protected OOIPMACFIFL LAEMLAPIAFD;
                static uintptr_t m_WeaponParams; // protected WeaponParams m_WeaponParams; - protected FKPFNILEOHE EFGDILOKKDP;
                static uintptr_t m_FireDuration; // protected float m_FireDuration; - protected float EEJLKDDDJJD;
                static uintptr_t m_IsSighting; // private bool m_IsSighting; - private bool GAFGBLFCKAF;
                static uintptr_t tangentTheta; // private float tangentTheta; - private float EFMCDHABKGP; class OACEDDHKLIM : CHEJCCHHDMH
                static uintptr_t IntWeaponType; // public int IntWeaponType; - public int PFBCILHBHBD; class OOIPMACFIFL
        };

        struct WeaponParams : public Weapon // struct FKPFNILEOHE
        {
        public:
                static uintptr_t FullDamageDistance; // public float FullDamageDistance; - public float JEDNPECNGCG;
                static uintptr_t PrefireDelay; // public float PrefireDelay; - public float PMCADKCCFKE;
                static uintptr_t Range; // public float Range; - public float NKGDADJJLAC;
        };

        // ==================== Transform / Position ====================
        class PlayerTransformNode
        {
        public:
                static uintptr_t Transform; // private Transform <transform>k__BackingField - private Transform <EPEGNDBBNMF>k__BackingField;
                static uintptr_t m_CachedTransform; // protected Transform m_CachedTransform;
        };

        class GetPosWorld
        {
        public:
                static uintptr_t transObj;
                static uintptr_t matrix;
                static uintptr_t index;
                static uintptr_t matrix_list;
                static uintptr_t matrix_indices;

                static uintptr_t HeadColliderMale;
                static uintptr_t HeadColliderFemale;
                static uintptr_t ColliderTransform;
                static uintptr_t BoundsCenter_1;
                static uintptr_t BoundsCenter_2;
                static uintptr_t BoundsCenter_3;
        };

        // ==================== IL2CPP Containers ====================

        template <bool N32, typename TValue = std::conditional_t<N32, uint32_t, uint64_t>>
        class UnityList
        {
        public:
                uintptr_t GetItems();
                int GetSize();
                TValue GetItem(int Index);
        };

        template <bool N32, bool V31 = false, typename TValue = std::conditional_t<N32, uint32_t, uint64_t>>
        class UnityDictionary
        {
        public:
                uintptr_t GetValues();
                int GetNumValues();
                TValue GetValue(int Index);
        };

            static void GameConfig();

        /*
         * (V9) AUTO-RESOLVE DE TYPEINFO: varre a .data da libil2cpp via
         * daemon (BRIDGE_CMD_FIND_TYPEINFO) e sobrescreve os TypeInfo
         * manuais (GameFacade/GameVarDef/AvatarWardrobeDataManager) com
         * os RVAs REAIS do build atual. Curta o "para no AccessClass"
         * depois de update do jogo. Retorna true quando achou os 3.
         */
        static bool AutoResolveTypeInfos();

        /*
         * true quando o perfil aplicado tem os offsets-chave != 0.
         * APENAS diagnostico — nao bloqueia mais nada (sem gate).
         */
        static bool Loaded();
private:
        static void FFTHV7A75();
        static void FFTHV7A76();
        static void FFTHV8A();

        /*
         * Zera TODOS os offsets antes de aplicar um perfil. Garante que
         * trocar "FF v7a" <-> "FF v8a" na Settings nao deixe valor velho
         * de um perfil vazando dentro do outro.
         */
        static void ZerarOffsets();
};