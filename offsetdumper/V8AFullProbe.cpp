// ============================================================================
//  V8AFullProbe.cpp — medida REAL do layout il2cpp v8a (paste-in offsetdumper)
//
//  Saber que o perfil usa 0xB8 nao prova nada: o il2cpp do Free Fire e
//  MODIFICADO (prova: no v7a o static_fields fica em 0x5C, no il2cpp 24.x
//  padrao seria 0x54). Este probe mede, in-process, sem risco (toda
//  desreferencia e validada contra /proc/self/maps antes):
//
//   1. offsetof(Il2CppClass, static_fields) DESTA build (o "AccessClass"):
//        via System.String.Empty  (funciona ja no lobby, sem partida)
//        via GameFacade.CurrentMatchGame -> *(obj) == klass do MatchGame
//        (confirmacao forte, precisa estar EM PARTIDA)
//   2. Offsets REAIS dos campos da cadeia (static e instance):
//        GameFacade.CurrentMatchGame, MatchGame.m_Match, Match.m_State,
//        Match.m_LocalPlayer / m_LocalObserver / m_AttackableEntities,
//        todos os statics do GameVarDef, CameraControllerManager.m_Camera,
//        Camera.m_CachedPtr
//   3. Se static_fields de uma classe estiver NULL (nao inicializada), o
//      SFOFF nao valida nesta rodada — re-roda (o rescan de 40 rodadas ja
//      chama de novo).
//
//  COMO USAR:
//    1. Copie este arquivo para a pasta do offsetdumper.
//    2. No arquivo principal do dumper:  #include "V8AFullProbe.cpp"
//    3. Dentro do loop de rescan (a cada rodada), chame:
//           v8aprobe::RunV8AFullProbe();
//    4. Filtre: adb logcat -v time OffsetDumper:* *:S
//       (mesma tag do dumper, linhas com "V8A PROBE" / "SFOFF")
// ============================================================================

#pragma once

#include <dlfcn.h>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <vector>
#include <android/log.h>

#define PROBE_LOGI(...) __android_log_print(ANDROID_LOG_INFO, "OffsetDumper", __VA_ARGS__)
#define PROBE_LOGW(...) __android_log_print(ANDROID_LOG_WARN, "OffsetDumper", __VA_ARGS__)

namespace v8aprobe
{
    // ------------------------------------------------------------------
    // API il2cpp via dlsym (o dumper roda dentro do processo do jogo,
    // entao os simbolos exportados do libil2cpp.so estao visíveis).
    // ------------------------------------------------------------------
    struct Api
    {
        void*       (*domain_get)(void);
        void**      (*domain_get_assemblies)(void* domain, size_t* size);
        void*       (*assembly_get_image)(void* assembly);
        size_t      (*image_get_class_count)(void* image);
        void*       (*image_get_class)(void* image, size_t index);
        const char* (*class_get_name)(void* klass);
        const char* (*class_get_namespace)(void* klass);
        void*       (*class_get_field_from_name)(void* klass, const char* name);
        uint32_t    (*field_get_flags)(void* field);
        uint32_t    (*field_get_offset)(void* field);
        void*       (*field_get_type)(void* field);
        int         (*type_get_type)(void* type);
    };

    static Api   g_Api   = { };
    static bool  g_ApiOk = false;

    static bool LoadApi( )
    {
        if ( g_ApiOk )
                return true;

        struct Pair { const char* sym; void** dst; };
        static const Pair table[] =
        {
            { "il2cpp_domain_get",                (void**)&g_Api.domain_get },
            { "il2cpp_domain_get_assemblies",     (void**)&g_Api.domain_get_assemblies },
            { "il2cpp_assembly_get_image",        (void**)&g_Api.assembly_get_image },
            { "il2cpp_image_get_class_count",     (void**)&g_Api.image_get_class_count },
            { "il2cpp_image_get_class",           (void**)&g_Api.image_get_class },
            { "il2cpp_class_get_name",            (void**)&g_Api.class_get_name },
            { "il2cpp_class_get_namespace",       (void**)&g_Api.class_get_namespace },
            { "il2cpp_class_get_field_from_name", (void**)&g_Api.class_get_field_from_name },
            { "il2cpp_field_get_flags",           (void**)&g_Api.field_get_flags },
            { "il2cpp_field_get_offset",          (void**)&g_Api.field_get_offset },
            { "il2cpp_field_get_type",            (void**)&g_Api.field_get_type },
            { "il2cpp_type_get_type",             (void**)&g_Api.type_get_type },
        };

        bool ok = true;
        for ( const Pair& p : table )
        {
                *p.dst = dlsym( RTLD_DEFAULT, p.sym );
                if ( !*p.dst )
                {
                        PROBE_LOGW( "V8AProbe: simbolo ausente: %s", p.sym );
                        ok = false;
                }
        }
        g_ApiOk = ok;
        return ok;
    }

    // ------------------------------------------------------------------
    // Leitura segura: so desreferencia enderecos dentro de range 'r'
    // (legivel) de /proc/self/maps. Nada aqui derruba o jogo.
    // ------------------------------------------------------------------
    struct Range { uint64_t start, end; };
    static std::vector<Range> g_Ranges;

    static void LoadRanges( )
    {
        g_Ranges.clear( );
        FILE* f = fopen( "/proc/self/maps", "r" );
        if ( !f )
                return;
        char line[ 512 ];
        while ( fgets( line, sizeof( line ), f ) )
        {
                uint64_t s = 0, e = 0;
                char perm[ 8 ] = { 0 };
                if ( sscanf( line, "%lx-%lx %7s", &s, &e, perm ) >= 2 && perm[ 0 ] == 'r' )
                        g_Ranges.push_back( { s, e } );
        }
        fclose( f );
    }

    static bool SafeRead64( uint64_t addr, uint64_t* out )
    {
        for ( size_t i = 0; i < g_Ranges.size( ); i++ )
        {
                if ( addr >= g_Ranges[ i ].start && addr + 8 <= g_Ranges[ i ].end )
                {
                        *out = *(uint64_t*)addr;
                        return true;
                }
        }
        return false;
    }

    static bool Plausible( uint64_t v ) // ponteiro heap arm64; tokens/garbage ficam fora
    {
        return v >= 0x100000000ull && v < 0x800000000000ull;
    }

    // ------------------------------------------------------------------
    // Acha classe por nome (e namespace opcional) que POSSUA o campo
    // esperado — elimina colisao de nomes ofuscados.
    // ------------------------------------------------------------------
    static void* FindClassWithField( const char* className, const char* ns, const char* fieldName )
    {
        if ( !g_ApiOk )
                return nullptr;
        void* dom = g_Api.domain_get( );
        if ( !dom )
                return nullptr;
        size_t nAsm = 0;
        void** asms = g_Api.domain_get_assemblies( dom, &nAsm );
        if ( !asms )
                return nullptr;

        for ( size_t i = 0; i < nAsm; i++ )
        {
                void* img = g_Api.assembly_get_image( asms[ i ] );
                if ( !img )
                        continue;
                size_t n = g_Api.image_get_class_count( img );
                for ( size_t c = 0; c < n; c++ )
                {
                        void* k = g_Api.image_get_class( img, c );
                        if ( !k )
                                continue;
                        const char* kn = g_Api.class_get_name( k );
                        if ( !kn || strcmp( kn, className ) != 0 )
                                continue;
                        if ( ns )
                        {
                                const char* kns = g_Api.class_get_namespace( k );
                                if ( !kns || strcmp( kns, ns ) != 0 )
                                        continue;
                        }
                        if ( fieldName && !g_Api.class_get_field_from_name( k, fieldName ) )
                                continue;
                        return k;
                }
        }
        return nullptr;
    }

    static const char* TypeName( int t )
    {
        switch ( t )
        {
            case 0x02: return "bool";
            case 0x05: return "float";
            case 0x08: return "int32";
            case 0x0a: return "int64";
            case 0x0e: return "string";
            case 0x11: return "valuetype";
            case 0x12: return "class";
            case 0x15: return "genericinst";
            case 0x1d: return "szarray";
            default:   return "other";
        }
    }

    static void DumpField( void* klass, const char* className, const char* fieldName )
    {
        if ( !klass )
        {
                PROBE_LOGI( "%s : CLASSE NAO ENCONTRADA (nome/ns diferente nesta build?)", className );
                return;
        }
        void* f = g_Api.class_get_field_from_name( klass, fieldName );
        if ( !f )
        {
                PROBE_LOGI( "%s.%s : campo NAO ENCONTRADO", className, fieldName );
                return;
        }
        uint32_t flags = g_Api.field_get_flags( f );
        uint32_t off   = g_Api.field_get_offset( f );
        void*    t     = g_Api.field_get_type( f );
        int      tt    = t ? g_Api.type_get_type( t ) : -1;
        PROBE_LOGI( "%s.%s = 0x%x;  // %s (%s)", className, fieldName, off,
                    ( flags & 0x10 ) ? "STATIC" : "instance", TypeName( tt ) );
    }

    // ------------------------------------------------------------------
    // Mede o offset de static_fields na struct da classe: varre qwords do
    // objeto klass e aceita o candidato B em que B+offField contem um
    // objeto cujo primeiro qword (klass do objeto) == klassBack.
    // Imprime TODOS os candidatos que passarem (ideal: so 1).
    // ------------------------------------------------------------------
    static int MeasureSFOFF( void* klass, void* klassBack, uint32_t offField, const char* via )
    {
        if ( !klass || !klassBack )
                return 0;

        int found = 0;
        for ( uintptr_t S = 0x80; S <= 0x140; S += 8 )
        {
                uint64_t B = 0;
                if ( !SafeRead64( (uint64_t)klass + S, &B ) ) continue;
                if ( !Plausible( B ) )                       continue;

                uint64_t obj = 0;
                if ( !SafeRead64( B + offField, &obj ) ) continue;
                if ( !Plausible( obj ) )                     continue;

                uint64_t back = 0;
                if ( !SafeRead64( obj, &back ) )          continue;
                if ( back != (uint64_t)klassBack )           continue;

                found++;
                PROBE_LOGI( "SFOFF[%d] = 0x%lx  (validado via %s)  <<< candidato a AccessClass do perfil v8a",
                            found, (unsigned long)S, via );
        }
        return found;
    }

    static bool g_FieldsDumped = false;
    static bool g_SfoffDone    = false;

    // Chame 1x por rodada do rescan do dumper.
    static void RunV8AFullProbe( )
    {
        if ( !LoadApi( ) )
                return;
        LoadRanges( );

        // ---------- 1) Offsets reais da cadeia (puro API, zero risco) ----------
        if ( !g_FieldsDumped )
        {
                void* kFacade  = FindClassWithField( "GameFacade", nullptr, "CurrentMatchGame" );
                void* kMatchG  = FindClassWithField( "MatchGame",  nullptr, "m_Match" );
                void* kMatch   = FindClassWithField( "Match",      nullptr, "m_State" );
                void* kGameVar = FindClassWithField( "GameVarDef", nullptr, "RotationSensitivityMin" );
                void* kCCM     = FindClassWithField( "CameraControllerManager", nullptr, "m_Camera" );
                void* kCamera  = FindClassWithField( "Camera", "UnityEngine", "m_CachedPtr" );

                PROBE_LOGI( "==== V8A PROBE: offsets reais da cadeia ====" );
                DumpField( kFacade,  "GameFacade",             "CurrentMatchGame" );
                DumpField( kMatchG,  "MatchGame",              "m_Match" );
                DumpField( kMatch,   "Match",                  "m_State" );
                DumpField( kMatch,   "Match",                  "m_LocalPlayer" );
                DumpField( kMatch,   "Match",                  "m_LocalObserver" );
                DumpField( kMatch,   "Match",                  "m_AttackableEntities" );
                DumpField( kGameVar, "GameVarDef",             "ShootTraceAdjustmentDistanceThreshold" );
                DumpField( kGameVar, "GameVarDef",             "EnableAccelerationOnFalling" );
                DumpField( kGameVar, "GameVarDef",             "EnableLowFallingSwapWeapon" );
                DumpField( kGameVar, "GameVarDef",             "RotationSensitivityMin" );
                DumpField( kGameVar, "GameVarDef",             "RotationSensitivityMax" );
                DumpField( kGameVar, "GameVarDef",             "AimRotationSensitivityMin" );
                DumpField( kGameVar, "GameVarDef",             "AimRotationSensitivityMax" );
                DumpField( kCCM,     "CameraControllerManager","m_Camera" );
                DumpField( kCamera,  "Camera",                 "m_CachedPtr" );
                g_FieldsDumped = true;
        }

        // ---------- 2) SFOFF (AccessClass) ----------
        if ( g_SfoffDone )
                return;

        // 2a. via System.String.Empty — a classe String ja nasce inicializada,
        //     funciona ate no lobby.
        void* kStr = FindClassWithField( "String", "System", "Empty" );
        if ( kStr )
        {
                void* fEmpty = g_Api.class_get_field_from_name( kStr, "Empty" );
                uint32_t offEmpty = g_Api.field_get_offset( fEmpty );
                PROBE_LOGI( "V8A PROBE: medindo SFOFF via String.Empty (static off=0x%x)...", offEmpty );
                if ( MeasureSFOFF( kStr, kStr, offEmpty, "String.Empty" ) > 0 )
                {
                        g_SfoffDone = true;
                        return;
                }
        }
        else
        {
                PROBE_LOGW( "V8A PROBE: classe System.String nao encontrada" );
        }

        // 2b. via GameFacade.CurrentMatchGame -> klass do MatchGame —
        //     confirmacao forte; precisa estar EM PARTIDA.
        void* kFacade = FindClassWithField( "GameFacade", nullptr, "CurrentMatchGame" );
        void* kMatchG = FindClassWithField( "MatchGame",  nullptr, "m_Match" );
        if ( kFacade && kMatchG )
        {
                void*    fCur   = g_Api.class_get_field_from_name( kFacade, "CurrentMatchGame" );
                uint32_t offCur = g_Api.field_get_offset( fCur );
                if ( MeasureSFOFF( kFacade, kMatchG, offCur, "GameFacade.CurrentMatchGame" ) > 0 )
                {
                        g_SfoffDone = true;
                }
                else if ( kStr )
                {
                        PROBE_LOGW( "V8A PROBE: SFOFF ainda nao validado (fora de partida? CurrentMatchGame null no lobby). Re-roda em partida." );
                }
        }
    }
}
