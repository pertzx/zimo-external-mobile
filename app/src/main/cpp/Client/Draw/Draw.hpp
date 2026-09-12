#pragma once

#include <WindowsCompat.hpp>
#include <pthread.h>
// ========== DEFINIR ANTES DE INCLUIR imgui.h ==========
#include <imgui.h>
#include <time.h>
#include <Memory/Memory.hpp>
#include <Offsets/Offsets.hpp>
#include <Unity/Unity.hpp>
#include <Math/Vectors/Vector3.hpp>
#include <Globals.hpp>
#include <vector>
#include <mutex>
#include <thread>
#include <atomic>
#include <string>
#include <cstring>
#include <XorStr.hpp>

enum PlayerType
{
        PLAYER_UNKNOWN = 0,
        PLAYER_NETWORK,
        PLAYER,
        PLAYER_TRAINING_HUMAN,
        PLAYER_TRAINING_STAND
};

struct PlayerData
{
        Vector3 HeadScreen;
        Vector3 FeetScreen;
        Vector3 ScreenPos;
        // Posicoes no mundo (usadas para reprojetar na tela com a view matrix mais
        // recente a cada frame, mesmo quando a leitura de entidades engasga — assim
        // o ESP continua "grudado" nos jogadores em vez de congelar/desligar).
        Vector3 HeadWorld;
        Vector3 FeetWorld;
        float HealthPercent;
        bool IsKnocked;
        // true = aliado (so entra no snapshot com Visuals.ESP.ShowTeam ligado).
        // Desenhado com a cor de time e NUNCA pode virar alvo do aimbot/silent.
        bool IsTeammate;
        bool IsVisible;
        bool IsBot;
        int WeaponID;
        uintptr_t Entity;
        uintptr_t UMAData;
        std::string Name;
        std::string Weapon;
        float Distance;
        short CurrentHealth;
        short MaxHealth;
        // Skeleton points
        std::vector<Vector3> Skeleton;
        // Tick (GetTickCount64) da última leitura boa desta entidade. Entidades
        // confirmadas na lista de ataque que caírem por falha TRANSITÓRIA de leitura
        // herdam o último estado bom (anti-flicker), mas só dentro desta janela —
        // se a falha persistir além dela a entidade cai de verdade (morto/despawn).
        LONGLONG LastSeenTick;
};

struct GameContext
{
        uintptr_t LocalPlayer;
        uintptr_t MatchGame;
        uintptr_t Match;
        uintptr_t MainCamera;
        Matrix4x4 ViewMatrix;
        bool IsObserving;
        float ClosestEnemyDist;
        float LocalYaw;
};

class Data
{
        public:
        static void StartReadThread( );
        static void StopReadThread( );
        static void Draw( int width, int height, bool N32, bool V31 );
        static GameContext GetContext( );

        static void DrawBox( float x, float y, float w, float h, ImColor color, ImColor fillColor, float thickness, int Type );
        static void DrawWeapon( int WeaponID, bool IsKnocked, Vector3 HeadPos, float Height );
        static void DrawHealthBar( short CurrentHealth, short MaxHealth, ImVec2 HeadPos, ImVec2 EntityPos, float Width, float Height, uintptr_t Entity );
        static void DrawSnapLine( const Vector3& HeadPos, const Vector3& EntityPos, bool showName, bool healthTop, ImColor color, float thickness, int type );
        static void SpinBot( uintptr_t LocalPlayer, bool N32 );

        static PlayerType GetPlayerType( uintptr_t entity, bool N32 )
        {
                // IMPORTANTE: o nome da classe do Il2Cpp (Il2CppClass::name em
                // klass+0x8 no 32-bit) e uma C STRING comum (const char*, ASCII,
                // terminada em 0) — NAO uma System.String. Ler com Memory::String()
                // (que espera "length" int32 em +0x8 e chars UTF-16 em +0xC) sempre
                // devolvia string vazia -> name nunca batia com "PlayerNetwork" ->
                // PLAYER_UNKNOWN para TODAS as entidades -> o ESP nunca desenhava
                // (era exatamente o "acha tudo certinho mas nao aparece nada").
                uintptr_t klass = N32 ? g_FreeFireMemory.Read<uint32_t>( entity ) : g_FreeFireMemory.Read<uint64_t>( entity );
                if ( klass == 0 ) return PLAYER_UNKNOWN;

                uintptr_t namePtr = N32 ? g_FreeFireMemory.Read<uint32_t>( klass + 0x8 ) : g_FreeFireMemory.Read<uint64_t>( klass + 0x10 );
                if ( namePtr == 0 ) return PLAYER_UNKNOWN;

                // Leitura de C STRING: bytes crus, null-terminated.
                char nameBuf[ 48 ];
                memset( nameBuf, 0, sizeof( nameBuf ) );
                if ( !g_FreeFireMemory.Read( namePtr, nameBuf, sizeof( nameBuf ) - 1 ) )
                        return PLAYER_NETWORK; // nao conseguiu ler o nome: aceita como player normal (os filtros seguintes filtram lixo)

                std::string name( nameBuf );

                if ( name == XorStr("PlayerNetwork") ) return PLAYER_NETWORK;
                if ( name == XorStr("PlayerUGCCommon") ) return PLAYER;
                if ( name == XorStr("Player_TrainingHumanTarget") ) return PLAYER_TRAINING_HUMAN;
                if ( name == XorStr("Player_TrainingHumanTarget_Stand") ) return PLAYER_TRAINING_STAND;

                // Classe desconhecida NAO descarta mais a entidade — o codigo que
                // funcionava aceitava tudo que vinha da lista de ataque. Os checks
                // seguintes (avatar/HP/posicao) filtram ponteiro lixo de qualquer jeito.
                return PLAYER_NETWORK;
        }

        // Nome legivel do tipo — so para os logs [ENTITY].
        static const char* GetPlayerTypeName( PlayerType t )
        {
                switch ( t )
                {
                        case PLAYER_NETWORK:              return "PlayerNetwork";
                        case PLAYER:                      return "PlayerUGCCommon";
                        case PLAYER_TRAINING_HUMAN:       return "TrainingHuman";
                        case PLAYER_TRAINING_STAND:       return "TrainingStand";
                        default:                          return "?";
                }
        }

        // Public accessors for DaemonApp IPC
        static std::vector<PlayerData>& GetPlayers() { return m_Players; }
        static GameContext& GetContextRef() { return m_Context; }
        static std::mutex& GetMutex() { return m_Mutex; }
        static std::atomic<bool>& GetRunning() { return m_Running; }

        public:
        // Explicitly allow access to private members from implementation
        static void SetRunning(bool value);
        static bool IsRunning();

        private:
        template <bool N32, bool V31>
        static void ReadLoop( );

        static std::vector<PlayerData> m_Players;
        static GameContext m_Context;
        static std::mutex m_Mutex;
        static std::atomic<bool> m_Running;
        static pthread_t m_ThreadHandle;
        static bool m_ThreadValid;
        // Template (N32/V31) com o qual a thread viva foi criada. Se o
        // GameConfig reclassificar a arquitetura depois do start, StartReadThread
        // detecta a divergencia e reinicia a thread — senao ela leria para
        // sempre com o tamanho de ponteiro errado (u64 em jogo v7a = lixo).
        static bool m_ThreadN32;
        static bool m_ThreadV31;
        // true quando m_Players/m_Context vieram de um frame de leitura que
        // completou com sucesso. Em falha transitória o snapshot antigo é mantido
        // (ESP continua desenhando), mas fica "não fresco" para o aimbot não mirar
        // em posição congelada.
        static bool m_SnapshotFresh;
        // Tick (GetTickCount64) do último frame com snapshot fresco — usado pelo
        // Draw para nunca desenhar posições congeladas por mais que ~3s (transição
        // real de partida), como o cheat de referência que limpa as entidades.
        static std::atomic<int64_t> m_LastFreshTick;

        template <bool N32, bool V31>
        static void* ReadLoopWrapper(void*);
};