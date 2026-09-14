#include "Draw.hpp"
#include "Silent.hpp"
#include <imgui_internal.h>
#include "../AndroidInput.hpp"
#include <algorithm>
#include <unordered_set>
#include <cmath>
#include <Globals.hpp>
#include <Unity/Unity.hpp>
#include <Unity/UTF/UTF8.hpp>
#include <Fonts/Fonts.hpp>
#include "Weapon/NameGun.h"
#include <Utils/Utils.hpp>
#include <Math/Quaternion/Quaternion.hpp>
#include <Math/MathUtils.hpp>
#include "Skeleton.hpp"
#include <android/log.h>
#include <cstdarg>
#include <cstdint>
#include <pthread.h>

#ifndef __ANDROID__
#include <DynamicStub/DynamicStub.hpp>
#endif

// Static members
std::vector<PlayerData> Data::m_Players;
GameContext Data::m_Context{ };
std::mutex Data::m_Mutex;
std::atomic<bool> Data::m_Running{ false };
pthread_t Data::m_ThreadHandle{};
bool Data::m_ThreadValid = false;

bool Data::m_SnapshotFresh = false;

std::atomic<int64_t> Data::m_LastFreshTick{ 0 };

static void DiagLog(
    const char* format,
    ...
)
{
    va_list args;
    va_start(args, format);

    __android_log_vprint(
        ANDROID_LOG_DEBUG,
        "StormDiag",
        format,
        args
    );

    va_end(args);
}

// =====================================================================
//  DIAGNOSTICO DA CADEIA [CHAIN] — dependencia zero
//  Loga no logcat com tag: StormDiag   (adb logcat -s StormDiag)
// =====================================================================
#include <android/log.h>
#include <cstdarg>
#include <cstdio>
#include <ctime>

namespace ReadChain
{
    enum Step : int
    {
        OffsetsZerados = 0,
        ElfMagic, GameFacade, AccessClass, MatchGame, Match, MatchState,
        LocalPlayer, CameraControllerManager, Camera, CachedPtr,
        ViewMatrix, EntityList, DictCount,
        STEP_COUNT
    };

    inline const char* Name(int s)
    {
        static const char* kNames[STEP_COUNT] =
        {
            "offsets-zerados", "elf-magic", "GameFacade", "AccessClass",
            "MatchGame", "Match", "MatchState", "LocalPlayer",
            "CameraControllerManager", "Camera", "m_CachedPtr",
            "ViewMatrix", "EntityList", "dictCount"
        };
        return (s >= 0 && s < STEP_COUNT) ? kNames[s] : "?";
    }
}

// Logger proprio do bloco — NAO mexe no teu DiagLog
static void ChainLogRaw(const char* fmt, ...)
{
    va_list a;
    va_start(a, fmt);
    __android_log_vprint(ANDROID_LOG_DEBUG, "StormDiag", fmt, a);
    va_end(a);
}

static unsigned long long ChainTickMs()
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (unsigned long long)ts.tv_sec * 1000ull
         + (unsigned long long)ts.tv_nsec / 1000000ull;
}

// Falha de um passo da cadeia — rate-limit de 5s POR passo (sem spam)
static void ChainFailLog(int step, const char* fmt, ...)
{
    static unsigned long long s_Last[ReadChain::STEP_COUNT] = { 0 };

    if (step < 0 || step >= ReadChain::STEP_COUNT)
        return;

    unsigned long long now = ChainTickMs();
    if (now - s_Last[step] < 5000ull)
        return;
    s_Last[step] = now;

    char detail[192];
    va_list a;
    va_start(a, fmt);
    vsnprintf(detail, sizeof(detail), fmt, a);
    va_end(a);

    ChainLogRaw("[CHAIN] cadeia parou em %s — %s", ReadChain::Name(step), detail);
}

// Cadeia completa — heartbeat de sucesso a cada 10s
static void ChainOkLog(int entities, int matchState, bool isObserving)
{
    static unsigned long long s_LastOk = 0;

    unsigned long long now = ChainTickMs();
    if (now - s_LastOk < 10000ull)
        return;
    s_LastOk = now;

    ChainLogRaw("[CHAIN] OK — cadeia completa: %d entidades, matchState=%d, observando=%d",
                entities, matchState, isObserving ? 1 : 0);
}

bool Data::m_ThreadN32 = false;
bool Data::m_ThreadV31 = false;

// Uma view-projection matrix valida tem elementos finitos e magnitude normal.
// Uma leitura "rasgada" (o jogo escreve os 64 bytes enquanto lemos) ou de um
// camera invalida costuma vir com NaN/Inf, tudo ~0 ou valores absurdos.
// Rejeitar essa matrix mantem a ultima camera boa em uso — o ESP nunca
// reprojeta com lixo e, portanto, nunca "some" por causa de um frame ruim.
static bool IsValidViewMatrix( const Matrix4x4& m )
{
        for ( int i = 0; i < 16; i++ )
        {
                if ( !std::isfinite( m.v [ i ] ) )
                        return false;
        }
        float s = 0.f;
        for ( int r = 0; r < 4; r++ )
        {
                for ( int c = 0; c < 4; c++ )
                        s += m.m [ r ][ c ] * m.m [ r ][ c ];
        }
        return ( s > 1e-4f && s < 1e12f );
}

// Constants
constexpr float kNameOffset = 10.0f;
constexpr float kHealthOffsetBase = 8.0f;
constexpr float kHealthBarHeight = 2.5f;
constexpr float kStackGap = 2.0f;

// Globals
static bool IsCursorVisibleNow( )
{
        CURSORINFO ci = { };
        ci.cbSize = sizeof( ci );
        if ( GetCursorInfo( &ci ) )
        {
                return ( ci.flags & 0x00000001 ) != 0;
        }
        return true;
}

// ==================== Disparo do player local (Player::IsFiring) ====================
// Le o campo do JOGO que indica que o player esta atirando. E a fonte da
// verdade para o aimbot (so pode agir enquanto atira).
static bool ReadLocalFiring( uintptr_t localPlayer )
{
        /*
         * Perfil sem offset (v8a TODO): nao ha como ler disparo — devolve
         * true para nao desligar o aimbot de quem usa outro perfil (degrada
         * para o comportamento antigo, so-tecla).
         */
        if ( localPlayer == 0 || Offsets::Player::IsFiring == 0 )
                return true;

        /*
         * FIX "TÁ ATIRANDO E O AIMBOT NAO VAI": Read<T> devolve ZERO/FALSE
         * quando a leitura pela ponte FALHA — e no meio da troca de tiro a
         * ponte esta no limite (silent + ESP + aimbot disputando o socket),
         * entao falhas transitorias eram interpretadas como "nao atirando"
         * e o aimbot morria EXATAMENTE durante o combate.
         *
         * Agora: 3 tentativas (2ms entre elas) e, persistindo a falha, usa
         * o ULTIMO valor lido com sucesso (valido por 500ms) antes de
         * concluir. Falso-negativo de falha nao derruba mais o aimbot.
         */
        static LONGLONG s_LastGoodFireTickMs = 0;
        static bool s_LastGoodFireValue = false;
        static bool s_LastLoggedFire = false;
        static LONGLONG s_LastFireLogMs = 0;

        for ( int attempt = 0; attempt < 3; ++attempt )
        {
                bool value = false;

                if ( g_FreeFireMemory.Read( localPlayer + Offsets::Player::IsFiring, &value, 1 ) )
                {
                        s_LastGoodFireValue = value;
                        s_LastGoodFireTickMs = ( LONGLONG )GetTickCount64( );

                        /* Log so na MUDANCA de estado (ou 1x/2s) — o LOGI por
                         * frame virava spam e escondia o sinal no logcat. */
                        const LONGLONG nowLogMs = ( LONGLONG )GetTickCount64( );
                        if ( value != s_LastLoggedFire || ( nowLogMs - s_LastFireLogMs ) > 2000 )
                        {
                                s_LastLoggedFire = value;
                                s_LastFireLogMs = nowLogMs;
                                DiagLog( "[firing] IsFiring=%s", value ? "TRUE (atirando)" : "false" );
                        }

                        return value;
                }

                Sleep( 2 );
        }

        return s_LastGoodFireValue &&
               ( ( LONGLONG )GetTickCount64( ) - s_LastGoodFireTickMs ) < 500;
}

bool ghostActive = false;
bool ghostSkeletonExists = false;
Vector3 ghostSkeletonPos = { 0, 0, 0 };
uintptr_t RageTarget = 0;
bool enemiesvisible = true;

static std::vector<float> g_SmoothedHealth;

/*
 * FIX "AIMBOT/SILENT NAO DÃO SINAL" (mobile):
 *
 * No Windows o gatilho das funções de mira era uma TECLA FÍSICA
 * (GetAsyncKeyState). No Android o "key" é um BOTÃO FLUTUANTE
 * (FloatingKeys, vk 0x7000+) — e quando o usuário NUNCA criou o botão,
 * o KeyBind fica 0 e IsKeyPressed(0) devolve false PARA SEMPRE.
 * Resultado: ativar o checkbox no painel não tinha efeito nenhum no
 * BoneSwap/Magnet/Rage (nenhum log, nenhum write — parecia morto).
 *
 * Regra agora:
 *   - KeyBind != 0 (botão flutuante criado): o gatilho é o botão
 *     (toque = toggle no modo simples, segurar no modo hold).
 *   - KeyBind == 0 (nenhum botão): o próprio checkbox da função é o
 *     gatilho — ativar no painel liga a mira de verdade.
 */
static bool AimTriggerHeld( int keyBind, bool enabledFlag )
{
        return ( keyBind == 0 )
                ? enabledFlag
                : AndroidInput::IsKeyPressed( keyBind );
}

// ==================== ESP Overlay Helper ====================
// Desenha o overlay de um jogador a partir do snapshot. Quando uma view matrix
// valida e passada, reprojeta as posicoes de mundo (mantendo o ESP grudado nos
// jogadores mesmo quando a leitura de entidades engasga — como no cheat de
// referencia). Com matrix invalida (restart/transicao) usa as posicoes de tela
// congeladas do snapshot. Nenhuma leitura viva e feita aqui; o unico trecho que
// depende de memoria ao vivo e o skeleton (desativado via drawSkeleton=false
// quando a base esta invalida).
static void DrawEspEntityOverlay( const PlayerData& p, ImDrawList* DL, const struct Cheat::Globals::Visuals::ESP& ESP, const Matrix4x4& ViewMatrix, bool N32, bool V31, bool drawSkeleton )
{
        if ( ESP.RenderDistance > 0 && p.Distance > ESP.RenderDistance )
                return;

        // Jogador morto NUNCA e desenhado — nem no caminho ao vivo nem no
        // congelado. A captura ja filtra (CurrentHealth <= 0), mas o snapshot
        // congelado segurava cadavers por ate 25s com a leitura engasgada
        // ("ESP de morto" grudada na tela). O knocked (pose 8) tem HP > 0 e
        // continua aparecendo em vermelho, como esperado.
        if ( p.CurrentHealth <= 0 )
                return;

        Vector3 headScreen = p.HeadScreen;
        Vector3 feetScreen = p.FeetScreen;

        if ( ViewMatrix.m [ 0 ][ 0 ] != 0.f && p.HeadWorld != Vector3::Zero( ) )
        {
                headScreen = W2S::World2Screen( ViewMatrix, p.HeadWorld );
                feetScreen = W2S::World2Screen( ViewMatrix, p.FeetWorld );

                // So a cabeca atras da camera derruba o jogador. Pes atras (inimigo
                // muito perto / camera baixa) NAO pode descartar a entidade inteira —
                // era isso que fazia a snapline (e o resto da ESP) piscar e sumir com
                // inimigo proximo. Usa a cabeca como fallback dos pes nesse caso.
                if ( headScreen.Z <= 0 )
                {
                        // Cabeca atras mas pes na frente = inimigo colado/abaixo da camera
                        // (melee). Usa os pes como referencial em vez de descartar o
                        // jogador inteiro — antes esse jogador sumia da ESP ate se afastar.
                        if ( feetScreen.Z > 0 )
                                headScreen = feetScreen;
                        else
                                return;
                }
                if ( feetScreen.Z <= 0 )
                        feetScreen = headScreen;
        }
        else if ( headScreen == Vector3::Zero( ) || feetScreen == Vector3::Zero( ) )
        {
                // Sem view matrix valida e a entidade nunca projetou (snapshot gravado
                // com matrix ruim): nao ha o que desenhar neste frame.
                return;
        }

        const float Height = fabsf( feetScreen.Y - headScreen.Y );
        // Altura minima: com os pes atras da camera (fallback da cabeca) a altura
        // colapsa a 0 e box/healthbar/weapon somem para aquele inimigo — parece
        // que a ESP parou. Mantem um tamanho minimo visivel.
        const float HeightSafe = ( Height < 2.0f ) ? 2.0f : Height;
        const float Width = HeightSafe * 0.5f;

        // Cor do elemento: aliado (cor de time) > caido (vermelho) > cor da opcao.
        auto pickColor = [ &ESP, &p ] ( const float c [ 4 ] ) -> ImColor
        {
                if ( p.IsTeammate )
                        return ImColor( ESP.TeamColor [ 0 ], ESP.TeamColor [ 1 ], ESP.TeamColor [ 2 ], ESP.TeamColor [ 3 ] );
                if ( p.IsKnocked )
                        return ImColor( 1.f, 0.f, 0.f, 1.f );
                return ImColor( c [ 0 ], c [ 1 ], c [ 2 ], c [ 3 ] );
        };

        // --- Name ---
        if ( ESP.ShowName )
        {
                ImColor nameColor = pickColor( ESP.NameColor );
                ImVec2 TextSize = Utils::CalcTextSize( Fonts::Gff, ESP.TextSize, p.Name.c_str( ) );
                ImVec2 NamePos( ( headScreen.X - Width * 0.5f ) + ( Width * 0.5f ) - ( TextSize.x * 0.5f ), headScreen.Y - kNameOffset );
                DL->AddText( Fonts::Gff, ESP.TextSize, NamePos, nameColor, p.Name.c_str( ) );
        }

        // --- Box ---
        if ( ESP.Box )
        {
                ImColor Color = pickColor( ESP.BoxColor );
                ImColor ColorFilled = pickColor( ESP.FilledBoxColor );
                Data::DrawBox( headScreen.X - Width * 0.5f, headScreen.Y, Width, HeightSafe, Color, ColorFilled, ESP.Thickness, ESP.BoxStyle );
        }

        // --- SnapLine ---
        if ( ESP.SnapLines )
        {
                ImColor Color = pickColor( ESP.SnapLinesColor );
                const bool healthTop = ( ESP.HealthBarStyle == 3 );
                Data::DrawSnapLine( headScreen, feetScreen, ESP.ShowName, healthTop, Color, ESP.Thickness, ESP.SnapLinesPos );
        }

        // --- HealthBar ---
        if ( ESP.HealthBar )
        {
                Data::DrawHealthBar( p.CurrentHealth, p.MaxHealth, ImVec2( headScreen.X, headScreen.Y ), ImVec2( feetScreen.X, feetScreen.Y ), Width, HeightSafe, ( uintptr_t )p.Entity );
        }

        // --- Weapon ---
        if ( ESP.Weapon )
        {
                Data::DrawWeapon( p.WeaponID, p.IsKnocked, headScreen, HeightSafe );
        }

        // --- Distance ---
        if ( ESP.Distance )
        {
                int mRounded = ( int )( p.Distance + 0.5f );
                char distanceText [ 16 ];
                snprintf( distanceText, sizeof( distanceText ), XorStr( "%dm" ), mRounded );

                ImGui::PushFont( Fonts::Verdana );
                ImColor Color = pickColor( ESP.DistanceColor );
                ImVec2 sz = Utils::CalcTextSize( Fonts::Verdana, ESP.TextSize, distanceText );
                DL->AddText( Fonts::Verdana, ESP.TextSize, ImVec2( headScreen.X - sz.x * 0.5f, feetScreen.Y + 5 ), Color, distanceText );
                ImGui::PopFont( );
        }

        // --- Skeleton ---
        if ( drawSkeleton )
                Skeleton::DrawPlayer( DL, p.Entity, p.UMAData, p.IsKnocked, ViewMatrix, p.FeetWorld, N32, V31 );
}

// ==================== Read Thread ====================

template <bool N32, bool V31>
void* Data::ReadLoopWrapper(void*)
{
    ReadLoop<N32, V31>();
    return nullptr;
}

void Data::StartReadThread()
{
    if (m_Running.load())
        return;

        /*
         * LOG DE OPERACAO DESLIGADO: logar cada READ no logd custa uma
         * escrita de log POR LEITURA e o volume da thread de leitura e
         * altissimo - isso sozinho derrubava a taxa de atualizacao do ESP.
         * (Diagnostico: ligue manualmente com Memory::EnableOpLogging(true))
         */

    m_Running.store(true);

    const bool N32 =
        g_Globals.General.N32;

    const bool V31 =
        g_Globals.General.V31;

    pthread_t thread{};

    int result = 0;

    if (N32 && V31)
    {
        result = pthread_create(
            &thread,
            nullptr,
            &ReadLoopWrapper<true, true>,
            nullptr
        );
    }
    else if (N32 && !V31)
    {
        result = pthread_create(
            &thread,
            nullptr,
            &ReadLoopWrapper<true, false>,
            nullptr
        );
    }
    else if (!N32 && V31)
    {
        result = pthread_create(
            &thread,
            nullptr,
            &ReadLoopWrapper<false, true>,
            nullptr
        );
    }
    else
    {
        result = pthread_create(
            &thread,
            nullptr,
            &ReadLoopWrapper<false, false>,
            nullptr
        );
    }

    if (result != 0)
    {
        m_ThreadValid = false;
        m_Running.store(false);
        return;
    }

    m_ThreadHandle = thread;
    m_ThreadValid = true;
}

void Data::StopReadThread()
{
    m_Running.store(false);

    if (!m_ThreadValid)
        return;

    pthread_join(
        m_ThreadHandle,
        nullptr
    );

    m_ThreadValid = false;
}

template <bool N32, bool V31>
void Data::ReadLoop( )
{
        int failCount = 0;
        int lobbyFrames = 0;
        int emptyFrames = 0;
        LONGLONG emptyStartMs = 0;
        LONGLONG lobbyStartMs = 0;
        while ( m_Running.load( ) && !g_Globals.General.ShutDown )
        {
                try
                {
                /*
                 * SEM GUARDA de offsets: o ReadLoop roda direto com o perfil
                 * escolhido na Settings. Offset zerado = a cadeia falha na
                 * etapa correspondente e o proprio ChainFailLog abaixo mostra
                 * onde parou (nada le o ELF como ponteiro sem log).
                 */

                std::vector<PlayerData> tempPlayers;
                GameContext tempCtx{ };
                // Entidades confirmadas como inimigo no frame atual (na lista oficial
                // e com classe conhecida). Servem de base para o carry-over: quem
                // passou destes checks mas caiu por falha transitória NÃO sai do
                // snapshot — herda o último estado bom da entidade (anti-flicker).
                std::unordered_set<uintptr_t> seenThisFrame;

                // fresh          = o frame inteiro (facade->partida->entidades) leu ok
                // readMatchState = conseguiu ler o estado da partida
                // matchActive    = a partida está ativa
                // matchRead      = conseguiu ler o ponteiro do Match (sinal confiavel
                //                  de "estamos numa partida"; false = cadeia quebrou
                //                  no nivel de partida/lobby/facade)
                bool fresh = false;
                bool readMatchState = false;
                bool matchActive = false;
                bool matchRead = false;

                do
                {
                        Memory::FlushTLB( );

                        // Revalida o CR3 do processo a cada ~64 iteracoes. Se o processo
                        // do jogo reiniciou no meio da sessao, o CR3 antigo envenena TODAS
                        // as leituras de uma vez (ESP + aimbot + silent param juntos). O
                        // check de ELF magic nao detecta isso quando a pagina antiga ainda
                        // esta mapeada — so a revalidacao direta do pgd pega.
                        static int cR3Tick = 0;
                        if ( ( ++cR3Tick & 0x3F ) == 0 )
                                Memory::RefreshCR3( );

                        // Valida a base do modulo ANTES de qualquer leitura. Quando o
                        // processo do jogo reinicia (ex: alt-tab no emulador) o ASLR muda a
                        // base e o magic ELF some — mesmo que leituras de ponteiro voltem
                        // lixo nao-zero (que antes nao era detectado). Com base invalida:
                        // mantem o snapshot congelado (ESP nao some) e agenda restart apos
                        // falha sustentada.
                        uint32_t ElfMagic = 0;
                        const bool elfReadOk = g_FreeFireMemory.Read<uint32_t>( Offsets::LibIl2Cpp, ElfMagic );
                        if ( !elfReadOk || ElfMagic != 0x464C457F )
                        {
                                ChainFailLog( ReadChain::ElfMagic,
                                              elfReadOk
                                                      ? "magic invalido (lido=0x%X) — base errada ou pagina trocada"
                                                      : "leitura falhou (processo morto? base desmapeada?)",
                                              ElfMagic );
                                if ( ++failCount > 20 )
                                {
                                        failCount = 0;
                                        DiagLog( "[diag] ELF fail: restart async apos %d frames com base invalida", 20 );
                                        g_FreeFireMemory.RestartAsync( );
                                }
                                break;
                        }
                        failCount = 0;

                        auto ReadPtr = [ ] ( uintptr_t addr ) -> uintptr_t
                        {
                                return N32 ? g_FreeFireMemory.Read<uint32_t>( addr ) : g_FreeFireMemory.Read<uint64_t>( addr );
                        };

                        uintptr_t GameFacade = ReadPtr( Offsets::LibIl2Cpp + Offsets::GameFacade::GameFacade_TypeInfo );
                        if ( GameFacade == 0 )
                        {
                                // Nulo aqui = estamos no lobby OU o offset GameFacade_TypeInfo
                                // nao serve para esta versao do jogo (cai sempre neste break).
                                ChainFailLog( ReadChain::GameFacade,
                                              "GameFacade nulo (lib+0x%lX) — lobby ou offset TypeInfo errado para esta versao",
                                              ( unsigned long )Offsets::GameFacade::GameFacade_TypeInfo );
                                break;
                        }
                        static LONGLONG s_LastGfLog = 0;
                        if ( GetTickCount64( ) - s_LastGfLog > 10000 )
                        {
                                s_LastGfLog = GetTickCount64( );
                                DiagLog( "[CHAIN] GameFacade=0x%lX (lido em lib+0x%lX)",
                                         ( unsigned long )GameFacade,
                                         ( unsigned long )Offsets::GameFacade::GameFacade_TypeInfo );
                        }

                        uintptr_t AccessClass = ReadPtr( GameFacade + Offsets::AccessClass );
                        if ( AccessClass == 0 )
                        {
                                ChainFailLog( ReadChain::AccessClass,
                                              "AccessClass nulo (GameFacade=0x%lX + 0x%lX) — offset AccessClass errado?",
                                              ( unsigned long )GameFacade, ( unsigned long )Offsets::AccessClass );
                                break;
                        }

                        uintptr_t MatchGame = ReadPtr( AccessClass + Offsets::GameFacade::CurrentMatchGame );
                        if ( MatchGame == 0 )
                        {
                                ChainFailLog( ReadChain::MatchGame,
                                              "MatchGame nulo (AccessClass=0x%lX + 0x%lX) — offset CurrentMatchGame errado?",
                                              ( unsigned long )AccessClass, ( unsigned long )Offsets::GameFacade::CurrentMatchGame );
                                break;
                        }

                        uintptr_t Match = ReadPtr( MatchGame + Offsets::MatchGame::m_Match );
                        if ( Match == 0 )
                        {
                                ChainFailLog( ReadChain::Match,
                                              "Match nulo (MatchGame=0x%lX + 0x%lX) — offset m_Match errado?",
                                              ( unsigned long )MatchGame, ( unsigned long )Offsets::MatchGame::m_Match );
                                break;
                        }
                        matchRead = true;

                        int MatchRaw = g_FreeFireMemory.Read<int>( Match + Offsets::Match::m_State );
                        auto MatchState = static_cast< Offsets::MatchState >( MatchRaw );
                        readMatchState = true;
                        if ( !Offsets::IsMatchActive( MatchState ) )
                        {
                                ChainFailLog( ReadChain::MatchState,
                                              "state=%d fora de [1..3] — fora de partida/lobby",
                                              MatchRaw );
                                break;
                        }
                        matchActive = true;

                        uintptr_t LocalObserver = ReadPtr( Match + Offsets::Match::m_LocalObserver );
                        bool IsObserving = ( LocalObserver != 0 );
                        uintptr_t LocalPlayer;
                        if ( LocalObserver != 0 )
                        {
                                LocalPlayer = ReadPtr( LocalObserver + Offsets::Observer::m_TargetPlayer );
                        }
                        else
                        {
                                LocalPlayer = ReadPtr( Match + Offsets::Match::m_LocalPlayer );
                        }
                        if ( LocalPlayer == 0 )
                        {
                                ChainFailLog( ReadChain::LocalPlayer,
                                              "LocalPlayer nulo (observando=%d, Match=0x%lX) — offset m_LocalPlayer/m_TargetPlayer errado?",
                                              IsObserving ? 1 : 0, ( unsigned long )Match );
                                break;
                        }

                        tempCtx.LocalPlayer = LocalPlayer;
                        tempCtx.MatchGame = MatchGame;
                        tempCtx.Match = Match;
                        tempCtx.IsObserving = IsObserving;

                        uintptr_t MainCamera = ReadPtr( LocalPlayer + Offsets::Player::MainCameraTransform );
                        tempCtx.MainCamera = MainCamera;

                        uintptr_t m_CameraControllerManager = ReadPtr( MatchGame + Offsets::MatchGame::m_CameraControllerManager );
                        if ( m_CameraControllerManager == 0 )
                        {
                                ChainFailLog( ReadChain::CameraControllerManager,
                                              "nulo (MatchGame=0x%lX + 0x%lX)",
                                              ( unsigned long )MatchGame, ( unsigned long )Offsets::MatchGame::m_CameraControllerManager );
                                break;
                        }

                        uintptr_t m_Camera = ReadPtr( m_CameraControllerManager + Offsets::CameraControllerManager::m_Camera );
                        if ( m_Camera == 0 )
                        {
                                ChainFailLog( ReadChain::Camera,
                                              "Camera nula (controller=0x%lX + 0x%lX)",
                                              ( unsigned long )m_CameraControllerManager, ( unsigned long )Offsets::CameraControllerManager::m_Camera );
                                break;
                        }

                        uintptr_t m_CachedPtr = ReadPtr( m_Camera + Offsets::Camera::m_CachedPtr );
                        if ( m_CachedPtr == 0 )
                        {
                                ChainFailLog( ReadChain::CachedPtr,
                                              "m_CachedPtr nulo (camera=0x%lX + 0x%lX)",
                                              ( unsigned long )m_Camera, ( unsigned long )Offsets::Camera::m_CachedPtr );
                                break;
                        }

                        Matrix4x4 ViewMatrix = g_FreeFireMemory.Read<Matrix4x4>( m_CachedPtr + Offsets::Camera::ViewMatrix );
                        if ( !IsValidViewMatrix( ViewMatrix ) )
                        {
                                // Fallback do codigo que funcionava: v7a usa 0xE8, FF MAX
                                // usa 0xE4. Se o offset configurado vier lixo/rasgado, tenta
                                // o offset alternativo antes de desistir do frame.
                                const uintptr_t kAltVM = ( Offsets::Camera::ViewMatrix == 0xE8 ) ? 0xE4 : 0xE8;
                                Matrix4x4 AltVM = g_FreeFireMemory.Read<Matrix4x4>( m_CachedPtr + kAltVM );
                                if ( IsValidViewMatrix( AltVM ) )
                                        ViewMatrix = AltVM;
                        }
                        if ( !IsValidViewMatrix( ViewMatrix ) )
                        {
                                ChainFailLog( ReadChain::ViewMatrix,
                                              "view matrix invalida/rasgada (cachedPtr=0x%lX + 0x%lX)",
                                              ( unsigned long )m_CachedPtr, ( unsigned long )Offsets::Camera::ViewMatrix );
                                break;
                        }
                        tempCtx.ViewMatrix = ViewMatrix;

                        auto EntityList = reinterpret_cast< Offsets::UnityList<N32>* >( ReadPtr( Match + Offsets::Match::m_AttackableEntities ) );
                        if ( EntityList == nullptr )
                        {
                                ChainFailLog( ReadChain::EntityList,
                                              "lista de entidades nula (Match=0x%lX + 0x%lX)",
                                              ( unsigned long )Match, ( unsigned long )Offsets::Match::m_AttackableEntities );
                                break;
                        }

                        int dictCount = EntityList->GetSize( );
                        if ( dictCount <= 0 || dictCount > 200 )
                        {
                                ChainFailLog( ReadChain::DictCount,
                                              "count=%d fora de [1..200] — lista lixo ou offset errado",
                                              dictCount );
                                break;
                        }

                        tempPlayers.reserve( dictCount );
                        seenThisFrame.reserve( dictCount );

                        /*
                         * Contadores de descarte por motivo. O ESP pode "nao aparecer"
                         * porque TODAS as entidades morrem em algum filtro intermediario
                         * (classe, time, HP, posicao) sem que a cadeia principal falhe.
                         * O resumo [ENTITY] (rate-limit 5s) mostra no logcat exatamente
                         * qual etapa esta comendo os players.
                         */
                        int entOk = 0, entLocal = 0, entClasse = 0, entAvatar = 0;
                        int entTeam = 0, entPri = 0, entHp = 0, entPos = 0;
                        int entNulo = 0;                                    // ← ADICIONE
                        bool sampleLogged = false;


                        // ═══════════════════════════════════════════════════════════
                        // LEITURA EM ONDAS (READ BATCH pela ponte)
                        //
                        // O loop antigo fazia ~30 round-trips de socket POR
                        // ENTIDADE (cada Read<T> = 1 viagem ao daemon). Com 50
                        // entidades eram ~1500 viagens por tick — o ESP levava
                        // centenas de ms pra atualizar.
                        //
                        // Agora cada ONDA le N enderecos (todas as entidades)
                        // em UM unico round-trip (BRIDGE_CMD_READ_BATCH), e a
                        // montagem/ filtros ficam 100% locais. O frame inteiro
                        // custa ~20-25 round-trips FIXOS, independente do
                        // numero de players. Semantica preservada:
                        //   - ponteiro lido como 0 = falha (igual Read<T>)
                        //   - mesma ordem de descarte e mesmos contadores
                        // ═══════════════════════════════════════════════════════════

                        // ---------- helpers ----------
                        struct BatchReq { uintptr_t addr; uint32_t size; void* out; };

                        auto RunWave = [](std::vector<BatchReq>& reqs)
                        {
                                if (reqs.empty()) return;

                                std::vector<Memory::BatchItem> items(reqs.size());
                                size_t total = 0;

                                for (size_t i = 0; i < reqs.size(); i++)
                                {
                                        items[i].address = reqs[i].addr;
                                        items[i].size    = reqs[i].size;
                                        total           += reqs[i].size;
                                }

                                std::vector<uint8_t> blob;

                                if (!Memory::ReadBatch(items.data(), items.size(), blob) || blob.size() < total)
                                        blob.resize(total, 0);   // ponte falhou: tudo zero (entidades saem por 1 frame, igual a falha de leitura antiga)

                                size_t off = 0;

                                for (size_t i = 0; i < reqs.size(); i++)
                                {
                                        memcpy(reqs[i].out, blob.data() + off, reqs[i].size);
                                        off += reqs[i].size;
                                }

                                reqs.clear();
                        };

                        const uint32_t kPtrSz = N32 ? 4u : 8u;

                        auto addPtr = [kPtrSz](std::vector<BatchReq>& w, uintptr_t base, uintptr_t off, void* out)
                        {
                                if (base != 0)
                                        w.push_back({ base + off, kPtrSz, out });
                        };

                        struct WalkSt
                        {
                                uintptr_t tf = 0, transObj = 0, matObj = 0, matList = 0, matIdx = 0;
                                uintptr_t idx = 0;
                                int iters = 0;
                                bool ok = false;
                                Vector3 acc = Vector3::Zero( );
                                TMatrix tm = { };
                                bool active = false;
                        };

                        struct EntWork
                        {
                                uintptr_t Entity = 0;
                                bool alive = true;
                                // nivel 1
                                uintptr_t klass = 0, klassNamePtr = 0;
                                uintptr_t avatarMgr = 0, priPool = 0, shadow = 0, profile = 0;
                                uintptr_t cachedTF = 0, fireCol = 0, headNode = 0;
                                uint8_t isBot = 0, isFemale = 0;
                                // nivel 2
                                uintptr_t avatar = 0, datas = 0, nick = 0;
                                // nivel 3
                                uintptr_t umaData = 0, hCur = 0, hMax = 0, weaponPtr = 0;
                                int pose = 0, hpCur = 0, hpMax = 0, weaponId = -1, nameLen = 0;
                                uint8_t isTeam = 0;
                                // head collider chain
                                uintptr_t hcTfc = 0, hcLoc = 0, hcH1 = 0, hcH2 = 0, hcH3 = 0;
                                Vector3 headPos = Vector3::Zero( );
                                uintptr_t headTf2 = 0;
                                // walkers
                                WalkSt feetWalk, headWalk;
                                Vector3 feetPos = Vector3::Zero( );
                                // nome
                                uint16_t nameChars[48] = { 0 };
                        };

                        std::vector<EntWork> ents;
                        ents.reserve(dictCount);

                        std::vector<BatchReq> wave;

                        // ---------- posicao da camera: UMA vez por frame ----------
                        // (o codigo antigo re-liava a camera DENTRO do loop por
                        // entidade — a mesma cadeia de 10+ leituras repetida N vezes)
                        Vector3 MainPos = (MainCamera != 0)
                                ? Transform::get_position_Injected(MainCamera, N32)
                                : Vector3::Zero( );

                        // ---------- ONDA 0: array base + ponteiros das entidades ----------
                        uintptr_t itemsBase = EntityList->GetItems();
                        if (itemsBase == 0)
                        {
                                ChainFailLog(ReadChain::EntityList,
                                             "array de itens nulo (Match=0x%lX + 0x%lX)",
                                             (unsigned long)Match, (unsigned long)Offsets::Match::m_AttackableEntities);
                                break;
                        }

                        std::vector<uint64_t> itemPtrs(dictCount, 0);

                        for (int i = 0; i < dictCount; i++)
                                addPtr(wave, itemsBase, (N32 ? 0x4 : 0x8) * (uintptr_t)i, &itemPtrs[i]);
                        RunWave(wave);

                        for (int i = 0; i < dictCount; i++)
                        {
                                if (itemPtrs[i] == 0)
                                {
                                        /* ponteiro nulo = leitura da onda falhou — conta pro diagnóstico */
                                        entNulo++;
                                        continue;
                                }

                                EntWork e;
                                e.Entity = (uintptr_t)itemPtrs[i];
                                ents.push_back(e);
                        }

                        // ---------- ONDA 1a: klass (obj->klass, primeiro campo) ----------
                        for (auto& e : ents)
                                addPtr(wave, e.Entity, 0, &e.klass);
                        RunWave(wave);

                        // ---------- ONDA 1b: klass->name (klass lido na onda anterior) ----------
                        for (auto& e : ents)
                                addPtr(wave, e.klass, N32 ? 0x8 : 0x10, &e.klassNamePtr);
                        RunWave(wave);

                        // ---------- descarte por classe (antes do PlayerType UNKNOWN) ----------
                        std::vector<EntWork> valid;
                        valid.reserve(ents.size());

                        for (auto& e : ents)
                        {
                                if (e.Entity == LocalPlayer) { entLocal++; continue; }
                                if (e.klass == 0 || e.klassNamePtr == 0) { entClasse++; continue; }

                                // Amostra da 1a entidade valida (mesmo proposito do log antigo)
                                if (!sampleLogged)
                                {
                                        sampleLogged = true;
                                        DiagLog("[ENTITY] amostra ent=0x%lX classe=%s",
                                                (unsigned long)e.Entity, Data::GetPlayerTypeName(PLAYER_NETWORK));
                                }

                                seenThisFrame.insert(e.Entity);
                                valid.push_back(e);
                        }

                        ents.swap(valid);

                        // ---------- ONDA 2: ponteiros de nivel 1 ----------
                        for (auto& e : ents)
                        {
                                addPtr(wave, e.Entity, Offsets::Player::m_AvatarManager, &e.avatarMgr);
                                addPtr(wave, e.Entity, Offsets::ReplicationEntity::m_PRIDataPool, &e.priPool);
                                addPtr(wave, e.Entity, Offsets::PlayerNetwork::m_ShadowState, &e.shadow);
                                addPtr(wave, e.Entity, Offsets::PlayerNetwork::m_Profile, &e.profile);
                                addPtr(wave, e.Entity, Offsets::PlayerTransformNode::m_CachedTransform, &e.cachedTF);
                                addPtr(wave, e.Entity, Offsets::Player::m_fireColliders, &e.fireCol);
                                addPtr(wave, e.Entity, Offsets::Player::HeadNode, &e.headNode);

                                if (e.Entity != 0)
                                {
                                        wave.push_back({ e.Entity + Offsets::Player::IsClientBot, 1, &e.isBot });
                                        wave.push_back({ e.Entity + Offsets::Player::IsFemale, 1, &e.isFemale });
                                }
                        }
                        RunWave(wave);

                        // ---------- ONDA 3: nivel 2 (avatar/datas/nick) ----------
                        for (auto& e : ents)
                        {
                                addPtr(wave, e.avatarMgr, Offsets::AvatarManager::m_Avatar, &e.avatar);
                                addPtr(wave, e.priPool, Offsets::ReplicationEntity::m_Datas, &e.datas);
                                addPtr(wave, e.profile, Offsets::BaseProfileInfo::NickName, &e.nick);
                        }
                        RunWave(wave);

                        // ---------- ONDA 4: nivel 3 (umaData/hp ptrs/weapon ptr) ----------
                        for (auto& e : ents)
                        {
                                addPtr(wave, e.avatar, Offsets::UMAAvatarBase::umaData, &e.umaData);
                                addPtr(wave, e.datas, Offsets::ReplicationEntity::HealthCurrentPtr, &e.hCur);
                                addPtr(wave, e.datas, Offsets::ReplicationEntity::HealthMaxPtr, &e.hMax);
                                addPtr(wave, e.datas, Offsets::ReplicationEntity::WeaponPtr, &e.weaponPtr);
                        }
                        RunWave(wave);

                        // ---------- ONDA 5: valores (pose/hp/weapon/isTeam) ----------
                        for (auto& e : ents)
                        {
                                if (e.shadow != 0)
                                        wave.push_back({ e.shadow + Offsets::ShadowState::TargetPhysXPose, 4, &e.pose });
                                if (e.hCur != 0)
                                        wave.push_back({ e.hCur + Offsets::ReplicationEntity::Value, 4, &e.hpCur });
                                if (e.hMax != 0)
                                        wave.push_back({ e.hMax + Offsets::ReplicationEntity::Value, 4, &e.hpMax });
                                if (e.weaponPtr != 0)
                                        wave.push_back({ e.weaponPtr + Offsets::ReplicationEntity::Value, 4, &e.weaponId });
                                if (e.umaData != 0)
                                        wave.push_back({ e.umaData + Offsets::UMAData::isTeammate, 1, &e.isTeam });
                        }
                        RunWave(wave);

                        // ---------- descartes (mesma ordem do loop antigo) ----------
                        {
                                std::vector<EntWork> keep;
                                keep.reserve(ents.size());

                                for (auto& e : ents)
                                {
                                        if (e.avatarMgr == 0 || e.avatar == 0 || e.umaData == 0) { entAvatar++; continue; }

                                        bool IsTeam = (e.isTeam != 0);
                                        if (IsTeam && !g_Globals.Visuals.ESP.ShowTeam) { entTeam++; continue; }

                                        if (e.priPool == 0 || e.datas == 0 || e.hCur == 0 || e.hMax == 0) { entPri++; continue; }

                                        if (e.hpMax == 0 || e.hpCur <= 0) { entHp++; continue; }

                                        keep.push_back(e);
                                }

                                ents.swap(keep);
                        }

                        // ---------- ONDA 6: nome (len e chars) ----------
                        for (auto& e : ents)
                        {
                                if (e.isBot || e.nick == 0)
                                        continue;

                                wave.push_back({ e.nick + (N32 ? 0x8 : 0x10), 4, &e.nameLen });
                        }
                        RunWave(wave);

                        for (auto& e : ents)
                        {
                                if (e.isBot || e.nick == 0)
                                        continue;

                                if (e.nameLen <= 0 || e.nameLen > 44)
                                        continue;

                                wave.push_back({
                                        e.nick + (N32 ? 0xC : 0x14),
                                        (uint32_t)(e.nameLen * 2),
                                        e.nameChars });
                        }
                        RunWave(wave);

                        auto Utf16ToStd = [](const uint16_t* chars, int len) -> std::string
                        {
                                std::string result;
                                result.reserve((size_t)len);

                                for (int i = 0; i < len; i++)
                                {
                                        uint16_t c = chars[i];

                                        if (c == 0) break;

                                        if (c < 0x80)
                                                result.push_back((char)c);
                                        else if (c < 0x800)
                                        {
                                                result.push_back((char)(0xC0 | (c >> 6)));
                                                result.push_back((char)(0x80 | (c & 0x3F)));
                                        }
                                        else
                                        {
                                                result.push_back((char)(0xE0 | (c >> 12)));
                                                result.push_back((char)(0x80 | ((c >> 6) & 0x3F)));
                                                result.push_back((char)(0x80 | (c & 0x3F)));
                                        }
                                }

                                return result;
                        };

                        // ---------- HEAD: cadeia do collider em ondas ----------
                        // h2: collider transform + head transform (bases independentes, mesma onda)
                        for (auto& e : ents)
                        {
                                addPtr(wave, e.fireCol, Offsets::GetPosWorld::ColliderTransform, &e.hcTfc);
                                addPtr(wave, e.headNode, Offsets::PlayerTransformNode::Transform, &e.headTf2);
                        }
                        RunWave(wave);

                        // h3..h6: encadeia o collider
                        for (auto& e : ents)
                        {
                                const uintptr_t headColliderOff = e.isFemale
                                        ? Offsets::GetPosWorld::HeadColliderFemale
                                        : Offsets::GetPosWorld::HeadColliderMale;

                                addPtr(wave, e.hcTfc, headColliderOff, &e.hcLoc);
                        }
                        RunWave(wave);

                        for (auto& e : ents)
                                addPtr(wave, e.hcLoc, Offsets::GetPosWorld::ColliderTransform, &e.hcH1);
                        RunWave(wave);

                        for (auto& e : ents)
                                addPtr(wave, e.hcH1, Offsets::GetPosWorld::BoundsCenter_1, &e.hcH2);
                        RunWave(wave);

                        for (auto& e : ents)
                                addPtr(wave, e.hcH2, Offsets::GetPosWorld::BoundsCenter_2, &e.hcH3);
                        RunWave(wave);

                        {
                                // h7: o Vector3 final + prepara os walkers de fallback
                                std::vector<BatchReq> w7;

                                for (auto& e : ents)
                                {
                                        if (e.hcH3 != 0)
                                                w7.push_back({ e.hcH3 + Offsets::GetPosWorld::BoundsCenter_3, sizeof(Vector3), &e.headPos });

                                        // Fallback da cadeia (só se o collider NAO produziu head):
                                        // walker pelo HeadNode, como no GetHeadPosition original
                                        const bool colliderChainOk = (e.hcTfc != 0 && e.hcLoc != 0 &&
                                                e.hcH1 != 0 && e.hcH2 != 0 && e.hcH3 != 0);

                                        if (!colliderChainOk && e.headTf2 != 0)
                                        {
                                                e.headWalk.tf = e.headTf2;
                                                e.headWalk.active = true;
                                        }

                                        // Feet: walker pelo cachedTransform
                                        if (e.cachedTF != 0)
                                        {
                                                e.feetWalk.tf = e.cachedTF;
                                                e.feetWalk.active = true;
                                        }
                                }

                                RunWave(w7);
                        }

                        // ---------- WALKERS (feet + head fallback) em lockstep ----------
                        // Reproduz o get_position_Injected: transObj -> matrix/index ->
                        // matrix_list/matrix_indices -> composicao da cadeia de pais.
                        std::vector<WalkSt*> walkers;

                        for (auto& e : ents)
                        {
                                if (e.feetWalk.active) walkers.push_back(&e.feetWalk);
                                if (e.headWalk.active) walkers.push_back(&e.headWalk);
                        }

                        // w1: transObj
                        for (auto* w : walkers)
                                addPtr(wave, w->tf, Offsets::GetPosWorld::transObj, &w->transObj);
                        RunWave(wave);

                        // w2: matrix + index
                        for (auto* w : walkers)
                        {
                                addPtr(wave, w->transObj, Offsets::GetPosWorld::matrix, &w->matObj);
                                addPtr(wave, w->transObj, Offsets::GetPosWorld::index, &w->idx);
                        }
                        RunWave(wave);

                        // w3: matrix_list + matrix_indices
                        for (auto* w : walkers)
                        {
                                addPtr(wave, w->matObj, Offsets::GetPosWorld::matrix_list, &w->matList);
                                addPtr(wave, w->matObj, Offsets::GetPosWorld::matrix_indices, &w->matIdx);
                        }
                        RunWave(wave);

                        // w4: posicao local + primeiro indice pai
                        {
                                struct W4 { WalkSt* w; };
                                for (auto* w : walkers)
                                {
                                        if (w->matList == 0 || w->matIdx == 0)
                                                continue;

                                        w->ok = true;
                                        wave.push_back({ w->matList + sizeof(TMatrix) * w->idx, sizeof(Vector3), &w->acc });
                                }
                                RunWave(wave);

                                std::vector<BatchReq> w4b;
                                for (auto* w : walkers)
                                {
                                        if (!w->ok)
                                                continue;

                                        // o indice inicial e um valor (nao ponteiro): lido como uint32
                                        w4b.push_back({ w->matIdx + sizeof(int) * w->idx, 4, &w->idx });
                                }
                                RunWave(w4b);
                        }

                        // w5+: um nivel de hierarquia por onda (todas as entidades juntas)
                        for (int level = 0; level < 60; level++)
                        {
                                bool any = false;

                                for (auto* w : walkers)
                                        if (w->ok && (int)w->idx >= 0 && w->iters < 60)
                                                any = true;

                                if (!any) break;

                                struct TmReq { WalkSt* w; };
                                std::vector<TmReq> lvl;

                                for (auto* w : walkers)
                                {
                                        if (!w->ok || (int)w->idx < 0 || w->iters >= 60)
                                                continue;

                                        lvl.push_back({ w });
                                }

                                // TMatrix + proximo indice: bases distintas (matrix_list /
                                // matrix_indices), podem ir na MESMA onda
                                for (auto& r : lvl)
                                {
                                        wave.push_back({ r.w->matList + sizeof(TMatrix) * r.w->idx, sizeof(TMatrix), &r.w->tm });
                                }
                                // proximo indice depois (precisa do TMatrix? nao: o proximo
                                // indice depende so do indice atual, que ja temos)
                                std::vector<BatchReq> nextIdx;
                                for (auto& r : lvl)
                                        nextIdx.push_back({ r.w->matIdx + sizeof(int) * r.w->idx, 4, &r.w->idx });

                                RunWave(wave);
                                RunWave(nextIdx);

                                // compor localmente (mesma matematica do get_position_Injected)
                                for (auto& r : lvl)
                                {
                                        WalkSt* w = r.w;
                                        TMatrix tm = w->tm;

                                        float rotX = tm.Rotation.x;
                                        float rotY = tm.Rotation.y;
                                        float rotZ = tm.Rotation.z;
                                        float rotW = tm.Rotation.w;

                                        float scaleX = w->acc.X * tm.Scale.x;
                                        float scaleY = w->acc.Y * tm.Scale.y;
                                        float scaleZ = w->acc.Z * tm.Scale.z;

                                        Vector3 next;
                                        next.X = tm.Position.x + scaleX + ( scaleX * ( ( rotY * rotY * -2.0 ) - ( rotZ * rotZ * 2.0 ) ) ) + ( scaleY * ( ( rotW * rotZ * -2.0 ) - ( rotY * rotX * -2.0 ) ) ) + ( scaleZ * ( ( rotZ * rotX * 2.0 ) - ( rotW * rotY * -2.0 ) ) );
                                        next.Y = tm.Position.y + scaleY + ( scaleX * ( ( rotX * rotY * 2.0 ) - ( rotW * rotZ * -2.0 ) ) ) + ( scaleY * ( ( rotZ * rotZ * -2.0 ) - ( rotX * rotX * 2.0 ) ) ) + ( scaleZ * ( ( rotW * rotX * -2.0 ) - ( rotZ * rotY * -2.0 ) ) );
                                        next.Z = tm.Position.z + scaleZ + ( scaleX * ( ( rotW * rotY * -2.0 ) - ( rotX * rotZ * -2.0 ) ) ) + ( scaleY * ( ( rotY * rotZ * 2.0 ) - ( rotW * rotX * -2.0 ) ) ) + ( scaleZ * ( ( rotX * rotX * -2.0 ) - ( rotY * rotY * 2.0 ) ) );

                                        w->acc = next;
                                        w->iters++;
                                }
                        }

                        for (auto& e : ents)
                        {
                                e.feetPos = (e.feetWalk.ok && e.feetWalk.acc != Vector3::Zero( ))
                                        ? e.feetWalk.acc
                                        : Vector3::Zero( );

                                if (e.headPos == Vector3::Zero( ) && e.headWalk.ok)
                                        e.headPos = (e.headWalk.acc != Vector3::Zero( ))
                                                ? e.headWalk.acc
                                                : Vector3::Zero( );
                        }

                        // ---------- montagem (mesma semantica do loop antigo) ----------
                        for (auto& e : ents)
                        {
                                if (e.headPos == Vector3::Zero( )) { entPos++; continue; }
                                if (e.feetPos == Vector3::Zero( )) { entPos++; continue; }

                                bool IsKnocked = (e.shadow != 0 && e.pose == 8);
                                int CurrentHealth = e.hpCur;
                                int MaxHealth = e.hpMax;
                                float HealthPercent = (float)CurrentHealth / (float)MaxHealth;

                                int WeaponID = -1;
                                if (e.weaponPtr != 0)
                                        WeaponID = e.weaponId;

                                std::string nameStr = XorStr("BOT");
                                if (!e.isBot && e.nick != 0 && e.nameLen > 0 && e.nameLen <= 44)
                                {
                                        std::string converted = Utf16ToStd(e.nameChars, e.nameLen);
                                        if (!converted.empty())
                                                nameStr = converted;
                                }

                                Vector3 PosHeadEntity = e.headPos;
                                Vector3 PosEntity = e.feetPos;

                                float Distancia = (MainPos != Vector3::Zero( ))
                                        ? Vector3::Distance(PosEntity, MainPos)
                                        : 0.0f;

                                Vector3 HeadWorld = PosHeadEntity + (Vector3::Up( ) * 0.20f);
                                Vector3 FeetWorld = PosEntity + (Vector3::Down( ) * 0.1f);

                                Vector3 HeadPos = W2S::World2Screen(ViewMatrix, HeadWorld);
                                Vector3 EntityPos = W2S::World2Screen(ViewMatrix, FeetWorld);
                                bool projOk = (ScreenWidth > 0 && ScreenHeight > 0 && HeadPos.Z > 0 && EntityPos.Z > 0);

                                PlayerData pd;
                                pd.HeadScreen = projOk ? HeadPos : Vector3::Zero( );
                                pd.FeetScreen = projOk ? EntityPos : Vector3::Zero( );
                                pd.HeadWorld = HeadWorld;
                                pd.FeetWorld = FeetWorld;
                                pd.HealthPercent = HealthPercent;
                                pd.IsKnocked = IsKnocked;
                                pd.IsTeammate = (e.isTeam != 0);
                                pd.WeaponID = WeaponID;
                                pd.Entity = e.Entity;
                                pd.UMAData = e.umaData;
                                pd.Name = nameStr;
                                pd.Distance = Distancia;
                                pd.CurrentHealth = (short)CurrentHealth;
                                pd.MaxHealth = (short)MaxHealth;
                                pd.LastSeenTick = GetTickCount64( );
                                tempPlayers.push_back(pd);
                                entOk++;
                        }

                        // Resumo de descarte (rate-limit 5s). Se o ESP nao aparece e a
                        // cadeia esta OK, ESTE log mostra qual filtro esta comendo tudo.
                        static LONGLONG s_LastEntSummary = 0;
                        if ( GetTickCount64( ) - s_LastEntSummary > 5000 )
                        {
                                s_LastEntSummary = GetTickCount64( );
                                DiagLog( "[ENTITY] lista=%d ok=%d nulo=%d | descartados: local=%d classe=%d avatar=%d team=%d hp=%d pos=%d",
                                        dictCount, entOk, entNulo, entLocal, entClasse, entAvatar,
                                        entTeam, entHp, entPos );
                        }

                        // Heartbeat de sucesso da busca inicial: prova que a cadeia
                        // inteira (GameFacade -> ... -> entidades) leu limpo neste frame.
                        ChainOkLog( dictCount, static_cast< int >( MatchState ), IsObserving );

                        fresh = true;
                }
                while ( false );

                if ( fresh )
                {
                        lobbyFrames = 0;
                        lobbyStartMs = 0;

                        // ==== carry-over de entidades (anti-flicker) ====
                        // A entidade fica presa pelo ponteiro e falha transitória NÃO
                        // derruba o ESP. Uma entidade que passou dos checks fundamentais
                        // deste frame (está na lista oficial e é inimiga) mas caiu no
                        // meio da leitura herda o ÚLTIMO ESTADO BOM do snapshot.
                        //
                        // A entidade SÓ cai de verdade quando:
                        //  - não aparece em seenThisFrame (saiu da lista de ataque);
                        //  - a re-leitura de vida dá <= 0 (morreu);
                        //  - passou da janela de validade (3s sem nenhuma leitura boa).
                        {
                                LONGLONG nowCarry = GetTickCount64( );
                                std::vector<PlayerData> carried;
                                carried.reserve( m_Players.size( ) / 2 );
                                std::lock_guard<std::mutex> lock( m_Mutex );
                                for ( const auto& prev : m_Players )
                                {
                                        if ( nowCarry - prev.LastSeenTick > 3000 )
                                                continue;
                                        if ( seenThisFrame.find( prev.Entity ) == seenThisFrame.end( ) )
                                                continue;
                                        bool alreadyIn = false;
                                        for ( const auto& np : tempPlayers )
                                        {
                                                if ( np.Entity == prev.Entity )
                                                {
                                                        alreadyIn = true;
                                                        break;
                                                }
                                        }
                                        if ( alreadyIn )
                                                continue;
                                        // Não ressuscita morto: re-lê a vida (mesma cadeia do loop).
                                        // Se voltar <= 0 a entidade morreu e cai no próximo frame.
                                        uintptr_t priPool = N32 ? g_FreeFireMemory.Read<uint32_t>( prev.Entity + Offsets::ReplicationEntity::m_PRIDataPool ) : g_FreeFireMemory.Read<uint64_t>( prev.Entity + Offsets::ReplicationEntity::m_PRIDataPool );
                                        uintptr_t arrPtr = ( priPool != 0 ) ? ( N32 ? g_FreeFireMemory.Read<uint32_t>( priPool + Offsets::ReplicationEntity::m_Datas ) : g_FreeFireMemory.Read<uint64_t>( priPool + Offsets::ReplicationEntity::m_Datas ) ) : 0;
                                        uintptr_t hPtr = ( arrPtr != 0 ) ? ( N32 ? g_FreeFireMemory.Read<uint32_t>( arrPtr + Offsets::ReplicationEntity::HealthCurrentPtr ) : g_FreeFireMemory.Read<uint64_t>( arrPtr + Offsets::ReplicationEntity::HealthCurrentPtr ) ) : 0;
                                        int h = ( hPtr != 0 ) ? g_FreeFireMemory.Read<int>( hPtr + Offsets::ReplicationEntity::Value ) : -1;
                                        if ( h <= 0 )
                                                continue;
                                        carried.push_back( prev );
                                }
                                for ( const auto& c : carried )
                                        tempPlayers.push_back( c );
                        }

                        std::lock_guard<std::mutex> lock( m_Mutex );
                        // Contexto/camera sempre atualiza — mesmo quando o snapshot e
                        // segurado, o ESP reprojeta as posicoes de mundo com a camera atual.
                        m_Context = tempCtx;
                        if ( tempPlayers.empty( ) && !m_Players.empty( ) )
                        {
                                // Um frame "fresco" que veio vazio NAO pode apagar o ESP.
                                // So aceita o vazio apos uma janela sustentada (25s reais).
                                LONGLONG nowEmpty = GetTickCount64( );
                                if ( emptyStartMs == 0 )
                                        emptyStartMs = nowEmpty;
                                if ( nowEmpty - emptyStartMs > 25000 )
                                {
                                        // JA estamos sob o lock de m_Mutex acima; relock do
                                        // mesmo mutex nao recursivo aqui = deadlock.
                                        LONGLONG elapsedEmpty = nowEmpty - emptyStartMs;
                                        emptyStartMs = 0;
                                        emptyFrames = 0;
                                        DiagLog( "[diag] empty-clear: snapshot apagado apos %lldms de frames vazios frescos", ( long long )elapsedEmpty );
                                        m_Players.swap( tempPlayers );
                                        m_SnapshotFresh = true;
                                        m_LastFreshTick.store( nowEmpty );
                                }
                                else
                                {
                                        m_SnapshotFresh = false;
                                }
                        }
                        else
                        {
                                emptyStartMs = 0;
                                emptyFrames = 0;
                                m_Players.swap( tempPlayers );
                                m_SnapshotFresh = true;
                                m_LastFreshTick.store( GetTickCount64( ) );
                        }
                }
                else if ( readMatchState && !matchActive )
                {
                        // m_State fora de [1..3] NUNCA limpa nada. So marca nao-fresco.
                        lobbyFrames = 0;
                        lobbyStartMs = 0;
                        std::lock_guard<std::mutex> lock( m_Mutex );
                        m_SnapshotFresh = false;
                }
                else
                {
                        // Falha de leitura: mantém o ultimo snapshot congelado.
                        // So limpa apos 30s reais sem Match (lobby real / jogo fechado).
                        if ( !matchRead )
                        {
                                LONGLONG nowLobby = GetTickCount64( );
                                if ( lobbyStartMs == 0 )
                                        lobbyStartMs = nowLobby;
                                if ( nowLobby - lobbyStartMs > 30000 )
                                {
                                        LONGLONG elapsedLobby = nowLobby - lobbyStartMs;
                                        lobbyStartMs = 0;
                                        lobbyFrames = 0;
                                        DiagLog( "[diag] lobby-clear: snapshot+contexto zerados apos %lldms sem Match", ( long long )elapsedLobby );
                                        std::lock_guard<std::mutex> lock( m_Mutex );
                                        m_Players.clear( );
                                        m_Context = GameContext{ };
                                        m_SnapshotFresh = false;
                                }
                                else
                                {
                                        std::lock_guard<std::mutex> lock( m_Mutex );
                                        m_SnapshotFresh = false;
                                }
                        }
                        else
                        {
                                lobbyStartMs = 0;
                                lobbyFrames = 0;
                                std::lock_guard<std::mutex> lock( m_Mutex );
                                m_SnapshotFresh = false;
                                // Se a leitura chegou ate a camera/view matrix antes de falhar,
                                // atualiza o contexto com ela — o ESP reprojeta as posicoes de
                                // mundo congeladas com a camera mais recente.
                                if ( tempCtx.ViewMatrix.m [ 0 ][ 0 ] != 0.f )
                                        m_Context = tempCtx;
                        }
                }
                }
                catch ( const std::exception& ex )
                {
                        DiagLog( "[diag] ReadLoop exception: %s", ex.what( ) );
                }
                catch ( ... )
                {
                        DiagLog( "[diag] ReadLoop exception (unknown)" );
                }

                /*
                 * STEALTH DE LEITURA — antes: 1ms (≈1000 varreduras/segundo,
                 * cada uma com ~20-25 round-trips de batch = volume gigante
                 * em cadência de metrônomo perfeito; era ISSO que o
                 * anti-cheat detectava em ~5 minutos). Agora: intervalo
                 * configurável (padrão 60ms ≈ 16Hz — ESP continua fluida)
                 * com jitter aleatório de ±20% quando StealthRead ligado,
                 * pra não formar padrão periódico.
                 */
                {
                        int readInterval = g_Globals.General.ReadIntervalMs;
                        if ( readInterval < 15 ) readInterval = 15;
                        if ( readInterval > 1000 ) readInterval = 1000;

                        if ( g_Globals.General.StealthRead )
                        {
                                static unsigned int s_JitterSeed = 12345u;
                                s_JitterSeed = s_JitterSeed * 1664525u + 1013904223u;
                                const int jitterPct = ( int )( ( s_JitterSeed >> 16 ) % 41u ) - 20;   // -20..+20
                                readInterval = readInterval * ( 100 + jitterPct ) / 100;
                        }

                        std::this_thread::sleep_for( std::chrono::milliseconds( readInterval ) );
                }
        }
}

template void Data::ReadLoop<true, false>( );    // v24.1 32-bit
template void Data::ReadLoop<false, false>( );   // v24.1 64-bit
template void Data::ReadLoop<true, true>( );     // v31 32-bit
template void Data::ReadLoop<false, true>( );    // v31 64-bit

GameContext Data::GetContext( )
{
        std::lock_guard<std::mutex> lock( m_Mutex );
        return m_Context;
}

void Data::Draw( int width, int height, bool N32, bool V31 )
{
        Memory::FlushTLB( );

        /*
         * ============================================================
         * INIT/DETECCAO — roda ANTES do gate de EnableFuncs.
         *
         * Antes: sem login (EnableFuncs=0) isso aqui retornava na hora
         * e NADA inicializava — nem PID, nem ponte, nem v7a/v8a.
         * Agora: procurar o jogo, conectar na ponte e detectar a
         * arquitetura acontece assim que o painel sobe, com FF aberto
         * ou nao. O gate continua valendo para ESP/aim/exploits.
         * ============================================================
         */
        if ( Offsets::LibIl2Cpp == 0 )
        {
                g_FreeFireMemory.RestartAsync( );
                StartReadThread( );

                static LONGLONG lastInitLog = 0;
                LONGLONG nowI = GetTickCount64( );
                if ( nowI - lastInitLog > 1000 )
                {
                        lastInitLog = nowI;
                        DiagLog( "[init] base==0: procurando jogo via ponte (EnableFuncs=%d)", ( int )g_Globals.General.EnableFuncs );
                }
        }
        else if ( !m_Running.load( ) )
        {
                /*
                 * Base valida mas a thread de leitura nunca existiu
                 * (StartReadThread so era chamado no watchdog, que por
                 * sua vez exigia m_ThreadValid=true — ou seja, nunca
                 * subia). Sobe agora.
                 */
                StartReadThread( );
        }

                // BYPASS pra sem auth
                g_Globals.General.EnableFuncs = 1;
        if ( g_Globals.General.EnableFuncs == 0 )
        {
                static LONGLONG lastGateLog = 0;
                LONGLONG nowG = GetTickCount64( );
                if ( nowG - lastGateLog > 2000 )
                {
                        lastGateLog = nowG;
                        DiagLog( "[gate] EnableFuncs=0 (faca login no painel) - ESP/aim/exploits pausados" );
                }
                return;
        }

        // Globals de projecao atualizados ANTES de qualquer desenho (inclusive o
        // path de base==0 abaixo) — W2S nunca usa dimensoes de um frame antigo.
        ScreenWidth = width;
        ScreenHeight = height;

        // Corpo inteiro protegido: uma excecao (bad_alloc do snapshot, leitura
        // lixo) nao pode derrubar o frame de render — sem isso o overlay inteiro
        // congela e a ESP morre junto. Captura e segue o proximo frame.
        try
        {

        // Sempre que a base/contexto nao permitir leituras vivas, desenha o overlay
        // do ultimo snapshot congelado (posicoes de tela) para o ESP nunca sumir.
        // O skeleton e desativado nesse caminho pois precisa de memoria ao vivo.
        // liveMatrix: view matrix atual (releitura ao vivo ou ultima boa). Se
        // valida, reprojeta os mundos do snapshot — o ESP SEGUE a camera mesmo
        // sem leitura fresca, em vez de ficar grudado na tela.
        auto DrawFrozenEsp = [ ] ( const Matrix4x4& liveMatrix )
        {
                // Master "ESP Player": com o toggle desligado nada do overlay congelado
                // e desenhado (aim/silent nao dependem deste caminho).
                if ( !g_Globals.Visuals.ESP.Enabled ) return;

                // Snapshot velho NAO para o desenho: o snapshot congelado continua
                // sendo exibido (reprojetado com a camera ao vivo, se houver) ate a
                // leitura renovar — antes, >3s sem leitura fresca zerava a tela no
                // meio da partida (engasgo do emulador/CR3) e o ESP so voltava quando
                // a leitura voltava (ou nem voltava). A limpeza de verdade (partida
                // encerrada) continua sendo feita pelo lobby-clear de 30s no ReadLoop.

                const auto& ESPc = g_Globals.Visuals.ESP;
                ImDrawList* DLc = ImGui::GetForegroundDrawList( );

                std::vector<PlayerData> frozen;
                {
                        std::lock_guard<std::mutex> lock( m_Mutex );
                        frozen = m_Players;
                }

                for ( const auto& p : frozen )
                        DrawEspEntityOverlay( p, DLc, ESPc, liveMatrix, false, false, false );
        };

        if ( Offsets::LibIl2Cpp == 0 )
        {
                // Base ainda nao achada (RestartAsync ja foi acionado no
                // bloco pre-gate acima, single-flight). Desenha o congelado.
                DrawFrozenEsp( Data::GetContext( ).ViewMatrix );
                return;
        }

        auto ReadPtr = [ N32 ] ( uintptr_t addr ) -> uintptr_t
        {
                return N32 ? g_FreeFireMemory.Read<uint32_t>( addr ) : g_FreeFireMemory.Read<uint64_t>( addr );
        };

        auto WritePtr = [ N32 ] ( uintptr_t addr, uintptr_t val )
        {
                N32 ? g_FreeFireMemory.Write<uint32_t>( addr, ( uint32_t )val ) : g_FreeFireMemory.Write<uint64_t>( addr, ( uint64_t )val );
        };

        const auto& ESP = g_Globals.Visuals.ESP;
        const auto& AimCfg = g_Globals.AimBot;
        const float fovSq = AimCfg.Fov * AimCfg.Fov;
        const float silentFovSq = g_Globals.Silent.Fov * g_Globals.Silent.Fov;

        /* Bias de distancia do silent (px^2 por m^2): inimigo a 50m custa
         * +625 "px^2" no score — empata com ~25px de erro de mira. Leve:
         * nao substitui a mira, so faz o PERTINHO ganhar duvidas. */
        constexpr float kSilentDistBiasPx2PerM2 = 0.25f;

        ImDrawList* DL = ImGui::GetForegroundDrawList( );
        const ImVec2 screenCenter( ( float )ScreenWidth * 0.5f, ( float )ScreenHeight * 0.5f );

        float ClosestDistSq = FLT_MAX;
        uintptr_t ClosestEntity = 0;
        short ClosestHP = 0;

        // Alvo do silent: selecao independente, com FOV/distancia proprios
        // (nao herda nada do AimCfg).
        float SilentDistSq = FLT_MAX;
        uintptr_t SilentClosestEntity = 0;

        // Nivel VISIVEL (oraculo do auto-lock do jogo): ganha do nivel geral.
        float SilentVisDistSq = FLT_MAX;
        uintptr_t SilentVisibleEntity = 0;

        int enemyCountFrame = 0;
        ImVec2 closestHead2D( 0.f, 0.f );
        float minDistance2Dsq = FLT_MAX;

        int _lastEnemyCount = -1;
        std::string _cachedEnemyText = XorStr( "Enemies Detected: 0" );

        static Matrix4x4 CurrentMatrix;

        static uintptr_t BS_LastTarget = 0;
        static uintptr_t BS_SavedNeck = 0;
        static uintptr_t BS_SavedHip = 0;
        static uintptr_t BS_NeckAddr = 0;
        static uintptr_t BS_HipAddr = 0;
        static bool BS_Applied = false;
        static bool BS_ActiveByCursor = false;

        auto BS_Restore = [ & ] ( )
        {
                if ( BS_Applied && BS_LastTarget && BS_NeckAddr && BS_HipAddr )
                {
                        WritePtr( BS_NeckAddr, BS_SavedNeck );
                        WritePtr( BS_HipAddr, BS_SavedHip );
                }
                BS_Applied = false;
                BS_LastTarget = 0;
                BS_SavedNeck = 0;
                BS_SavedHip = 0;
                BS_NeckAddr = 0;
                BS_HipAddr = 0;
        };

        auto BS_Apply = [ & ] ( uintptr_t ent ) -> bool
        {
                if ( ent == 0 ) return false;

                uintptr_t neckAddr = ent + Offsets::Player::m_HipNode;
                uintptr_t hipAddr = ent + Offsets::Player::m_BloodEffectNode;
                if ( g_Globals.AimBot.Target == 1 )
                        hipAddr = ent + Offsets::Player::m_RightArmNode;

                uintptr_t neckVal = ReadPtr( neckAddr );
                uintptr_t hipVal = ReadPtr( hipAddr );
                if ( neckVal == 0 || hipVal == 0 )
                {
                        // Sinal de diagnostico: offsets nao resolveram ponteiro
                        static LONGLONG lastBsNull = 0;
                        const LONGLONG nowBsNull = ( LONGLONG )GetTickCount64( );
                        if ( nowBsNull - lastBsNull > 5000 )
                        {
                                lastBsNull = nowBsNull;
                                DiagLog( "[aimbot] boneswap: neck=0x%lX hip=0x%lX invalidos (ent=0x%lX)",
                                        ( unsigned long )neckVal, ( unsigned long )hipVal, ( unsigned long )ent );
                        }
                        return false;
                }

                BS_NeckAddr = neckAddr;
                BS_HipAddr = hipAddr;
                BS_SavedNeck = neckVal;
                BS_SavedHip = hipVal;

                WritePtr( neckAddr, hipVal );
                WritePtr( hipAddr, neckVal );

                BS_LastTarget = ent;
                BS_Applied = true;

                // SINAL NO LOG: confirma que o aimbot aplicou a troca de ossos.
                static LONGLONG lastBsApplyLog = 0;
                const LONGLONG nowBs = ( LONGLONG )GetTickCount64( );
                if ( nowBs - lastBsApplyLog > 3000 )
                {
                        lastBsApplyLog = nowBs;
                        DiagLog( "[aimbot] boneswap APLICADO em ent=0x%lX (neck<->hip trocados)", ( unsigned long )ent );
                }
                return true;
        };

        if ( !AimCfg.Enabled && BS_Applied )
        {
                BS_Restore( );
                BS_ActiveByCursor = false;
        }

        /* Se o perfil nao preencheu GameVarDef_TypeInfo (ex: v8a vazio),
    * pula — sem isso lia o proprio ELF (0x464C457F) como TypeInfo. */
    uintptr_t GameVar_TI = ( Offsets::GameVarDef::GameVarDef_TypeInfo != 0 )
                ? ReadPtr( Offsets::LibIl2Cpp + Offsets::GameVarDef::GameVarDef_TypeInfo ) : 0;
    uintptr_t GameVar = ( GameVar_TI != 0 )
                ? ReadPtr( GameVar_TI + Offsets::AccessClass ) : 0;

        // --- BugarPixel ---
        if ( GameVar != 0 )
        {
                static bool lastBugarPixelState = false;
                float currentValue = g_FreeFireMemory.Read<float>( GameVar + Offsets::GameVarDef::ShootTraceAdjustmentDistanceThreshold );
                if ( g_Globals.Misc.Exploits.LocalPlayer.BugarPixel )
                {
                        if ( currentValue != 0.0f )
                                g_FreeFireMemory.Write<float>( GameVar + Offsets::GameVarDef::ShootTraceAdjustmentDistanceThreshold, 0.0f );
                }
                else
                {
                        if ( lastBugarPixelState )
                                g_FreeFireMemory.Write<float>( GameVar + Offsets::GameVarDef::ShootTraceAdjustmentDistanceThreshold, 1.5f );
                }
                lastBugarPixelState = g_Globals.Misc.Exploits.LocalPlayer.BugarPixel;

                // --- Precision ---
                static bool lastPrecisionState = false;
                if ( g_Globals.Misc.Exploits.LocalPlayer.Precision )
                {
                        float curRotMin = g_FreeFireMemory.Read<float>( GameVar + Offsets::GameVarDef::RotationSensitivityMin );
                        float curRotMax = g_FreeFireMemory.Read<float>( GameVar + Offsets::GameVarDef::RotationSensitivityMax );
                        float curAimMin = g_FreeFireMemory.Read<float>( GameVar + Offsets::GameVarDef::AimRotationSensitivityMin );
                        float curAimMax = g_FreeFireMemory.Read<float>( GameVar + Offsets::GameVarDef::AimRotationSensitivityMax );

                        if ( curRotMin != 15.0625f )
                                g_FreeFireMemory.Write<float>( GameVar + Offsets::GameVarDef::RotationSensitivityMin, 15.0625f );
                        if ( curRotMax != 3.40939e-05f )
                                g_FreeFireMemory.Write<float>( GameVar + Offsets::GameVarDef::RotationSensitivityMax, 3.40939e-05f );
                        if ( curAimMin != 15.125f )
                                g_FreeFireMemory.Write<float>( GameVar + Offsets::GameVarDef::AimRotationSensitivityMin, 15.125f );
                        if ( curAimMax != 3184.0f )
                                g_FreeFireMemory.Write<float>( GameVar + Offsets::GameVarDef::AimRotationSensitivityMax, 3184.0f );
                }
                else
                {
                        if ( lastPrecisionState )
                        {
                                g_FreeFireMemory.Write<float>( GameVar + Offsets::GameVarDef::RotationSensitivityMin, 15.0f );
                                g_FreeFireMemory.Write<float>( GameVar + Offsets::GameVarDef::RotationSensitivityMax, 35.0f );
                                g_FreeFireMemory.Write<float>( GameVar + Offsets::GameVarDef::AimRotationSensitivityMin, 10.0f );
                                g_FreeFireMemory.Write<float>( GameVar + Offsets::GameVarDef::AimRotationSensitivityMax, 20.0f );
                        }
                }
                lastPrecisionState = g_Globals.Misc.Exploits.LocalPlayer.Precision;

                // --- BackJump ---
                static bool LastBackJumpState = false;
                bool AccelerationOnFallingValue = g_FreeFireMemory.Read<bool>( GameVar + Offsets::GameVarDef::EnableAccelerationOnFalling );
                bool FallingSwapWeaponValue = g_FreeFireMemory.Read<bool>( GameVar + Offsets::GameVarDef::EnableLowFallingSwapWeapon );
                if ( g_Globals.Misc.Exploits.LocalPlayer.BackJump )
                {
                        if ( AccelerationOnFallingValue == true || FallingSwapWeaponValue == false )
                        {
                                g_FreeFireMemory.Write<bool>( GameVar + Offsets::GameVarDef::EnableAccelerationOnFalling, false );
                                g_FreeFireMemory.Write<float>( GameVar + Offsets::GameVarDef::EnableLowFallingSwapWeapon, true );
                        }
                }
                else
                {
                        if ( LastBackJumpState )
                        {
                                g_FreeFireMemory.Write<bool>( GameVar + Offsets::GameVarDef::EnableAccelerationOnFalling, true );
                                g_FreeFireMemory.Write<float>( GameVar + Offsets::GameVarDef::EnableLowFallingSwapWeapon, true );
                        }
                }
                LastBackJumpState = g_Globals.Misc.Exploits.LocalPlayer.BackJump;
        }

        // ==================== Snapshot ====================

        std::vector<PlayerData> snapshot;
        GameContext ctx;
        bool snapshotFresh = false;
        {
                std::lock_guard<std::mutex> lock( m_Mutex );
                snapshot = m_Players;
                ctx = m_Context;
                snapshotFresh = m_SnapshotFresh;
        }

        // ==================== Watchdog de recuperacao (ESP nunca desliga) ====================
        // Se o snapshot nao fica fresco por ~2s, o processo do jogo provavelmente
        // reiniciou ou o CR3 envelheceu. Acoes, sem depender do usuario:
        //   < 6s  -> RefreshCR3 (barato, 1x/seg)
        //   >= 6s -> RestartAsync (re-localiza a base; single-flight + cooldown)
        // Se a thread de leitura morreu (crash fora do alcance do try/catch do
        // ReadLoop), recria a thread aqui — a ESP se recupera sozinha.
        if ( !snapshotFresh )
        {
                LONGLONG staleMs = GetTickCount64( ) - m_LastFreshTick.load( );
                if ( staleMs > 1500 )
                {
                        static LONGLONG lastWatchdogAct = 0;
                        LONGLONG nowWd = GetTickCount64( );
                        if ( nowWd - lastWatchdogAct > 1000 )
                        {
                                lastWatchdogAct = nowWd;
                                if ( staleMs > 4000 )
                                {
                                        DiagLog( "[diag] watchdog: %lldms sem leitura fresca — restart forcado", ( long long )staleMs );
                                        g_FreeFireMemory.RestartAsync( );
                                }
                                else
                                {
                                        Memory::RefreshCR3( );
                                }
                        }
                        if (m_ThreadValid && !m_Running.load())
                        {
                                pthread_join(
                                        m_ThreadHandle,
                                        nullptr
                                );

                                m_ThreadValid = false;

                                DiagLog(
                                        "[diag] watchdog: thread de leitura morta — recriando"
                                );

                                StartReadThread();
                        }
                }
        }

        uintptr_t localPlayer = ctx.LocalPlayer;
        uintptr_t MainCamera = ctx.MainCamera;
        Matrix4x4 ViewMatrix = ctx.ViewMatrix;
        bool IsObserving = ctx.IsObserving;

        // ==================== View Matrix ao vivo (espelho do FF) ====================
        // O cheat de referencia relê a view matrix a cada frame no loop de desenho;
        // por isso o ESP dele reprojeta SEMPRE com a câmera atual e nunca fica
        // grudado quando a leitura de entidades engasga. Aqui a releitura usa apenas
        // a cadeia de câmera (MatchGame -> controller -> camera -> cachedPtr) com o
        // mesmo vetor de offsets do ReadLoop; se falhar, fica a matriz do snapshot;
        // se as duas falharem, cai no caminho congelado abaixo.
        if ( ctx.MatchGame != 0 )
        {
                uintptr_t ccm = ReadPtr( ctx.MatchGame + Offsets::MatchGame::m_CameraControllerManager );
                if ( ccm != 0 )
                {
                        uintptr_t cam = ReadPtr( ccm + Offsets::CameraControllerManager::m_Camera );
                        if ( cam != 0 )
                        {
                                uintptr_t cached = ReadPtr( cam + Offsets::Camera::m_CachedPtr );
                                if ( cached != 0 )
                                {
                                        Matrix4x4 live = g_FreeFireMemory.Read<Matrix4x4>( cached + Offsets::Camera::ViewMatrix );
                                        if ( IsValidViewMatrix( live ) )
                                        {
                                                ViewMatrix = live;
                                                {
                                                        std::lock_guard<std::mutex> lock( m_Mutex );
                                                        m_Context.ViewMatrix = live;
                                                }
                                        }
                                }
                        }
                }
        }

        if ( localPlayer == 0 || !IsValidViewMatrix( ViewMatrix ) )
        {
                // Contexto invalido (transicao de partida/lobby/loading): mesmo sem
                // localPlayer/view matrix, desenha o overlay do ultimo snapshot (posicoes
                // de tela congeladas) para o ESP nao desligar do nada. Exploits e aimbot
                // nao rodam nesse caminho.
                static LONGLONG lastFrozenLog = 0;
                LONGLONG now = GetTickCount64( );
                if ( now - lastFrozenLog > 1000 )
                {
                        lastFrozenLog = now;
                        DiagLog( "[diag] frozen: localPlayer=%llx matrixValid=%d snapshot=%d",
                                ( unsigned long long )localPlayer, IsValidViewMatrix( ViewMatrix ) ? 1 : 0, ( int )snapshot.size( ) );
                }
                DrawFrozenEsp( ViewMatrix );
                return;
        }

        Silent::UpdateViewMatrix( ViewMatrix );

        // ==================== Ghost Skeleton Rendering ====================

        if ( ghostSkeletonExists )
        {
                Vector3 world = ghostSkeletonPos;
                Vector3 screen = W2S::World2Screen( ViewMatrix, world );

                if ( W2S::IsOnScreen( screen ) )
                {
                        float size = 10.0f;
                        ImColor color = ImColor( 0.f, 1.f, 1.f, 1.f );

                        DL->AddLine( ImVec2( screen.X, screen.Y - size ), ImVec2( screen.X, screen.Y + size ), color, 2.0f );
                        DL->AddLine( ImVec2( screen.X - size, screen.Y ), ImVec2( screen.X + size, screen.Y ), color, 2.0f );
                }
        }

        // ==================== FastMedkit ====================

        uintptr_t PlayerAttributes = ReadPtr( localPlayer + Offsets::Player::m_Attributes );

        // ==================== Atributar Armas ====================
        if (g_Globals.Misc.Exploits.LocalPlayer.AtributarArma)
        {
                if (PlayerAttributes != 0)
                {
                        // Níveis interpolados do original(1.0) até o level max(0.75)
                        float fireIntervalLevels[4] = { 0.90f, 0.85f, 0.75f, 0.65f };
                        int level = g_Globals.Misc.Exploits.LocalPlayer.AtributarArmaLevel;
                        if (level < 0 || level > 3) level = 0;
                        
                        g_FreeFireMemory.Write<float>(PlayerAttributes + Offsets::PlayerAttributes::m_FireIntervalScale, fireIntervalLevels[level]);
                }
        }
        else
        {
                if (PlayerAttributes != 0)
                {
                        g_FreeFireMemory.Write<float>(PlayerAttributes + Offsets::PlayerAttributes::m_FireIntervalScale, 1.0f);
                }
        }
        
        // ==================== Ghost Toggle ====================

        bool ghostKeyHeld = AimTriggerHeld( g_Globals.AimBot.ghostkey, g_Globals.AimBot.ghost );
        if ( g_Globals.AimBot.ghost )
        {
                if ( ghostKeyHeld && !ghostActive )
                {
                        ghostActive = true;
                        g_FreeFireMemory.Write<bool>( localPlayer + Offsets::Player::m_WaitForForceSync, true );
                        ghostSkeletonPos = Transform::GetPosition( localPlayer, N32 );
                        ghostSkeletonExists = true;
                }
                else if ( !ghostKeyHeld && ghostActive )
                {
                        ghostActive = false;
                        g_FreeFireMemory.Write<bool>( localPlayer + Offsets::Player::m_WaitForForceSync, false );
                        ghostSkeletonExists = false;
                }
        }
        else
        {
                ghostActive = false;
                ghostSkeletonExists = false;
        }

        /*
         * FIX "SILENT TEM ALGO QUE IMPEDE" (CAUSA RAIZ): a thread do silent
         * NUNCA nasce se Silent::Start() nao e chamado — o toggle ligava, o
         * alvo era selecionado la embaixo, mas nao existia thread nenhuma
         * para consumir o alvo e escrever o RayDir (nem sinal no log). O
         * bootstrap existia no .bak e se perdeu na integracao dos patches.
         * A thread nasce no primeiro frame com o silent ligado (e renasce
         * sozinha se morreu); desligado, ela dorme (3ms) sem escrever nada.
         */
        if ( g_Globals.Silent.Enabled && Silent::g_Running == 0 )
        {
                Silent::Start( );
                DiagLog( "[silent] thread iniciada (Enabled=1, aguardando alvo)" );
        }

        // ==================== TelaParada ====================
        if ( g_Globals.Misc.Exploits.LocalPlayer.telaparada )
        {
                uintptr_t userControl = ReadPtr( localPlayer + Offsets::Player::m_UserControl );
                if ( userControl != 0 )
                {
                        uintptr_t axisDataArray = ReadPtr( userControl + Offsets::UserControlHandler::m_AxisData );
                        if ( axisDataArray != 0 )
                        {
                                uintptr_t moveAxisData = ReadPtr( axisDataArray + ( N32 ? 0x10 : 0x20 ) );
                                if ( moveAxisData != 0 )
                                {
                                        bool isTouched = g_FreeFireMemory.Read<bool>( moveAxisData + Offsets::UserControlHandler::m_IsTouched );
                                        if ( isTouched )
                                        {
                                                g_FreeFireMemory.Write<bool>( userControl + Offsets::UserControlHandler::m_LockFingerInDashArea, false );
                                                g_FreeFireMemory.Write<bool>( userControl + Offsets::UserControlHandler::m_DashByMovingJoystick, true );
                                                g_FreeFireMemory.Write<int>( userControl + Offsets::UserControlHandler::m_FingerInDashArea, 1 );
                                        }
                                        else
                                        {
                                                g_FreeFireMemory.Write<int>( userControl + Offsets::UserControlHandler::m_FingerInDashArea, 0 );
                                        }
                                }
                        }
                }
        }

        // ==================== ESP Rendering Loop ====================

        if ( snapshot.empty( ) )
        {
                Skeleton::ClearCache( );
        }


        const float centerX = ( float )ScreenWidth * 0.5f;
        const float centerY = ( float )ScreenHeight * 0.5f;

        /*
         * ORACULO DE VISIBILIDADE do silent (fix "mira em quem ta atras da
         * parede"): o proprio jogo resolve, a cada frame, qual inimigo esta
         * VISIVEL e perto da mira — o auto-lock publica isso em
         * m_TargetHeuristic (TargetInfo.m_Entity), ja com raycast de parede
         * feito pelo JOGO. 3 reads UMA vez por frame (nao por entidade):
         * quem igualar esse ponteiro esta comprovadamente visivel, custo
         * zero pra gente. 0 = oraculo calado (cai no comportamento de
         * sempre, silent nunca fica sem alvo).
         */
        uintptr_t autoLockTargetEntity = 0;

        if ( g_Globals.Silent.Enabled && localPlayer != 0 )
        {
                uintptr_t aimAssist = ReadPtr( localPlayer + Offsets::Player::m_AimAssist );

                if ( aimAssist != 0 )
                {
                        uintptr_t targetInfo =
                                ReadPtr( aimAssist + Offsets::AimAssistAutoLock::m_TargetHeuristic );

                        if ( targetInfo != 0 )
                                autoLockTargetEntity =
                                        ReadPtr( targetInfo + Offsets::AimAssistAutoLock::m_Entity );
                }
        }

        // ==================== Aimbot/Silent target selection ====================
        // Desacoplado do loop de render: roda mesmo com "ESP Player" (master do
        // Visuals.ESP) desligado, porque silent, boneswap, magnet e rage dependem
        // de ClosestEntity. Só mira quando o snapshot é fresco (leitura do frame
        // atual). Em falha transitória o ESP continua desenhando a posição
        // congelada, mas o aimbot não trava em alvo antigo — evita tiro que
        // "acerta" e não conta dano.
        if ( AimCfg.Enabled || g_Globals.Silent.Enabled || AimCfg.aimmagnect )
        {
                for ( size_t i = 0; i < snapshot.size( ); i++ )
                {
                        const auto& p = snapshot [ i ];

                        // Aliado nunca vira alvo — ShowTeam so afeta o desenho da ESP,
                        // nunca a selecao de alvo do aimbot/silent/magnet.
                        if ( p.IsTeammate ) continue;

                        if ( snapshotFresh && p.Distance >= 0 && p.Distance <= AimCfg.MaxDistance )
                        {
                                enemiesvisible = true;

                                // Visible check
                                if ( g_Globals.AimBot.VisibleCheck )
                                {
                                        bool anyVisible = false;

                                        uintptr_t aimAssist = ReadPtr( localPlayer + Offsets::Player::m_AimAssist );
                                        if ( aimAssist != 0 )
                                        {
                                                uintptr_t targetInfo = ReadPtr( aimAssist + Offsets::AimAssistAutoLock::m_TargetHeuristic );
                                                if ( targetInfo != 0 )
                                                        anyVisible = true;
                                        }

                                        uintptr_t aimAssistSighting = ReadPtr( localPlayer + Offsets::Player::m_AimAssistOnSighting );
                                        if ( aimAssistSighting != 0 )
                                        {
                                                uintptr_t targetInfo = ReadPtr( aimAssistSighting + Offsets::AimAssistAutoLock::m_TargetHeuristic );
                                                if ( targetInfo != 0 )
                                                        anyVisible = true;
                                        }

                                        if ( !anyVisible )
                                        {
                                                enemiesvisible = false;
                                                continue;
                                        }
                                }

                                // Ignore bots and knocked
                                bool IsClientBot = false;
                                g_FreeFireMemory.Read<bool>( p.Entity + Offsets::Player::IsClientBot, IsClientBot );

                                if ( ( !AimCfg.IgnoreKnocked || !p.IsKnocked ) && ( !AimCfg.IgnoreBots || !IsClientBot ) )
                                {
                                        float dx = p.HeadScreen.X - centerX;
                                        float dy = p.HeadScreen.Y - centerY;
                                        float crosshairDistSq = dx * dx + dy * dy;

                                        if ( crosshairDistSq < fovSq && crosshairDistSq < ClosestDistSq )
                                        {
                                                ClosestDistSq = crosshairDistSq;
                                                ClosestEntity = p.Entity;
                                                ClosestHP = p.CurrentHealth;
                                        }
                                }
                        }

                                                /*
                         * Alvo do silent: config propria (Silent.Fov e
                         * Silent.MaxDistance) com FILTRO DE VISIBILIDADE +
                         * preferencia por alvo PERTINHO:
                         *   - visivel (oraculo do auto-lock) ganha de invisivel
                         *     — fim do "silent prefere o la longe atras da parede";
                         *   - dentro do mesmo nivel, o score soma a distancia de
                         *     jogo ao erro de mira (pertinho desempata).
                         */
                        if ( g_Globals.Silent.Enabled && snapshotFresh && p.Distance >= 0 && p.Distance <= g_Globals.Silent.MaxDistance )
                        {
                                /*
                                 * FILTROS PROPRIOS do silent (config da aba):
                                 * derrubado e bot saem da candidatura ANTES de
                                 * qualquer score; VisibleCheck exige o oraculo
                                 * do auto-lock (visivel = m_TargetHeuristic
                                 * apontando pra ele).
                                 */
                                if ( g_Globals.Silent.IgnoreKnocked && p.IsKnocked )
                                        continue;

                                if ( g_Globals.Silent.IgnoreBots )
                                {
                                        bool silIsBot = false;
                                        g_FreeFireMemory.Read<bool>( p.Entity + Offsets::Player::IsClientBot, silIsBot );

                                        if ( silIsBot )
                                                continue;
                                }

                                float sdx = p.HeadScreen.X - centerX;
                                float sdy = p.HeadScreen.Y - centerY;
                                float silentCrosshairDistSq = sdx * sdx + sdy * sdy;

                                if ( silentCrosshairDistSq < silentFovSq )
                                {
                                        const bool silVisible =
                                                ( autoLockTargetEntity != 0 &&
                                                  p.Entity == autoLockTargetEntity );

                                        if ( g_Globals.Silent.VisibleCheck && !silVisible )
                                                continue;

                                        const float gameDist =
                                                ( p.Distance > 0.0f ) ? p.Distance : 0.0f;

                                        const float silScore =
                                                silentCrosshairDistSq +
                                                gameDist * kSilentDistBiasPx2PerM2;

                                        if ( silVisible )
                                        {
                                                if ( silScore < SilentVisDistSq )
                                                {
                                                        SilentVisDistSq = silScore;
                                                        SilentVisibleEntity = p.Entity;
                                                }
                                        }
                                        else if ( silScore < SilentDistSq )
                                        {
                                                SilentDistSq = silScore;
                                                SilentClosestEntity = p.Entity;
                                        }
                                }
                        }
                }
        }

        /*
         * FILTRO DE VISIBILIDADE — decisao final: se o auto-lock do jogo
         * confirmou alguem VISIVEL dentro do FOV do silent, ele ganha de
         * QUALQUER invisivel. Ninguem visivel confirmado = mantem o mais
         * perto da mira (comportamento de sempre, silent nunca sem alvo).
         */
        if ( SilentVisibleEntity != 0 )
                SilentClosestEntity = SilentVisibleEntity;

        // Partida ativa (localPlayer + view matrix validos): desenha o snapshot
        // SEMPRE, mesmo quando a leitura de entidades engasga por segundos — as
        // posicoes de mundo congeladas sao reprojetadas com a camera ao vivo e o
        // ESP segue os inimigos em vez de sumir. O snapshot so desaparece quando o
        // ReadLoop realmente limpa (lobby-clear 1200 frames sem Match / empty-clear
        // 900 frames vazios), ou seja, quando a partida de fato acabou. O watchdog
        // acima recupera a leitura viva em paralelo.
        for ( size_t i = 0; i < snapshot.size( ); i++ )
        {
                const auto& p = snapshot [ i ];

                if ( ESP.RenderDistance > 0 && p.Distance > ESP.RenderDistance ) continue;

                // Master "ESP Player": so controla o desenho. Aimbot/silent ja
                // selecionaram alvo no bloco acima e continuam funcionando.
                if ( !ESP.Enabled ) continue;

                // Isolamento por entidade: uma entidade/skeleton com problema nunca
                // pode abortar o resto do frame — que roda magnet, boneswap, o feed
                // do silent e o rage abaixo. A ESP pula a entidade e segue; aimbot
                // e silent ficam imunes a falha de desenho da ESP (e vice-versa).
                try
                {
                        DrawEspEntityOverlay( p, DL, ESP, ViewMatrix, N32, V31, true );
                }
                catch ( ... )
                {
                        static LONGLONG lastSkipLog = 0;
                        LONGLONG nowSkip = GetTickCount64( );
                        if ( nowSkip - lastSkipLog > 5000 )
                        {
                                lastSkipLog = nowSkip;
                                DiagLog( "[diag] esp: entidade %zu pulada (excecao no desenho)", ( size_t )i );
                        }
                }

                // --- Enemy counter (aliados de time nao contam) ---
                if ( ESP.Enemy && !p.IsTeammate )
                {
                        enemyCountFrame++;
                        float dx = p.HeadScreen.X - centerX;
                        float dy = p.HeadScreen.Y - centerY;
                        float d2 = dx * dx + dy * dy;
                        if ( d2 < minDistance2Dsq && p.Distance < ESP.RenderDistance )
                        {
                                minDistance2Dsq = d2;
                                closestHead2D = ImVec2( p.HeadScreen.X, p.HeadScreen.Y );
                        }
                }
        } // end for entities

        // ==================== Skeleton Cleanup ====================
        {
                std::unordered_set<uintptr_t> activeEntities;
                for ( const auto& p : snapshot )
                        activeEntities.insert( p.Entity );
                Skeleton::CleanupCache( activeEntities );
        }

        // ==================== Watermark ====================

        if ( ESP.Enabled && ESP.Watermark )
        {
                ImGui::PushFont( Fonts::Verdana );

                const char* text =  "STORM CHEATS" ;
                float fontScale = ESP.TextSize / 15.0f;
                float fontSize = Fonts::Verdana->FontSize * fontScale;

                ImVec2 baseSize = ImGui::CalcTextSize( text );
                ImVec2 textSize( baseSize.x * fontScale, baseSize.y * fontScale );
                ImVec2 screenSize = ImGui::GetIO( ).DisplaySize;
                ImVec2 textPos( ( screenSize.x - textSize.x ) * 0.5f, 75.0f );

                ImColor textColor( ESP.WatermarkColor [ 0 ], ESP.WatermarkColor [ 1 ], ESP.WatermarkColor [ 2 ], ESP.WatermarkColor [ 3 ] );
                ImU32 shadowColor = 0xFF000000;

                DL->AddText( Fonts::Verdana, fontSize, ImVec2( textPos.x + 1, textPos.y + 1 ), shadowColor, text );
                DL->AddText( Fonts::Verdana, fontSize, textPos, textColor, text );

                ImGui::PopFont( );
        }

        // ==================== Enemy text ====================

        if ( ESP.Enabled && ESP.Enemy )
        {
                if ( _lastEnemyCount != enemyCountFrame )
                {
                        _cachedEnemyText = XorStr( "Enemies Detected: " ) + std::to_string( enemyCountFrame );
                        _lastEnemyCount = enemyCountFrame;
                }

                ImGui::PushFont( Fonts::Verdana );

                float fontScale = ESP.TextSize / 15.0f;
                float fontSize = Fonts::Verdana->FontSize * fontScale;

                ImVec2 baseSize = ImGui::CalcTextSize( _cachedEnemyText.c_str( ) );
                ImVec2 textSize( baseSize.x * fontScale, baseSize.y * fontScale );
                ImVec2 screenSize = ImGui::GetIO( ).DisplaySize;
                ImVec2 textPos( ( screenSize.x - textSize.x ) * 0.5f, 75.0f + textSize.y + 3.0f );

                ImColor textColor( ESP.EnemyColor [ 0 ], ESP.EnemyColor [ 1 ], ESP.EnemyColor [ 2 ], ESP.EnemyColor [ 3 ] );
                ImU32 shadowColor = 0xFF000000;

                DL->AddText( Fonts::Verdana, fontSize, ImVec2( textPos.x + 1, textPos.y + 1 ), shadowColor, _cachedEnemyText.c_str( ) );
                DL->AddText( Fonts::Verdana, fontSize, textPos, textColor, _cachedEnemyText.c_str( ) );

                ImGui::PopFont( );
        }

        // ==================== Ghost Distance Text ====================

        if ( g_Globals.AimBot.ghost && ghostSkeletonExists )
        {
                Vector3 currentPos = Transform::GetPosition( localPlayer, N32 );
                float ghostDistance = Vector3::Distance( currentPos, ghostSkeletonPos );

                const char* ghostText = ( ghostDistance <= 4.60f ) ? ( "Ghost Damage: ON" ) : ( "Ghost Damage: FAKE" );
                ImColor textColor = ( ghostDistance <= 4.60f ) ? ImColor( 0.0f, 1.0f, 0.0f, 1.0f ) : ImColor( 1.0f, 0.0f, 0.0f, 1.0f );

                ImGui::PushFont( Fonts::Verdana );

                float fontScale = ESP.TextSize / 10.0f;
                float fontSize = Fonts::Verdana->FontSize * fontScale;

                ImVec2 baseSize = ImGui::CalcTextSize( ghostText );
                ImVec2 textSize( baseSize.x * fontScale, baseSize.y * fontScale );
                ImVec2 screenSize = ImGui::GetIO( ).DisplaySize;

                float yBase = 75.0f;
                float yOffset = textSize.y * 1.5f;
                ImVec2 textPos( ( screenSize.x - textSize.x ) * 0.5f, yBase + yOffset );
                textPos.y -= 2.5f;

                ImU32 shadowColor = 0xFF000000;

                DL->AddText( Fonts::Verdana, fontSize, ImVec2( textPos.x + 1, textPos.y + 1 ), shadowColor, ghostText );
                DL->AddText( Fonts::Verdana, fontSize, textPos, textColor, ghostText );

                ImGui::PopFont( );
        }

        // ==================== Magnet Aimbot ====================

        if ( AimCfg.aimmagnect )
        {
                static bool isHolding = false;
                static Vector3 lockedRootPos = { 0.f, 0.f, 0.f };
                static uintptr_t lockedMatrixAddr = 0;
                static std::thread magnetThread;

                /*
                 * Mobile: MagKey == 0 (botão flutuante nunca criado) -> o
                 * checkbox "Pull Player" é o gatilho (antes nunca ativava).
                 */
                const bool keyPressed =
                        AimTriggerHeld( g_Globals.AimBot.MagKey, AimCfg.aimmagnect );

                if ( keyPressed && !isHolding && ClosestEntity != 0 )
                {
                        isHolding = true;

                        Vector3 cameraPos = ( MainCamera != 0 ) ? Transform::get_position_Injected( MainCamera, N32 ) : Vector3::Zero( );
                        Matrix4x4 invVP{ };
                        if ( cameraPos == Vector3::Zero( ) || !MatrixUtils::Invert( ViewMatrix, invVP ) )
                                goto magnet_end;

                        Vector4 nearClip( 0.f, 0.f, 0.f, 1.f );
                        Vector4 farClip( 0.f, 0.f, 1.f, 1.f );

                        Vector4 worldNear4 = MatrixUtils::Multiply( nearClip, invVP );
                        Vector4 worldFar4 = MatrixUtils::Multiply( farClip, invVP );

                        if ( worldNear4.w == 0 || worldFar4.w == 0 )
                                goto magnet_end;

                        Vector3 worldNear( worldNear4.x / worldNear4.w, worldNear4.y / worldNear4.w, worldNear4.z / worldNear4.w );
                        Vector3 worldFar( worldFar4.x / worldFar4.w, worldFar4.y / worldFar4.w, worldFar4.z / worldFar4.w );

                        Vector3 cameraForward;
                        cameraForward.X = worldFar.X - worldNear.X;
                        cameraForward.Y = worldFar.Y - worldNear.Y;
                        cameraForward.Z = worldFar.Z - worldNear.Z;

                        float len = sqrtf( cameraForward.X * cameraForward.X + cameraForward.Y * cameraForward.Y + cameraForward.Z * cameraForward.Z );
                        if ( len > 0.0001f )
                        {
                                cameraForward.X /= len;
                                cameraForward.Y /= len;
                                cameraForward.Z /= len;
                        }
                        else goto magnet_end;

                        Vector3 targetHead = Transform::GetHeadPosition( ClosestEntity, N32 );
                        Vector3 targetRoot = Transform::GetPosition( ClosestEntity, N32 );
                        if ( targetHead == Vector3::Zero( ) || targetRoot == Vector3::Zero( ) )
                                goto magnet_end;

                        float distance = Vector3::Distance( cameraPos, targetHead );
                        if ( distance < 0.3f || distance > 500.f )
                                goto magnet_end;

                        Vector3 desiredHeadPos = cameraPos + ( cameraForward * distance );
                        Vector3 headOffset = targetHead - targetRoot;
                        if ( headOffset == Vector3::Zero( ) )
                                headOffset = Vector3( 0.f, 0.25f, 0.f );

                        lockedRootPos = desiredHeadPos - headOffset;

                        // Resolve transform chain for bone root (ternary 32/64)
                        uintptr_t boneRoot = ReadPtr( ClosestEntity + Offsets::Player::m_HipNode );
                        if ( boneRoot )
                        {
                                uintptr_t transformValue = ReadPtr( boneRoot + Offsets::GetPosWorld::transObj );
                                if ( transformValue )
                                {
                                        uintptr_t transformObj = ReadPtr( transformValue + Offsets::GetPosWorld::transObj );
                                        if ( transformObj )
                                        {
                                                lockedMatrixAddr = ReadPtr( transformObj + Offsets::GetPosWorld::matrix );
                                        }
                                }
                        }

                        // Write position offset: 0x80 for 32bit, 0xB0 for 64bit
                        uintptr_t posWriteOffset = N32 ? 0x80 : 0xB0;

                        if ( lockedMatrixAddr )
                        {
                                magnetThread = std::thread( [ posWriteOffset ] ( )
                                {
                                        #ifdef __ANDROID__
                                        // Android: pthread é usado diretamente.
                                        #else
                                                HANDLE hThread = GetCurrentThread();
                                                SetThreadAffinityMask(hThread, 1 << 0);
                                                SetThreadPriority(
                                                        hThread,
                                                        THREAD_PRIORITY_TIME_CRITICAL
                                                );
                                        #endif

                                        while ( isHolding && lockedMatrixAddr )
                                        {
                                                g_FreeFireMemory.Write<Vector3>( lockedMatrixAddr + posWriteOffset, lockedRootPos );
                                                /*
                                                 * STEALTH DE ESCRITA: 1 microssegundo = até
                                                 * ~1 MILHÃO de writes/segundo agora que a
                                                 * ponte de write funciona — detecção
                                                 * instantânea. 2ms (500/s) segura o magnet
                                                 * com volume ~2000x menor.
                                                 */
                                                std::this_thread::sleep_for( std::chrono::milliseconds( 2 ) );
                                        }
                                } );
                                magnetThread.detach( );
                        }
                }

                if ( !keyPressed && isHolding )
                {
                        isHolding = false;
                        lockedMatrixAddr = 0;
                        lockedRootPos = { 0.f, 0.f, 0.f };
                }

        magnet_end:
                ;
        }

        // ==================== Estado de disparo (Player::UGCStartFiring) ====================
        // O aimbot (BoneSwap/Safe e Rage) so age ENQUANTO o player atira.
        // Grace pos-fogo: 300ms pro Rage (tap de semi-auto nao fica liga/desliga)
        // e 400ms pro BoneSwap (bala em voo ainda precisa do bone trocado).
        static LONGLONG s_LastFireTickMs = 0;
        bool bsFiringActive = false;
        bool rageFiringActive = false;

        if ( AimCfg.Enabled && localPlayer != 0 )
        {
                const bool firingRaw = ReadLocalFiring( localPlayer );
                const LONGLONG nowFireTickMs = ( LONGLONG )GetTickCount64( );

                if ( firingRaw )
                        s_LastFireTickMs = nowFireTickMs;

                bsFiringActive = firingRaw || ( nowFireTickMs - s_LastFireTickMs ) < 400;
                rageFiringActive = firingRaw || ( nowFireTickMs - s_LastFireTickMs ) < 300;
        }

        // ==================== BoneSwap Aimbot ====================

        if ( AimCfg.Enabled && AimCfg.aimtype == 0 )
        {
                /*
                 * Mobile: KeyBind == 0 (botão "Aim" nunca criado) -> o checkbox
                 * "Aimbot" é o gatilho. Antes, ativar só o checkbox deixava
                 * IsKeyPressed(0) == false para sempre e o boneswap nunca aplicava.
                 */
                const bool keyDown = AimTriggerHeld( AimCfg.KeyBind, AimCfg.Enabled );
                const bool cursorVisible = IsCursorVisibleNow( );

                if ( !keyDown || !bsFiringActive )
                {
                        if ( BS_Applied ) BS_Restore( );
                        BS_ActiveByCursor = false;
                }
                else
                {
                        if ( !BS_ActiveByCursor )
                        {
                                if ( cursorVisible == 0 )
                                {
                                        BS_ActiveByCursor = true;
                                }
                        }

                        if ( BS_ActiveByCursor )
                        {
                                if ( ClosestEntity == 0 )
                                {
                                        if ( BS_Applied ) BS_Restore( );
                                }
                                else
                                {
                                        if ( !BS_Applied || BS_LastTarget != ClosestEntity )
                                        {
                                                if ( BS_Applied ) BS_Restore( );
                                                if ( ClosestHP > 0 )
                                                {
                                                        BS_Apply( ClosestEntity );
                                                }
                                                else if ( BS_Applied )
                                                {
                                                        BS_Restore( );
                                                }
                                        }
                                        else
                                        {
                                                if ( ClosestHP <= 0 && BS_Applied )
                                                {
                                                        BS_Restore( );
                                                }
                                        }
                                }
                        }
                }
        }
        else if ( BS_Applied )
        {
                BS_Restore( );
                BS_ActiveByCursor = false;
        }

        // ==================== Silent Aim Target ====================
        // Usa o alvo PRÓPRIO do silent (SilentClosestEntity), selecionado com
        // Silent.Fov/Silent.MaxDistance — independente do aimbot.
        //
        // FIX "SILENT NAO FICA CONSTANTE": quando a ponte satura no combate,
        // o snapshot engasga (snapshotFresh=false por alguns frames) e
        // SilentClosestEntity zera — o ClearTarget derrubava o silent
        // EXATAMENTE no meio do spray. Agora o último alvo válido sobrevive
        // 600ms (keep-alive) até o snapshot voltar a ficar fresco.
        static uintptr_t s_SilentKeepAliveTarget = 0;
        static LONGLONG s_SilentKeepAliveTickMs = 0;

        if ( SilentClosestEntity != 0 )
        {
                s_SilentKeepAliveTarget = SilentClosestEntity;
                s_SilentKeepAliveTickMs = ( LONGLONG )GetTickCount64( );
        }

        uintptr_t silentTargetNow = SilentClosestEntity;

        if ( silentTargetNow == 0 && s_SilentKeepAliveTarget != 0 &&
             ( LONGLONG )GetTickCount64( ) - s_SilentKeepAliveTickMs < 600 )
        {
                silentTargetNow = s_SilentKeepAliveTarget;
        }

        if ( silentTargetNow != 0 && localPlayer != 0 )
                Silent::SetTarget( localPlayer, silentTargetNow );
        else
                Silent::ClearTarget( );

        
        // ==================== Silent Firing Feed ====================
        // Publica o estado de tiro pro Silent::NotifyFiring — 2a fonte do
        // modo burst (a 1a e o IsFiring lido pela feed do silent). Usa a
        // leitura que o Draw ja conhece (ReadLocalFiring), com throttling
        // de 32ms: custo de 31 reads/s, nada pra ponte.
        {
                static LONGLONG s_SilFireLastMs = 0;
                static bool s_SilFireCache = false;

                const LONGLONG nowSilFireMs = ( LONGLONG )GetTickCount64( );

                if ( nowSilFireMs - s_SilFireLastMs >= 32 )
                {
                        s_SilFireLastMs = nowSilFireMs;

                        s_SilFireCache = localPlayer != 0 &&
                                         Offsets::Player::IsFiring != 0 &&
                                         ReadLocalFiring( localPlayer );
                }

                Silent::NotifyFiring( s_SilFireCache );
        }

        // ==================== Rage Aimbot ====================

        if ( ClosestEntity != 0 )
        {
                if ( CurrentMatrix.m [ 0 ][ 0 ] == 0.f )
                {
                        CurrentMatrix = ViewMatrix;
                }

                if ( AimCfg.Enabled && AimCfg.aimtype == 1 && ( !g_Globals.AimBot.VisibleCheck || ( g_Globals.AimBot.VisibleCheck && enemiesvisible ) ) )
                {
                        static bool s_AimFloodRunning = false;

                        bool isShooting = rageFiringActive; // so enquanto ATIRA (UGCStartFiring)
                        /*
                         * MOBILE (FIX "RAGE NUNCA ATIVA"): MouseDown[0] do painel
                         * nunca fica true durante o gameplay — o toque vai pro jogo,
                         * não pro overlay do painel (no Windows o rage esperava
                         * botão do mouse + tecla). O gatilho aqui é o keybind
                         * (botão flutuante) ou, sem botão criado, o checkbox Enabled.
                         */
                        const bool keyCurrentlyPressed = AimTriggerHeld( AimCfg.KeyBind, AimCfg.Enabled );

                        if ( isShooting && keyCurrentlyPressed && !IsCursorVisibleNow( ) )
                        {
                                RageTarget = ClosestEntity;

                                if ( !s_AimFloodRunning )
                                {
                                        s_AimFloodRunning = true;

                                        // SINAL NO LOG: o flood do rage comecou (mira presa ao alvo).
                                        static LONGLONG s_LastRageLog = 0;
                                        const LONGLONG nowRage = ( LONGLONG )GetTickCount64( );
                                        if ( nowRage - s_LastRageLog > 3000 )
                                        {
                                                s_LastRageLog = nowRage;
                                                DiagLog( "[aimbot] rage: flood iniciado (ent=0x%lX, disparando)", ( unsigned long )ClosestEntity );
                                        }

                                        std::thread( [ localPlayer, MainCamera, N32 ] ( )
                                        {
                                                // Qualquer saida (inclusive excecao) libera o flag do
                                                // flood. Sem isso, uma excecao dentro da thread deixava
                                                // s_AimFloodRunning preso em true e o rage nunca mais
                                                // ativava na partida ("para do nada e nao volta").
                                                struct FloodReset { bool* p; ~FloodReset( ) { *p = false; } } reset{ &s_AimFloodRunning };

                                                /*
                                                 * Disparo FRESCO (Player::UGCStartFiring) lido da
                                                 * memoria DENTRO da thread: o MouseDown[0] do ImGui
                                                 * nao acompanha o botao de fogo do jogo no Android.
                                                 * Recheca a cada 32ms pra nao inflar a ponte; entre
                                                 * checagens usa cache com grace de 250ms (tap de
                                                 * semi-auto nao derruba o rage entre um tiro e outro).
                                                 */
                                                LONGLONG lastFireMs = 0;
                                                LONGLONG lastFireCheckMs = 0;
                                                bool fireCache = true;

                                                auto firingNow = [ & ] ( ) -> bool
                                                {
                                                        const LONGLONG nowMs = ( LONGLONG )GetTickCount64( );

                                                        if ( nowMs - lastFireCheckMs >= 32 )
                                                        {
                                                                lastFireCheckMs = nowMs;
                                                                fireCache = ReadLocalFiring( localPlayer );

                                                                if ( fireCache )
                                                                        lastFireMs = nowMs;
                                                        }

                                                        return fireCache || ( nowMs - lastFireMs ) < 250;
                                                };

                                                int originalAimAssist = 0;
                                                bool aimAssistModified = false;

                                                try
                                                {
                                                auto ReadPtrT = [ N32 ] ( uintptr_t addr ) -> uintptr_t
                                                {
                                                        return N32 ? g_FreeFireMemory.Read<uint32_t>( addr ) : g_FreeFireMemory.Read<uint64_t>( addr );
                                                };

                                                int delayMs = 0;
                                                switch ( g_Globals.AimBot.PeitosIndex )
                                                {
                                                        case 1: delayMs = 300; break;
                                                        case 2: delayMs = 400; break;
                                                        case 3: delayMs = 450; break;
                                                        case 4: delayMs = 550; break;
                                                        case 5: delayMs = ( rand( ) % 450 );
                                                                break;
                                                        default: delayMs = 0; break;
                                                }

                                                if ( delayMs > 0 )
                                                        std::this_thread::sleep_for( std::chrono::milliseconds( delayMs ) );

                                                if ( delayMs > 0 )
                                                {
                                                        originalAimAssist = g_FreeFireMemory.Read<int>( localPlayer + Offsets::Player::m_EAimAssit );
                                                        g_FreeFireMemory.Write<int>( localPlayer + Offsets::Player::m_EAimAssit, 2 );
                                                        aimAssistModified = true;
                                                }

                                                if ( !firingNow( ) || !AimTriggerHeld( g_Globals.AimBot.KeyBind, g_Globals.AimBot.Enabled ) || IsCursorVisibleNow( ) )
                                                {
                                                        if ( aimAssistModified )
                                                                g_FreeFireMemory.Write<int>( localPlayer + Offsets::Player::m_EAimAssit, originalAimAssist );
                                                        return;
                                                }

                                                // Main aim loop
                                                while ( !g_Globals.General.ShutDown )
                                                {
                                                        if ( IsCursorVisibleNow( ) )
                                                                break;

                                                        /*
                                                         * FIX "NAO TÁ ATIRANDO E O AIMBOT CONTINUA": o
                                                         * disparo TEM que valer no break — na integracao
                                                         * o firingNow() ficou calculado mas FORA da
                                                         * condicao, e o rage seguia colado no alvo com
                                                         * IsFiring=false (so parava soltando a tecla).
                                                         */
                                                        const bool isStillShooting = firingNow( );
                                                        if ( !( isStillShooting && AimTriggerHeld( g_Globals.AimBot.KeyBind, g_Globals.AimBot.Enabled ) ) )
                                                                break;

                                                        // Relê o HP do alvo SEMPRE (com ou sem IgnoreKnocked):
                                                        // se o alvo morreu no meio do flood, sai do loop e o
                                                        // próximo frame re-seleciona o novo alvo. Antes, com
                                                        // IgnoreKnocked desligado, o flood ficava preso no
                                                        // cadáver até soltar o botão — parecia travado.
                                                        int hp = 1;
                                                        uintptr_t fixedTarget = RageTarget;
                                                        if ( fixedTarget != 0 )
                                                        {
                                                                uintptr_t priPool = ReadPtrT( fixedTarget + Offsets::ReplicationEntity::m_PRIDataPool );
                                                                if ( priPool != 0 )
                                                                {
                                                                        uintptr_t datas = ReadPtrT( priPool + Offsets::ReplicationEntity::m_Datas );
                                                                        uintptr_t health = ReadPtrT( datas + Offsets::ReplicationEntity::HealthCurrentPtr );
                                                                        if ( health )
                                                                                hp = g_FreeFireMemory.Read<int>( health + Offsets::ReplicationEntity::Value );
                                                                }

                                                                bool isKnocked = false;
                                                                uintptr_t shadowBase = ReadPtrT( fixedTarget + Offsets::PlayerNetwork::m_ShadowState );
                                                                if ( shadowBase != 0 )
                                                                {
                                                                        int playerPose = g_FreeFireMemory.Read<int>( shadowBase + Offsets::ShadowState::TargetPhysXPose );
                                                                        isKnocked = ( playerPose == 8 );
                                                                }

                                                                if ( hp <= 0 || ( g_Globals.AimBot.IgnoreKnocked && isKnocked ) )
                                                                {
                                                                        // PraCima
                                                                if (
                                                                        g_Globals.AimBot.PraCima &&
                                                                        g_Globals.AimBot.PraCimaValor > 0.f &&
                                                                        g_Globals.AimBot.PraCimaTempo > 0
                                                                )
                                                                {
                                                                        Quaternion qCurrent =
                                                                                g_FreeFireMemory.Read<Quaternion>(
                                                                                        localPlayer +
                                                                                        Offsets::Player::m_AimRotation
                                                                                );

                                                                        const float totalPitchUp =
                                                                                -g_Globals.AimBot.PraCimaValor;

                                                                        const int totalTimeMs =
                                                                                g_Globals.AimBot.PraCimaTempo;

                                                                        const float pitchPerMs =
                                                                                totalPitchUp /
                                                                                static_cast<float>(totalTimeMs);

                                                                        const Quaternion qDelta =
                                                                                Quaternion::FromEuler(
                                                                                        pitchPerMs,
                                                                                        0.0f,
                                                                                        0.0f
                                                                                );

                                                                        for (int elapsedMs = 0;
                                                                                elapsedMs < totalTimeMs;
                                                                                ++elapsedMs)
                                                                        {
                                                                                qCurrent =
                                                                                        Quaternion::Normalized(
                                                                                                qDelta * qCurrent
                                                                                        );

                                                                                g_FreeFireMemory.Write<Quaternion>(
                                                                                        localPlayer +
                                                                                        Offsets::Player::m_AimRotation,
                                                                                        qCurrent
                                                                                );

                                                                                usleep(1000);
                                                                        }

                                                                        qCurrent =
                                                                                Quaternion::Normalized(qCurrent);

                                                                        g_FreeFireMemory.Write<Quaternion>(
                                                                                localPlayer +
                                                                                Offsets::Player::m_AimRotation,
                                                                                qCurrent
                                                                        );
                                                                }

                                                                        Sleep( 300 );
                                                                        break;
                                                                }
                                                        }
                                                        else
                                                        {
                                                                break;
                                                        }

                                                        Vector3 Head = Transform::GetHeadPosition( RageTarget, N32 );
                                                        Vector3 LocalCamera = ( MainCamera != 0 ) ? Transform::get_position_Injected( MainCamera, N32 ) : Vector3::Zero( );
                                                        if ( Head == Vector3::Zero( ) || LocalCamera == Vector3::Zero( ) )
                                                                break;

                                                        auto playerLook = AimBot::GetRotationToLocation( Head, 0.0f, LocalCamera );
                                                        g_FreeFireMemory.Write( localPlayer + Offsets::Player::m_AimRotation, playerLook );
                                                        /* STEALTH DE ESCRITA: 1µs virou 2ms (ver magnet) */
                                                        std::this_thread::sleep_for( std::chrono::milliseconds( 2 ) );
                                                }

                                                if ( aimAssistModified )
                                                        g_FreeFireMemory.Write<int>( localPlayer + Offsets::Player::m_EAimAssit, originalAimAssist );

                                                }
                                                catch ( ... )
                                                {
                                                        // Excecao na thread do rage: restaura o assist e sai.
                                                        // O FloodReset libera o flag — o rage volta a ativar.
                                                        if ( aimAssistModified )
                                                                g_FreeFireMemory.Write<int>( localPlayer + Offsets::Player::m_EAimAssit, originalAimAssist );
                                                }

                                        } ).detach( );
                                }
                        }
                }
        }

        // ==================== Weapon Exploits ====================

        /*
         * THROTTLE + STEALTH: este bloco lê e escreve na memória do jogo.
         * Rodando TODO frame (até 240 fps) gerava centenas de operações
         * por segundo em cadência de metrônomo — assinatura fácil de
         * detectar. Agora roda a cada 50ms (20 Hz), mais que suficiente
         * para NoRecoil/MedKit/Precision/FireDelay/etc.
         * O SpinBot ficou FORA do throttle (efeito visual contínuo).
         */
        static LONGLONG s_LastExploitTick = 0;
        const LONGLONG nowExploitTick = GetTickCount64( );
        const bool exploitTick = ( nowExploitTick - s_LastExploitTick ) >= 50;
        if ( exploitTick )
                s_LastExploitTick = nowExploitTick;

        // --- SpinBot (por frame, fora do throttle) ---
        if ( g_Globals.Misc.Exploits.LocalPlayer.SpinBot && !IsObserving )
        {
                SpinBot( localPlayer, N32 );
        }

        if ( exploitTick )
        {

        uintptr_t m_InventoryManager = ReadPtr( localPlayer + Offsets::Player::m_InventoryManager );
        uintptr_t m_itemOnHand = ReadPtr( m_InventoryManager + Offsets::InventoryManager::m_itemOnHand );
        uintptr_t m_WeaponData = ReadPtr( m_itemOnHand + Offsets::Weapon::m_WeaponData );
        uintptr_t WeaponParams = m_itemOnHand + Offsets::Weapon::m_WeaponParams;

        // --- NoRecoil ---
        if ( g_Globals.Misc.Exploits.LocalPlayer.NoRecoil )
        {
                uintptr_t FireComponent = ReadPtr( m_itemOnHand + Offsets::Weapon::FireComponent );
                if ( FireComponent != 0 )
                {
                        float currentRecoil = 0.0f;
                        if ( g_FreeFireMemory.Read<float>( FireComponent + Offsets::Weapon::tangentTheta, currentRecoil ) )
                        {
                                constexpr float baseRecoil = 0.0174825f;
                                float control = static_cast< float >( g_Globals.Misc.Exploits.LocalPlayer.RecoilControl );
                                float newRecoil = baseRecoil * ( 1.0f - ( control / 100.0f ) );
                                if ( fabs( currentRecoil - newRecoil ) > 0.00001f )
                                        g_FreeFireMemory.Write<float>( FireComponent + Offsets::Weapon::tangentTheta, newRecoil );
                        }
                }
        }

        // --- SocoLonge ---
        if ( g_Globals.Misc.Exploits.LocalPlayer.SocoLonge && !IsObserving )
        {
                float socolonge = g_FreeFireMemory.Read<float>( WeaponParams + Offsets::WeaponParams::Range );
                if ( socolonge == 1.35f )
                {
                        g_FreeFireMemory.Write<float>( WeaponParams + Offsets::WeaponParams::Range, 3.1f );
                }
        }

        static float s_FastmeditOriginal = 0.0f;
        static bool s_FastmeditHasOriginal = false;
        static bool s_FastmeditRestored = true;

        if (g_Globals.Misc.Exploits.LocalPlayer.FastMedkit)
        {
                if (PlayerAttributes != 0)
                {
                        uintptr_t eatSpeedAddr = PlayerAttributes + Offsets::PlayerAttributes::m_EatSpeedScale;
                        if (eatSpeedAddr != 0)
                        {
                                if (!s_FastmeditHasOriginal)
                                {
                                        s_FastmeditOriginal = g_FreeFireMemory.Read<float>(eatSpeedAddr);
                                        s_FastmeditHasOriginal = true;
                                        s_FastmeditRestored = false;
                                }

                                float valueToWrite = s_FastmeditOriginal;
                                if (s_FastmeditOriginal == 1.0f)
                                {
                                        valueToWrite = 0.75f;
                                }
                                else if (s_FastmeditOriginal == 0.75f)
                                {
                                        valueToWrite = 0.50f;
                                }
                                g_FreeFireMemory.Write<float>(eatSpeedAddr, valueToWrite);
                        }
                }
        }
        else
        {
                if (s_FastmeditHasOriginal && !s_FastmeditRestored && PlayerAttributes != 0)
                {
                        uintptr_t eatSpeedAddr = PlayerAttributes + Offsets::PlayerAttributes::m_EatSpeedScale;
                        if (eatSpeedAddr != 0)
                        {
                                g_FreeFireMemory.Write<float>(eatSpeedAddr, s_FastmeditOriginal);
                                s_FastmeditRestored = true;
                        }
                }
        }

        // --- MoreDamage ---
        if ( g_Globals.Misc.Exploits.LocalPlayer.MoreDamage )
        {
                g_FreeFireMemory.Write<float>( WeaponParams + Offsets::WeaponParams::FullDamageDistance, 400.0f );
        }

        // --- FireDelay ---
        if ( g_Globals.Misc.Exploits.LocalPlayer.FireDelay )
        {
                g_FreeFireMemory.Write<float>( WeaponParams + Offsets::WeaponParams::PrefireDelay, 0.0f );
        }

        // --- Aimlock ---
        if ( g_Globals.Misc.Exploits.LocalPlayer.Aimlock )
        {
                g_FreeFireMemory.Write<float>( m_itemOnHand + Offsets::Weapon::m_FireDuration, -3.0f );
        }

        // --- AimLock2x ---
        if ( g_Globals.Misc.Exploits.LocalPlayer.AimLock2x && AimCfg.aimtype == 0 )
        {
                bool isSighting = g_FreeFireMemory.Read<bool>( m_itemOnHand + Offsets::Weapon::m_IsSighting );
                if ( isSighting )
                {
                        uintptr_t aimassist = ReadPtr( localPlayer + Offsets::Player::m_AimAssistOnSighting );
                        g_FreeFireMemory.Write<float>( aimassist + Offsets::AimAssistOnSighting::m_fAimAssistCurrentLerpTime, 0.0f );
                }
        }

        // --- AimbotAwm ---
        static std::vector<std::pair<uintptr_t, int>> s_awmOriginals;
        int WeaponType = g_FreeFireMemory.Read<int>( m_WeaponData + Offsets::Weapon::IntWeaponType );
        if ( g_Globals.Misc.Exploits.LocalPlayer.AimbotAwm )
        {
                if ( WeaponType == 1 )
                {
                        bool exists = false;
                        for ( const auto& pair : s_awmOriginals )
                        {
                                if ( pair.first == m_WeaponData )
                                {
                                        exists = true;
                                        break;
                                }
                        }
                        if ( !exists )
                        {
                                s_awmOriginals.emplace_back( m_WeaponData, WeaponType );
                        }

                        int currentCheck = g_FreeFireMemory.Read<int>( m_WeaponData + Offsets::Weapon::IntWeaponType );
                        if ( currentCheck != 0 )
                        {
                                g_FreeFireMemory.Write<int>( m_WeaponData + Offsets::Weapon::IntWeaponType, 0 );
                        }
                }
        }
        else
        {
                if ( !s_awmOriginals.empty( ) )
                {
                        for ( const auto& [weaponAddr, original] : s_awmOriginals )
                        {
                                if ( weaponAddr != 0 )
                                {
                                        g_FreeFireMemory.Write<int>( weaponAddr + Offsets::Weapon::IntWeaponType, original );
                                }
                        }
                        s_awmOriginals.clear( );
                }
        }

        } // if ( exploitTick )

        }
        catch ( const std::exception& ex )
        {
                DiagLog( "[diag] Draw exception: %s", ex.what( ) );
        }
        catch ( ... )
        {
                DiagLog( "[diag] Draw exception (unknown)" );
        }
}

// ==================== Helper Functions ====================

void Data::DrawBox( float x, float y, float w, float h, ImColor color, ImColor fillColor, float thickness, int Type )
{
        ImDrawList* DrawList = ImGui::GetForegroundDrawList( );

        if ( Type == 1 )
        {
                if ( g_Globals.Visuals.ESP.BoxFilled )
                {
                        DrawList->AddRectFilled( ImVec2( x, y ), ImVec2( x + w, y + h ), ImGui::GetColorU32( ImVec4( fillColor.Value.x, fillColor.Value.y, fillColor.Value.z, fillColor.Value.w * 0.3f ) ) );
                }
                DrawList->AddRect( ImVec2( x, y ), ImVec2( x + w, y + h ), color, 0.0f, thickness );
                return;
        }
        else if ( Type == 2 )
        {
                float lineW = w / 3.0f;
                float lineH = h / 3.0f;

                if ( g_Globals.Visuals.ESP.BoxFilled )
                {
                        DrawList->AddRectFilled( ImVec2( x, y ), ImVec2( x + w, y + h ), ImGui::GetColorU32( ImVec4( fillColor.Value.x, fillColor.Value.y, fillColor.Value.z, fillColor.Value.w * 0.3f ) ) );
                }

                DrawList->AddLine( ImVec2( x, y - thickness / 2 ), ImVec2( x, y + lineH ), color, thickness );
                DrawList->AddLine( ImVec2( x - thickness / 2, y ), ImVec2( x + lineW, y ), color, thickness );
                DrawList->AddLine( ImVec2( x + w - lineW, y ), ImVec2( x + w + thickness / 2, y ), color, thickness );
                DrawList->AddLine( ImVec2( x + w, y - thickness / 2 ), ImVec2( x + w, y + lineH ), color, thickness );
                DrawList->AddLine( ImVec2( x, y + h - lineH ), ImVec2( x, y + h + thickness / 2 ), color, thickness );
                DrawList->AddLine( ImVec2( x - thickness / 2, y + h ), ImVec2( x + lineW, y + h ), color, thickness );
                DrawList->AddLine( ImVec2( x + w - lineW, y + h ), ImVec2( x + w + thickness / 2, y + h ), color, thickness );
                DrawList->AddLine( ImVec2( x + w, y + h - lineH ), ImVec2( x + w, y + h + thickness / 2 ), color, thickness );
                return;
        }
        else if ( Type == 3 )
        {
                if ( g_Globals.Visuals.ESP.BoxFilled )
                {
                        DrawList->AddRectFilled( ImVec2( x, y ), ImVec2( x + w, y + h ), ImGui::GetColorU32( ImVec4( fillColor.Value.x, fillColor.Value.y, fillColor.Value.z, fillColor.Value.w ) ) );
                }
                DrawList->AddRect( ImVec2( x, y ), ImVec2( x + w, y + h ), color, 0.0f, thickness );
                return;
        }
        else
        {
                if ( g_Globals.Visuals.ESP.BoxFilled )
                {
                        DrawList->AddRectFilled( ImVec2( x, y ), ImVec2( x + w, y + h ), ImGui::GetColorU32( ImVec4( fillColor.Value.x, fillColor.Value.y, fillColor.Value.z, fillColor.Value.w * 0.3f ) ) );
                }
                DrawList->AddRect( ImVec2( x, y ), ImVec2( x + w, y + h ), color, 0.0f, thickness );
                return;
        }
}

void Data::DrawWeapon( int WeaponID, bool IsKnocked, Vector3 HeadPos, float Height )
{
        const int Style = g_Globals.Visuals.ESP.WeaponStyle;
        if ( Style == 0 ) return;

        // WeaponID < 0 = leitura de arma falhou (player continua na ESP, so
        // a linha da arma e pulada).
        if ( WeaponID < 0 ) return;

        // "Icones de arma" (ShowIcons): desliga os icones sem desativar a arma
        // inteira — estilo Icono/Both cai para Texto quando o toggle esta off.
        bool drawIcons = g_Globals.Visuals.ESP.ShowIcons;

        static bool namegun_inited = false;
        if ( !namegun_inited )
        {
                Namegun::Init( ); namegun_inited = true;
        }

        ImDrawList* DrawList = ImGui::GetForegroundDrawList( );

        const float feetY = HeadPos.Y + Height;
        const float centerX = HeadPos.X;

        ImVec2 iconSz( 0, 0 );
        ImVec2 textSz( 0, 0 );
        std::string icon, name;

        ImColor Color = IsKnocked ? ImColor( 1.f, 0.f, 0.f, 1.f )
                : ImColor( g_Globals.Visuals.ESP.WeaponColor [ 0 ], g_Globals.Visuals.ESP.WeaponColor [ 1 ],
                        g_Globals.Visuals.ESP.WeaponColor [ 2 ], g_Globals.Visuals.ESP.WeaponColor [ 3 ] );

        if ( Namegun::HasIcon( WeaponID ) && drawIcons ) icon = Namegun::GetGunIcon( WeaponID );
        name = Namegun::GetGunName( WeaponID );

        if ( ( Style == 2 || Style == 3 ) && drawIcons )
        {
                if ( !icon.empty( ) )
                {
                        ImGui::PushFont( Fonts::IconWeapon );
                        iconSz = Utils::CalcTextSize( Fonts::IconWeapon, g_Globals.Visuals.ESP.TextSize, icon.c_str( ) );
                        ImGui::PopFont( );
                }
        }

        // Estilo "Icon" sem icones permitidos cai para texto (fallback)
        const bool drawText = ( Style == 1 || Style == 3 || ( Style == 2 && !drawIcons ) );

        if ( drawText )
        {
                if ( !name.empty( ) )
                {
                        ImGui::PushFont( Fonts::InterRegular );
                        textSz = Utils::CalcTextSize( Fonts::InterRegular, g_Globals.Visuals.ESP.TextSize, name.c_str( ) );
                        ImGui::PopFont( );
                }
        }

        if ( ( Style == 2 || Style == 3 ) && drawIcons )
        {
                if ( !icon.empty( ) )
                {
                        ImGui::PushFont( Fonts::IconWeapon );
                        DrawList->AddText( Fonts::IconWeapon, g_Globals.Visuals.ESP.TextSize,
                                ImVec2( centerX - iconSz.x * 0.5f, feetY + 33.0f ), Color, icon.c_str( ) );
                        ImGui::PopFont( );
                }
        }

        if ( drawText )
        {
                if ( !name.empty( ) )
                {
                        ImGui::PushFont( Fonts::Verdana );
                        float yName = feetY + 20.0f;
                        DrawList->AddText( Fonts::Verdana, g_Globals.Visuals.ESP.TextSize,
                                ImVec2( centerX - textSz.x * 0.5f, yName ), Color, name.c_str( ) );
                        ImGui::PopFont( );
                }
        }
}

void Data::DrawHealthBar( short CurrentHealth, short MaxHealth, ImVec2 HeadPos, ImVec2 EntityPos, float Width, float Height, uintptr_t Entity )
{
        struct HealthCacheEntry
        {
                uintptr_t Entity;
                float SmoothedHealth;
        };

        if ( MaxHealth <= 0 ) return;

        int Style = g_Globals.Visuals.ESP.HealthBarStyle;
        if ( Style == 0 ) return;

        static std::vector<HealthCacheEntry> HealthCache;
        static const int MAX_HEALTH_ENTITIES = 512;

        ImDrawList* DrawList = ImGui::GetForegroundDrawList( );
        ImGuiIO& io = ImGui::GetIO( );

        float HealthPercentage = static_cast< float >( CurrentHealth ) / MaxHealth;
        float* SmoothedHealthPtr = nullptr;

        for ( auto& entry : HealthCache )
        {
                if ( entry.Entity == Entity )
                {
                        SmoothedHealthPtr = &entry.SmoothedHealth;
                        break;
                }
        }

        if ( !SmoothedHealthPtr )
        {
                if ( HealthCache.size( ) < MAX_HEALTH_ENTITIES )
                {
                        HealthCache.push_back( { Entity, HealthPercentage } );
                        SmoothedHealthPtr = &HealthCache.back( ).SmoothedHealth;
                }
                else
                {
                        int index = Entity % MAX_HEALTH_ENTITIES;
                        HealthCache [ index ] = { Entity, HealthPercentage };
                        SmoothedHealthPtr = &HealthCache [ index ].SmoothedHealth;
                }
        }

        float& SmoothedHealth = *SmoothedHealthPtr;
        SmoothedHealth = ImLerp( SmoothedHealth, HealthPercentage, io.DeltaTime * 10.0f );

        ImVec4 GreenColor = ImVec4( 0.0f, 1.0f, 0.0f, 1.0f );
        ImVec4 YellowColor = ImVec4( 1.0f, 1.0f, 0.0f, 1.0f );
        ImVec4 RedColor = ImVec4( 1.0f, 0.0f, 0.0f, 1.0f );

        ImVec4 HealthBarColor;
        if ( SmoothedHealth > 0.5f )
                HealthBarColor = ImLerp( GreenColor, YellowColor, ( 1.0f - SmoothedHealth ) * 2.0f );
        else
                HealthBarColor = ImLerp( YellowColor, RedColor, ( 0.5f - SmoothedHealth ) * 2.0f );

        if ( Style == 1 )
        {
                ImVec2 Position( HeadPos.x - ( Width * 0.5f ) - 5.0f, HeadPos.y );
                float BarWidth = 2.5f;
                float FilledBarHeight = Height * SmoothedHealth;

                DrawList->AddRectFilled( ImVec2( Position.x, Position.y ), ImVec2( Position.x + BarWidth, Position.y + Height ), IM_COL32( 0, 0, 0, 128 ) );
                DrawList->AddRectFilled( ImVec2( Position.x, Position.y + ( Height - FilledBarHeight ) ), ImVec2( Position.x + BarWidth, Position.y + Height ), ImGui::ColorConvertFloat4ToU32( HealthBarColor ) );
        }
        else if ( Style == 2 )
        {
                ImVec2 Position( HeadPos.x + ( Width * 0.5f ) + 3.0f, HeadPos.y );
                float BarWidth = 2.5f;
                float FilledBarHeight = Height * SmoothedHealth;

                DrawList->AddRectFilled( ImVec2( Position.x, Position.y ), ImVec2( Position.x + BarWidth, Position.y + Height ), IM_COL32( 0, 0, 0, 128 ) );
                DrawList->AddRectFilled( ImVec2( Position.x, Position.y + ( Height - FilledBarHeight ) ), ImVec2( Position.x + BarWidth, Position.y + Height ), ImGui::ColorConvertFloat4ToU32( HealthBarColor ) );
        }
        else if ( Style == 3 )
        {
                const bool showName = g_Globals.Visuals.ESP.ShowName;
                float dynamicTopOffset = showName ? ( kNameOffset + kStackGap + kHealthBarHeight )
                        : kHealthOffsetBase;

                ImVec2 Position( HeadPos.x - Width * 0.5f, HeadPos.y - dynamicTopOffset );
                float BarHeight = kHealthBarHeight;
                float FilledWidth = Width * SmoothedHealth;

                DrawList->AddRectFilled( Position, ImVec2( Position.x + Width, Position.y + BarHeight ), IM_COL32( 0, 0, 0, 128 ) );
                DrawList->AddRectFilled( Position, ImVec2( Position.x + FilledWidth, Position.y + BarHeight ), ImGui::ColorConvertFloat4ToU32( HealthBarColor ) );
        }
        else if ( Style == 4 )
        {
                ImVec2 Position( HeadPos.x - ( Width * 0.5f ), EntityPos.y + 5.0f );
                float BarHeight = 2.5f;
                float FilledWidth = Width * SmoothedHealth;

                DrawList->AddRectFilled( Position, ImVec2( Position.x + Width, Position.y + BarHeight ), IM_COL32( 0, 0, 0, 128 ) );
                DrawList->AddRectFilled( Position, ImVec2( Position.x + FilledWidth, Position.y + BarHeight ), ImGui::ColorConvertFloat4ToU32( HealthBarColor ) );
        }
        else if ( Style == 5 )
        {
                ImGui::PushFont( Fonts::Gff );
                char healthText [ 16 ];
                snprintf( healthText, sizeof( healthText ), "HP: %d", ( int )CurrentHealth );
                ImVec2 TextSize = Utils::CalcTextSize( Fonts::Gff, 13.0f, healthText );
                ImVec2 barPos( HeadPos.x - Width * 0.5f, HeadPos.y - 3.0f );
                ImVec2 textPos( HeadPos.x - ( TextSize.x * 0.5f ), barPos.y - TextSize.y - 2.0f );
                DrawList->AddText( Fonts::Gff, 13.0f, textPos, ImColor( 255, 255, 255, 255 ), healthText );
                ImGui::PopFont( );
        }
}

void Data::DrawSnapLine( const Vector3& HeadPos, const Vector3& EntityPos, bool showName, bool healthTop, ImColor color, float thickness, int type )
{
        if ( type == 0 ) return;

        float topOffset = healthTop ? ( showName ? ( kNameOffset + kStackGap + kHealthBarHeight )
                : kHealthOffsetBase )
                : ( showName ? kNameOffset : 1.0f );

        float targetX = HeadPos.X;
        float targetY = HeadPos.Y;

        switch ( type )
        {
                case 1: targetY = HeadPos.Y - topOffset; break;
                case 2: targetY = EntityPos.Y + 1.0f; break;
                default: targetY = HeadPos.Y; break;
        }

        ImDrawList* DrawList = ImGui::GetForegroundDrawList( );
        ImVec2 startPoint = ( type == 2 )
                ? ImVec2( ScreenWidth * 0.5f, ( float )ScreenHeight )
                : ImVec2( ScreenWidth * 0.5f, ScreenHeight * 0.03f );

        // Alvo fora dos limites da tela NAO pode fazer a linha sumir: clamped para
        // a borda mais proxima, a snapline continua "pegando" (aponta a direcao)
        // mesmo com o inimigo fora do enquadramento.
        float targetXClamped = ImClamp( targetX, 0.0f, ( float )( ScreenWidth - 1 ) );
        float targetYClamped = ImClamp( targetY, 0.0f, ( float )( ScreenHeight - 1 ) );

        ImVec2 endPoint( targetXClamped, targetYClamped );
        DrawList->AddLine( startPoint, endPoint, color, thickness );
}

void Data::SpinBot( uintptr_t LocalPlayer, bool N32 )
{
        if ( LocalPlayer == 0 ) return;

        auto ReadPtr = [ N32 ] ( uintptr_t addr ) -> uintptr_t
        {
                return N32 ? g_FreeFireMemory.Read<uint32_t>( addr ) : g_FreeFireMemory.Read<uint64_t>( addr );
        };

        uintptr_t transform = ReadPtr( LocalPlayer + Offsets::PlayerTransformNode::m_CachedTransform );
        if ( !transform ) return;

        uintptr_t transformObj = ReadPtr( transform + Offsets::GetPosWorld::transObj );
        if ( !transformObj ) return;

        uintptr_t matrix = ReadPtr( transformObj + Offsets::GetPosWorld::matrix );
        if ( !matrix ) return;

        int index = g_FreeFireMemory.Read<int>( transformObj + Offsets::GetPosWorld::index );

        uintptr_t matrixList = ReadPtr( matrix + Offsets::GetPosWorld::matrix_list );
        uintptr_t matrixIndices = ReadPtr( matrix + Offsets::GetPosWorld::matrix_indices );

        if ( !matrixList || !matrixIndices ) return;

        bool isShooting = ImGui::GetIO().MouseDown[0];
        uintptr_t userControl = ReadPtr( LocalPlayer + Offsets::Player::m_UserControl );
        if ( userControl != 0 )
        {
                uintptr_t axisDataArray = ReadPtr( userControl + Offsets::UserControlHandler::m_AxisData );
                if ( axisDataArray != 0 )
                {
                        uintptr_t moveAxisData = ReadPtr( axisDataArray + ( N32 ? 0x10 : 0x20 ) );
                        if ( moveAxisData != 0 )
                        {
                                bool isTouched = g_FreeFireMemory.Read<bool>( moveAxisData + Offsets::UserControlHandler::m_IsTouched );
                                if ( isTouched && !isShooting )
                                {
                                        return;
                                }
                        }
                }
        }

        static float rotationAngle = 0.0f;
        rotationAngle += g_Globals.Misc.Exploits.LocalPlayer.SpinSpeed * 0.1f;

        if ( rotationAngle >= 3.14159265358979323846f * 2.0f )
                rotationAngle -= 3.14159265358979323846f * 2.0f;

        uintptr_t rotationWriteOffset = N32 ? 0x10 : 0x20;

        TMatrix rootMatrix = g_FreeFireMemory.Read<TMatrix>( matrixList + sizeof( TMatrix ) * index );
        Quaternion rootQuat( rootMatrix.Rotation.x, rootMatrix.Rotation.y, rootMatrix.Rotation.z, rootMatrix.Rotation.w );
        Vector3 rootEuler = Quaternion::ToEuler( rootQuat );
        Quaternion newRootQuat = Quaternion::FromEuler( rootEuler.X, rotationAngle, rootEuler.Z );

        newRootQuat = Quaternion::Normalized( newRootQuat );
        g_FreeFireMemory.Write<Vector4>( matrixList + sizeof( TMatrix ) * index + rotationWriteOffset, { newRootQuat.X, newRootQuat.Y, newRootQuat.Z, newRootQuat.W } );
        int parentIndex = g_FreeFireMemory.Read<int>( matrixIndices + sizeof( int ) * index );

        int curIndex = 0;
        while ( parentIndex >= 0 && curIndex++ < 60 )
        {
                TMatrix parentMatrix = g_FreeFireMemory.Read<TMatrix>( matrixList + sizeof( TMatrix ) * parentIndex );
                Quaternion parentQuat( parentMatrix.Rotation.x, parentMatrix.Rotation.y, parentMatrix.Rotation.z, parentMatrix.Rotation.w );
                Vector3 parentEuler = Quaternion::ToEuler( parentQuat );
                Quaternion newParentQuat = Quaternion::FromEuler( parentEuler.X, rotationAngle, parentEuler.Z );
                newParentQuat = Quaternion::Normalized( newParentQuat );
                g_FreeFireMemory.Write<Vector4>( matrixList + sizeof( TMatrix ) * parentIndex + rotationWriteOffset, { newParentQuat.X, newParentQuat.Y, newParentQuat.Z, newParentQuat.W } );
                parentIndex = g_FreeFireMemory.Read<int>( matrixIndices + sizeof( int ) * parentIndex );
        }
}