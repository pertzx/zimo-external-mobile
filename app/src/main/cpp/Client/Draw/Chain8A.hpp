#pragma once
// ============================================================================
// Chain8A.hpp — subsistema da cadeia TypeInfo/AccessClass para arm64-v8a
// ============================================================================
//
// PROBLEMA QUE ISTO RESOLVE (v8a):
//
//   A cadeia do ESP comeca em:
//     GameFacade_TypeInfo = *(u64*)(libBase + RVA_do_slot)
//     statics             = *(klass + AccessClass)          <- offsetof(Il2CppClass, static_fields)
//     MatchGame           = *(statics + CurrentMatchGame)
//     Match               = *(MatchGame + m_Match)
//     state               = *(int*)(Match + m_State)
//
//   No v8a o slot do TypeInfo comeca a vida guardando um TOKEN lazy de
//   metadata-usage (ex: 0x2001B431 = flag 0x20000000 | indice 111665) e so e
//   substituido IN-PLACE pelo ponteiro real do Il2CppClass quando o codigo da
//   classe roda pela primeira vez. Enquanto o slot guarda token:
//     - 0xB8/0x6C/qualquer AccessClass e irrelevante (nao ha klass valido);
//     - o auto-probe nao tem como validar nada (morre no passo 1);
//     - a cadeia inteira fica morta com klass = token.
//
//   O QUE ESTE ARQUIVO FAZ:
//     1. Le o slot de forma TOKEN-AWARE (v7a mantem leitura u32 original);
//     2. Quando acha token: loga decodificado (rate-limit), varre a janela
//        vizinha (diagnostico de regiao) e tenta RVAs alternativos
//        configuraveis (TypeInfoAltRva[3]);
//     3. Mezura o AccessClass real do v8a por probe: so confirma um candidato
//        B se a SUBCADEIA REAL inteira fizer sentido
//        (statics -> CurrentMatchGame -> m_Match -> m_State ∈ [-1..5]);
//        o valor confirmado fica em cache. Enquanto nao confirma, usa o
//        fallback (0xB8 = valor padrao do il2cpp 29, PALPITE — nao medida).
//
//   SEGURANCA: 100% das leituras vao pela ponte externa (g_FreeFireMemory)
//   — nada desreferencia ponteiro do proprio processo, zero risco de crash.
//
//   LOGCAT: tag "Chain8A" (procure por [SLOT] e [SFOFF]).
//
// ============================================================================

#include <cstdint>
#include <cinttypes>
#include <time.h>
#include <android/log.h>

#include <Memory/Memory.hpp>
#include <Offsets/Offsets.hpp>

#define CHAIN8A_LOG( ... )  __android_log_print( ANDROID_LOG_INFO, "Chain8A", __VA_ARGS__ )
#define CHAIN8A_WARN( ... ) __android_log_print( ANDROID_LOG_WARN, "Chain8A", __VA_ARGS__ )

namespace Chain8A
{
	// ==================== CONFIGURACAO EDITAVEL ====================

	// RVAs ALTERNATIVOS do slot TypeInfo. Se o offsetdumper rodado NESTA build
	// apontar para OUTRO RVA (jogo atualizou / array de usage mudou de lugar),
	// preencha aqui e o cliente tenta automaticamente quando o slot principal
	// estiver em TOKEN. 0 = desativado. NAO substitui o perfil de Offsets —
	// e uma rede de seguranca para nao precisar recompilar perfil.
	static uintptr_t TypeInfoAltRva[ 3 ] = { 0, 0, 0 };

	// AccessClass (offsetof Il2CppClass.static_fields) fallback do v8a enquanto
	// o probe nao confirma o valor real. 0xB8 = padrao do il2cpp 29 — PALPITE,
	// pois o il2cpp do FF e modificado (no v7a o FF usa 0x5C onde o padrao 24.x
	// seria 0x54). O probe abaixo EXISTE justamente para medir o valor real.
	static uintptr_t AccessClassV8AFallback = 0xB8;

	// ==================== ESTADO INTERNO ====================

	static uintptr_t s_AccessClassV8A   = 0;  // valor ja CONFIRMADO pelo probe
	static long long s_LastProbeMs      = 0;
	static long long s_LastProbeFailMs  = 0;
	static long long s_LastTokenLogMs   = 0;
	static long long s_LastWindowMs     = 0;

	// ------------------------------------------------------------------

	static long long NowMs( )
	{
		struct timespec ts;
		clock_gettime( CLOCK_MONOTONIC, &ts );
		return ( long long )ts.tv_sec * 1000LL + ts.tv_nsec / 1000000LL;
	}

	// No v8a um ponteiro valido deste processo (heap/mmap do Android) sempre
	// tem a dword alta nao-zero e alinhamento de 8. Qualquer valor que caiba
	// em 32 bits aqui e TOKEN de metadata-usage (lazy), nunca ponteiro.
	static bool LooksLikePointer64( uint64_t v )
	{
		if ( v == 0 )                return false;
		if ( ( v >> 32 ) == 0 )      return false;   // cabe em 32 bits = token
		if ( v < 0x1000 )            return false;
		if ( ( v & 0x7 ) != 0 )      return false;   // Il2CppClass* e 8-aligned
		return true;
	}

	// ==================== DIAGNOSTICO DE TOKEN ====================
	// Tudo com rate-limit para nao inundar o logcat (a cadeia roda ~60x/s).

	static void TokenDiag( uintptr_t libBase, uintptr_t rva, uint64_t v, const char* tag )
	{
		long long now = NowMs( );

		// ---- decode do token (1x / 10s) ----
		if ( now - s_LastTokenLogMs > 10000 )
		{
			s_LastTokenLogMs = now;

			uint32_t raw        = ( uint32_t )v;
			uint32_t idx        = raw & 0x1FFFFFFF;
			bool     isTypeInfo = ( ( raw & 0xE0000000 ) == 0x20000000 );

			CHAIN8A_WARN(
				"[SLOT] %s: lib+0x%llX = 0x%08X = TOKEN lazy metadata-usage (%s idx=%u). "
				"Jogo ainda NAO converteu o slot em ponteiro; cadeia retesta todo frame. "
				"Se nao resolver DENTRO DA PARTIDA, rode o offsetdumper DE NOVO nesta build.",
				tag,
				( unsigned long long )rva,
				raw,
				isTypeInfo ? "TypeInfo" : "outro",
				idx );

			if ( TypeInfoAltRva[ 0 ] == 0 && TypeInfoAltRva[ 1 ] == 0 && TypeInfoAltRva[ 2 ] == 0 )
			{
				CHAIN8A_WARN(
					"[SLOT] Dica: se o dumper apontar outro RVA nesta build, preencha "
					"Chain8A::TypeInfoAltRva[3] no topo do Chain8A.hpp e recompile." );
			}
		}

		// ---- janela ±0x40 (1x / 30s): diagnostico da REGIAO ----
		if ( now - s_LastWindowMs > 30000 )
		{
			s_LastWindowMs = now;

			int      tokens      = 0;
			int      ponteiros   = 0;
			int      zeros       = 0;
			uintptr_t primeiroPtr = 0;

			for ( int d = -0x40; d <= 0x40; d += 8 )
			{
				uint64_t w = 0;
				if ( !g_FreeFireMemory.Read<uint64_t>( libBase + rva + ( uintptr_t )d, w ) )
					continue;

				if ( w == 0 )                { zeros++;     continue; }
				if ( ( w >> 32 ) == 0 )      { tokens++;    continue; }

				ponteiros++;
				if ( primeiroPtr == 0 )
					primeiroPtr = rva + ( uintptr_t )d;
			}

			CHAIN8A_WARN(
				"[SLOT] janela lib+0x%llX +-0x40: %d tokens, %d ponteiros, %d zeros. "
				"So tokens = o array de metadata-usage INTEIRO esta sem inicializar "
				"nesta sessao (problema de regiao/build, nao deste RVA so).",
				( unsigned long long )rva, tokens, ponteiros, zeros );

			if ( primeiroPtr != 0 && primeiroPtr != rva )
			{
				CHAIN8A_WARN(
					"[SLOT] primeiro ponteiro da janela: lib+0x%llX (slot de OUTRA classe — "
					"use so como referencia para cruzar com o offsetdumper).",
					( unsigned long long )primeiroPtr );
			}
		}
	}

	// ==================== LEITURA DO SLOT TypeInfo (token-aware) ====================
	//
	// v7a (N32=true): le u32 — comportamento IDENTICO ao de antes.
	// v8a           : le u64; token -> diagnostico + RVAs alternativos -> 0
	//                 (a cadeia quebra neste frame e retesta no proximo;
	//                 quando o jogo converter o slot, a cadeia volta sozinha).
	//
	// Retorna o Il2CppClass* (0 = indisponivel neste frame).

	static uintptr_t ReadTypeInfoClass( bool N32, uintptr_t libBase, uintptr_t rva, const char* tag )
	{
		if ( libBase == 0 || rva == 0 )
			return 0;

		if ( N32 )
		{
			uint32_t v = 0;
			if ( !g_FreeFireMemory.Read<uint32_t>( libBase + rva, v ) )
				return 0;
			return ( uintptr_t )v;
		}

		// ------------------ v8a ------------------
		uint64_t v = 0;
		if ( !g_FreeFireMemory.Read<uint64_t>( libBase + rva, v ) )
			return 0;

		if ( v == 0 )
			return 0;

		if ( LooksLikePointer64( v ) )
			return ( uintptr_t )v;                       // caminho normal

		// -------- slot em TOKEN (lazy metadata-usage) --------
		TokenDiag( libBase, rva, v, tag );

		// RVAs alternativos configurados? tenta em ordem.
		for ( int i = 0; i < 3; i++ )
		{
			uintptr_t alt = TypeInfoAltRva[ i ];
			if ( alt == 0 || alt == rva )
				continue;

			uint64_t av = 0;
			if ( !g_FreeFireMemory.Read<uint64_t>( libBase + alt, av ) )
				continue;
			if ( !LooksLikePointer64( av ) )
				continue;

			CHAIN8A_LOG(
				"[SLOT] %s: ALT RVA lib+0x%llX tem ponteiro valido (0x%llX). "
				"Atualize o perfil para este RVA — o fallback esta mantendo a cadeia viva.",
				tag, ( unsigned long long )alt, ( unsigned long long )av );

			return ( uintptr_t )av;
		}

		return 0;
	}

	// ==================== AccessClass (offsetof static_fields) ====================
	//
	// N32  (v7a)               : devolve Offsets::AccessClass — valor MEDIDO do
	//                            perfil (FF 24.x = 0x5C). Nada muda.
	// v8a + probeValidate=true : GameFacade — probe mede o AccessClass real.
	// v8a + probeValidate=false: GameVarDef — nao existe subcadeia para validar,
	//                            entao reusa o valor JA confirmado pelo probe
	//                            (AccessClass e layout da struct Il2CppClass —
	//                            o MESMO para todas as classes) ou o fallback.
	//
	// Retorna o endereco dos static_fields (0 = indisponivel neste frame).

	static uintptr_t ResolveAccessClass( bool N32, uintptr_t klass, bool probeValidate )
	{
		if ( klass == 0 )
			return 0;

		if ( N32 )
			return Offsets::AccessClass;

		// valor confirmado 1x vale para sempre (layout da lib nao muda em runtime)
		if ( s_AccessClassV8A != 0 )
			return s_AccessClassV8A;

		if ( !probeValidate )
			return AccessClassV8AFallback;

		// ------------------ auto-probe v8a (1x / 2s) ------------------
		// So converge DENTRO de uma partida: precisa de
		// CurrentMatchGame -> m_Match -> m_State reais para validar.
		long long now = NowMs( );
		if ( now - s_LastProbeMs < 2000 )
			return AccessClassV8AFallback;
		s_LastProbeMs = now;

		// Validacao de UM candidato B: a subcadeia real inteira precisa fazer sentido.
		auto ProbeCandidate = [ klass ]( uintptr_t B ) -> bool
		{
			uint64_t statics = 0;
			if ( !g_FreeFireMemory.Read<uint64_t>( klass + B, statics ) )
				return false;
			if ( !LooksLikePointer64( statics ) )
				return false;

			uint64_t cmg = 0;
			if ( !g_FreeFireMemory.Read<uint64_t>( statics + Offsets::GameFacade::CurrentMatchGame, cmg ) )
				return false;
			if ( !LooksLikePointer64( cmg ) )
				return false;

			uint64_t match = 0;
			if ( !g_FreeFireMemory.Read<uint64_t>( cmg + Offsets::MatchGame::m_Match, match ) )
				return false;
			if ( !LooksLikePointer64( match ) )
				return false;

			int state = 0;
			if ( !g_FreeFireMemory.Read<int>( match + Offsets::Match::m_State, state ) )
				return false;
			if ( state < -1 || state > 5 )           // MatchState enum: Lobby..Unknown
				return false;

			return true;
		};

		// 1) candidatos prioritarios (padroes il2cpp 27/29 + vizinhos)
		static const uintptr_t priority[] = { 0xB8, 0xB0, 0xC0, 0xA8, 0x98, 0xD0 };
		uintptr_t confirmed = 0;

		for ( uintptr_t B : priority )
		{
			if ( ProbeCandidate( B ) ) { confirmed = B; break; }
		}

		// 2) varredura completa 0x90..0x120 passo 8 (pula os ja testados)
		if ( confirmed == 0 )
		{
			for ( uintptr_t B = 0x90; B <= 0x120; B += 8 )
			{
				bool skip = false;
				for ( uintptr_t p : priority )
				{
					if ( p == B ) { skip = true; break; }
				}
				if ( skip )
					continue;

				if ( ProbeCandidate( B ) ) { confirmed = B; break; }
			}
		}

		if ( confirmed != 0 )
		{
			s_AccessClassV8A = confirmed;
			CHAIN8A_LOG(
				"[SFOFF] AccessClass v8a CONFIRMADO por probe = 0x%llX "
				"(subcadeia real statics->CurrentMatchGame->m_Match->m_State OK). "
				"Atualize Offsets::AccessClass do perfil v8a para este valor.",
				( unsigned long long )confirmed );
			return confirmed;
		}

		if ( now - s_LastProbeFailMs > 15000 )
		{
			s_LastProbeFailMs = now;
			CHAIN8A_WARN(
				"[SFOFF] probe nao validou nenhum AccessClass nesta rodada "
				"(precisa de partida ATIVA + offsets de campo corretos no perfil v8a). "
				"Usando fallback 0x%llX. Se persistir em partida, rode o V8AFullProbe "
				"do offsetdumper para medir SFOFF e os offsets de campo reais.",
				( unsigned long long )AccessClassV8AFallback );
		}

		return AccessClassV8AFallback;
	}

} // namespace Chain8A
