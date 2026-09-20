// ============================================================================
//  Chain8A.hpp — correção da cadeia TypeInfo -> static_fields para v8a (64-bit)
//  StormMemory / zimo-external-mobile — V8A-CHAIN-FIX
//
//  PROBLEMA
//  --------
//  A cadeia do ReadLoop usa:
//      klass   = ReadPtr(LibIl2Cpp + TypeInfo_RVA)      -> Il2CppClass*
//      statics = ReadPtr(klass + Offsets::AccessClass)  -> static_fields
//      campo   = ReadPtr(statics + off_do_campo)
//
//  Offsets::AccessClass é offsetof(Il2CppClass, static_fields) — um offset de
//  ESTRUTURA, que depende do layout do il2cpp de cada build:
//      • v7a (32-bit): 0x5C → correto, a cadeia sempre funcionou.
//      • v8a (64-bit): os ponteiros da struct dobram de tamanho e o layout
//        muda. No il2cpp 29 (Unity 2021.2+ — a mesma geração que introduziu
//        os slots lazy/token que o OffsetDumper detectou no seu log),
//        static_fields fica em 0xB8.
//  Com 0x5C numa struct 64-bit a leitura cai no MEIO do ponteiro 'parent'
//  (bytes 0x5C..0x63 = metade de parent + metade de generic_class) → lixo
//  → a cadeia morre no primeiro salto, mesmo com o offset do TypeInfo
//  perfeito. Por isso o OffsetDumper confirma 0xAC1E768 e o ESP v8a
//  mesmo assim não funciona.
//
//  SOLUÇÃO
//  -------
//  1) ReadTypeInfoClass(): leitura do slot com guarda contra token lazy.
//     Em il2cpp novo o slot .data começa com o token codificado (ex.:
//     0x2001B431 = (1<<29)|111665) e só é substituído pelo Il2CppClass*
//     real quando o código da classe roda pela primeira vez. No v8a,
//     valor < 2^32 = token (não é ponteiro) → aguardar próximo frame.
//  2) ResolveAccessClass(): no v8a auto-proba o offset de static_fields
//     validando a sub-cadeia REAL (statics -> CurrentMatchGame -> m_Match
//     -> m_State válido). Leituras EXTERNAS pela ponte (seguras — não
//     derrubam o jogo). Cacheia o valor que passar. Se o 0xB8 do perfil
//     estiver certo, valida na primeira partida; se estiver errado, ele
//     descobre sozinho e loga o valor para você fixar.
// ============================================================================

#pragma once

#include <cstdint>
#include <cinttypes>
#include <chrono>
#include <android/log.h>

#include <Memory/Memory.hpp>
#include <Offsets/Offsets.hpp>

#define CHAIN8A_LOGI(...) __android_log_print(ANDROID_LOG_INFO, "StormChain", __VA_ARGS__)
#define CHAIN8A_LOGW(...) __android_log_print(ANDROID_LOG_WARN, "StormChain", __VA_ARGS__)

namespace Chain8A
{
        static int64_t NowMs( )
        {
                return std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now( ).time_since_epoch( ) ).count( );
        }

        // Ponteiro plausível de userspace arm64: heap/pools de metadata ficam
        // acima de 4 GiB. Token de metadata-usage (lazy) é < 2^32.
        static bool PlausiblePtr64( uint64_t v )
        {
                return v >= 0x100000000ull && v < 0x800000000000ull;
        }

        // ------------------------------------------------------------------
        // 1) Leitura do slot TypeInfo com guarda de token lazy.
        //    v7a: u32 normal (comportamento idêntico ao anterior).
        //    v8a: u64; valor que couber em 32 bits é token (slot ainda não
        //    resolvido pelo jogo) -> loga e devolve 0 (retry no próx. frame).
        // ------------------------------------------------------------------
        static uintptr_t ReadTypeInfoClass( bool N32, uintptr_t libBase, uintptr_t typeInfoRva, const char* tag )
        {
                if ( libBase == 0 || typeInfoRva == 0 )
                        return 0;

                if ( N32 )
                        return g_FreeFireMemory.Read<uint32_t>( libBase + typeInfoRva );

                uint64_t v = g_FreeFireMemory.Read<uint64_t>( libBase + typeInfoRva );
                if ( v == 0 )
                        return 0;

                if ( ( v >> 32 ) == 0 )
                {
                        static int64_t s_LastTokenLog = 0;
                        int64_t now = NowMs( );
                        if ( now - s_LastTokenLog > 5000 )
                        {
                                s_LastTokenLog = now;
                                CHAIN8A_LOGW( "[V8A] slot %s ainda com TOKEN lazy 0x%08llX — classe ainda não usada pelo jogo; aguardando resolver...",
                                              tag, (unsigned long long)v );
                        }
                        return 0;
                }

                return (uintptr_t)v;
        }

        // ------------------------------------------------------------------
        // 2) offsetof(Il2CppClass, static_fields):
        //    • v7a -> Offsets::AccessClass (0x5C), intocado.
        //    • v8a -> auto-probe com validação da sub-cadeia real + cache.
        //      Probe no máximo 1x a cada 2s (precisa estar EM PARTIDA, pois
        //      GameFacade.CurrentMatchGame só é não-nulo dentro dela).
        // ------------------------------------------------------------------
        static uintptr_t ResolveAccessClass( bool N32, uintptr_t klass )
        {
                if ( N32 || klass == 0 )
                        return Offsets::AccessClass;

                static uintptr_t s_Detected = 0;
                static int64_t   s_LastTry  = 0;

                if ( s_Detected )
                        return s_Detected;

                int64_t now = NowMs( );
                if ( now - s_LastTry < 2000 )
                        return Offsets::AccessClass;            // fallback: valor do perfil v8a
                s_LastTry = now;

                const uintptr_t offCurMatchGame = Offsets::GameFacade::CurrentMatchGame;
                const uintptr_t offMatch        = Offsets::MatchGame::m_Match;
                const uintptr_t offState        = Offsets::Match::m_State;
                if ( offCurMatchGame == 0 || offMatch == 0 || offState == 0 )
                        return Offsets::AccessClass;

                // Layouts 64-bit conhecidos primeiro (il2cpp 29 = 0xB8,
                // 27 = 0xB0, 24.x = 0xA8), depois varredura ampla por garantia.
                static const uintptr_t kPreferred[] = { 0xB8, 0xB0, 0xA8 };
                uintptr_t cand[ 24 ];
                int n = 0;
                for ( uintptr_t c : kPreferred )
                        cand[ n++ ] = c;
                for ( uintptr_t c = 0x90; c <= 0x120 && n < 24; c += 8 )
                {
                        bool dup = false;
                        for ( uintptr_t p : kPreferred )
                                if ( p == c ) { dup = true; break; }
                        if ( !dup )
                                cand[ n++ ] = c;
                }

                for ( int i = 0; i < n; i++ )
                {
                        uint64_t statics   = 0;
                        uint64_t matchGame = 0;
                        uint64_t match     = 0;
                        int32_t  state     = 0;

                        if ( !g_FreeFireMemory.Read<uint64_t>( klass + cand[ i ], statics ) )           continue;
                        if ( !PlausiblePtr64( statics ) )                                               continue;
                        if ( !g_FreeFireMemory.Read<uint64_t>( statics + offCurMatchGame, matchGame ) ) continue;
                        if ( !PlausiblePtr64( matchGame ) )                                             continue;
                        if ( !g_FreeFireMemory.Read<uint64_t>( matchGame + offMatch, match ) )          continue;
                        if ( !PlausiblePtr64( match ) )                                                 continue;
                        if ( !g_FreeFireMemory.Read<int32_t>( match + offState, state ) )               continue;
                        if ( state < -1 || state > 5 )                                                  continue; // enum MatchState

                        s_Detected = cand[ i ];
                        CHAIN8A_LOGI( "[V8A] SFOFF validado = 0x%lX (MatchState=%d) — fixe este valor no perfil v8a (AccessClass)",
                                      (unsigned long)cand[ i ], state );
                        return s_Detected;
                }

                return Offsets::AccessClass;
        }
}
