// ============================================================================
// StormSysRW.hpp — READ/WRITE POR SYSCALL DIRETA (__NR_pread64/__NR_pwrite64)
// ============================================================================
// (RWSYSCALL-V8 — substitui a versao da Task 17/PONTEFIX-V5)
// (V8.1 — re-adiciona GetLastErrno()/GetRetryCount() usados pelo
//  daemon_main do RWFIX-V7; grava errno tambem nas falhas de ESCRITA)
//
// ARQUITETURA (o que continua valendo):
//   - Leitura : __NR_pread64  em /proc/<pid>/mem (fd aberto e fechado na hora)
//   - Escrita : __NR_pwrite64 em /proc/<pid>/mem (idem)
//   - Open    : __NR_openat   (nem o open() da libc e usado)
//   - Close   : __NR_close
//   - Cache de blocos de 256 B com TTL curto (10 ms ~ 1 frame)
//   - Escrita NUNCA cacheada: sempre direta + invalidacao dos blocos tocados
//
// POR QUE ISSO REDUZ DETECÇÃO (mantido):
//   1. O binario para de IMPORTAR process_vm_readv/process_vm_writev — nomes
//      que sao assinatura classica de cheat na tabela de simbolos/PLT/GOT.
//   2. Nenhuma wrapper da libc de memoria e chamada; a unica "api" usada e
//      syscall(), generica e onipresente.
//   3. O cache corta o VOLUME de syscalls (menos ruido estatistico, menos CPU).
//
// ============================================================================
// (V8) O QUE MUDOU — correcao do "ora sim ora nao" em partida (v7a):
// ============================================================================
// O log de campo mostrou: GameFacade lendo 0 numa chamada e o valor real na
// re-leitura imediata; Match+0x8C (state) lendo 1 e Match+0x94 (LocalPlayer)
// lendo 0 no MESMO frame; cadeia quebrando em passos alternados (x56
// GameFacade, x114 LocalPlayer). Tres buracos na camada de leitura explicam:
//
//   1. ENTRADA NEGATIVA NA 1a FALHA: qualquer pread de bloco que falhasse
//      UMA vez (janela transitória: pagina descartada sob pressao de
//      memoria, GC do jogo, scheduler) envenenava o BLOCO INTEIRO de 256 B
//      por 10 ms. Os campos m_State(0x8C), m_LocalPlayer(0x94) e
//      m_LocalObserver(0xB4) do Match CAEM NO MESMO BLOCO (Match
//      page-aligned) — uma unica falha derrubava state E LocalPlayer E
//      observer ao mesmo tempo, e a cadeia quebrava num passo diferente a
//      cada frame. AGORA: entrada negativa so depois de 2+ falhas
//      CONSECUTIVAS do mesmo bloco, e o TTL negativo ficou mais curto.
//
//   2. FALLBACK EXATO COM 1 TENTATIVA SO: janela transitória de leitura nao
//      e atravessada por 1 tentativa. AGORA: ReadExactFdRetry() retry
//      limitado (padrao: 12 tentativas, sono crescente 80us..1.28ms),
//      EINTR/EAGAIN sem sono, errnos transitorios (EIO/EFAULT/ESRCH/
//      ENOENT/ENOMEM) com sono escalonado, errno fatal (EPERM/EBADF/
//      EINVAL) falha imediata.
//
//   3. SEM CANAL SECUNDARIO: a Task 17 removeu o process_vm_readv (que era
//      o canal principal ANTERIOR e funcionava). AGORA volta como FALLBACK
//      POR CHAMADA, dentro do daemon root, via SYSCALL DIRETA
//      (SYS_process_vm_readv/SYS_process_vm_writev — mesmo nivel de bypass:
//      roda no processo root, nunca no app; o binario do APP continua sem
//      importar esses simbolos). O pread64 direto continua sendo o canal
//      PRINCIPAL; o vm_readv so entra depois que o pread64 esgotou as
//      re-tentativas daquela leitura.
//
// TELEMETRIA (V8): contadores atomicos novos (retries, exactFb, vmFbReads,
// vmFbWrites, negCreated, openFails, lastReadErrno, lastReadStage) expostos
// no GetStats() e impressos no [STATS] do daemon — o proximo logcat PROVA
// qual canal falhou e com qual errno, sem chute.
//
// FUNCIONA EM 32 E 64 BITS (v7a e v8a) — split do offset 64-bit mantido:
//   - arm64 (v8a): o offset 64-bit passa como UM argumento.
//   - arm  (v7a): kernel espera o offset 64-bit num PAR de registradores
//     ALINHADO PAR — r4=low, r5=high — com PADDING em r3.
//     Ordem confirmada em 3 fontes: musl (__SYSCALL_LL_O = "0, lo, hi"),
//     stub AAPCS do bionic (64-bit cai em par par r4:r5) e o wrapper
//     syscall() do bionic (a4->r3, a5->r4, a6->r5).
//
// THREAD-SAFE: o daemon atende 1 thread por cliente; todo acesso ao cache
// e protegido por mutex e os contadores sao atomicos.
// ============================================================================

#pragma once

#include <sys/syscall.h>

#include <fcntl.h>
#include <unistd.h>

#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>

#if defined(SYS_process_vm_readv) || defined(SYS_process_vm_writev)
#include <sys/uio.h>
#endif

#include <atomic>
#include <mutex>

namespace StormRW
{
    // ========================================================================
    // CAMADA 1 — SYSCALL CRU (sem passar por NENHUMA wrapper de memoria)
    // ========================================================================

    /*
     * Numeros vindos de <sys/syscall.h> (corretos por arquitetura):
     *   arm64 (v8a): __NR_pread64 = 67  | __NR_pwrite64 = 68
     *   arm   (v7a): __NR_pread64 = 328 | __NR_pwrite64 = 329
     */

    inline ssize_t SysPread(
        int fd,
        void* buf,
        size_t count,
        uint64_t offset
    )
    {
#if defined(STORM_FORCE_SPLIT32)
        /*
         * CAMINHO DE TESTE (só para validar sintoma do split 32-bit em
         * compilacao nativa). NUNCA usado em producao.
         */
        const unsigned long long off =
            static_cast<unsigned long long>(offset);

        return static_cast<ssize_t>(
            ::syscall(
                static_cast<long>(__NR_pread64),
                fd,
                buf,
                count,
                0UL,
                static_cast<unsigned long>(off & 0xFFFFFFFFULL),
                static_cast<unsigned long>(off >> 32)
            ));
#elif defined(__aarch64__) || defined(__x86_64__) || defined(__LP64__)
        /*
         * ABI 64-bit (daemon v8a): offset 64-bit passa inteiro.
         */
        return static_cast<ssize_t>(
            ::syscall(
                static_cast<long>(__NR_pread64),
                fd,
                buf,
                count,
                static_cast<long long>(offset)
            ));
#elif defined(__arm__)
        /*
         * ABI 32-bit (daemon v7a): PADDING + LOW + HIGH (ver cabecalho).
         */
        const unsigned long long off =
            static_cast<unsigned long long>(offset);

        return static_cast<ssize_t>(
            ::syscall(
                static_cast<long>(__NR_pread64),
                fd,
                buf,
                count,
                0UL,                                             /* padding r3 */
                static_cast<unsigned long>(off & 0xFFFFFFFFULL), /* r4 = low   */
                static_cast<unsigned long>(off >> 32)            /* r5 = high  */
            ));
#else
        /*
         * Outra ABI 32-bit (ex.: i386): par (low, high) sem padding.
         * Nao e usado no Android; mantido so pra compilar em qualquer lugar.
         */
        const unsigned long long off =
            static_cast<unsigned long long>(offset);

        return static_cast<ssize_t>(
            ::syscall(
                static_cast<long>(__NR_pread64),
                fd,
                buf,
                count,
                static_cast<unsigned long>(off & 0xFFFFFFFFULL),
                static_cast<unsigned long>(off >> 32)
            ));
#endif
    }

    inline ssize_t SysPwrite(
        int fd,
        const void* buf,
        size_t count,
        uint64_t offset
    )
    {
#if defined(STORM_FORCE_SPLIT32)
        const unsigned long long off =
            static_cast<unsigned long long>(offset);

        return static_cast<ssize_t>(
            ::syscall(
                static_cast<long>(__NR_pwrite64),
                fd,
                buf,
                count,
                0UL,
                static_cast<unsigned long>(off & 0xFFFFFFFFULL),
                static_cast<unsigned long>(off >> 32)
            ));
#elif defined(__aarch64__) || defined(__x86_64__) || defined(__LP64__)
        return static_cast<ssize_t>(
            ::syscall(
                static_cast<long>(__NR_pwrite64),
                fd,
                buf,
                count,
                static_cast<long long>(offset)
            ));
#elif defined(__arm__)
        const unsigned long long off =
            static_cast<unsigned long long>(offset);

        return static_cast<ssize_t>(
            ::syscall(
                static_cast<long>(__NR_pwrite64),
                fd,
                buf,
                count,
                0UL,
                static_cast<unsigned long>(off & 0xFFFFFFFFULL),
                static_cast<unsigned long>(off >> 32)
            ));
#else
        const unsigned long long off =
            static_cast<unsigned long long>(offset);

        return static_cast<ssize_t>(
            ::syscall(
                static_cast<long>(__NR_pwrite64),
                fd,
                buf,
                count,
                static_cast<unsigned long>(off & 0xFFFFFFFFULL),
                static_cast<unsigned long>(off >> 32)
            ));
#endif
    }

    /*
     * Abre /proc/<pid>/mem via __NR_openat (nem open() da libc e usado).
     * O fd existe SOMENTE durante a operacao: abre-usa-fecha (higiene da
     * Task 12 mantida — nenhum handle persistente exposto no /proc).
     */
    inline int SysOpenMem(
        pid_t pid,
        bool writeMode
    )
    {
        char path[64];

        std::snprintf(
            path,
            sizeof(path),
            "/proc/%d/mem",
            static_cast<int>(pid)
        );

        const int flags =
            writeMode
                ? (O_WRONLY | O_CLOEXEC)
                : (O_RDONLY | O_CLOEXEC);

        return static_cast<int>(
            ::syscall(
                static_cast<long>(__NR_openat),
                static_cast<int>(AT_FDCWD),
                path,
                flags,
                0UL
            ));
    }

    inline void SysClose(
        int fd
    )
    {
        if (fd >= 0)
        {
            ::syscall(
                static_cast<long>(__NR_close),
                fd
            );
        }
    }

    // ========================================================================
    // (V8) CLASSIFICACAO DE ERRNO — transitorio x fatal
    // ========================================================================
    /*
     * TRANSITORIO: pode passar na proxima tentativa (janela de pagina
     * descartada, EAGAIN do kernel, processo em transicao). Vale re-tentar
     * com sono curto.
     * FATAL: re-tentar e desperdicio (EPERM = permissoes, EBADF = fd morto,
     * EINVAL = argumento). Falha imediata da etapa.
     */
    inline bool
    IsTransientErrno(
        int e
    )
    {
        switch (e)
        {
            case EIO:       /* pagina nao presente / range parcialmente morto */
            case EAGAIN:    /* kernel pediu pra tentar de novo                */
            case EFAULT:    /* range tocou area invalida NAQUELE instante     */
            case ESRCH:     /* processo em transicao (thread group)           */
            case ENOENT:    /* /proc/pid sumindo e voltando                   */
            case ENOMEM:    /* pressao de memoria momentanea                  */
                return true;

            default:
                return false;
        }
    }

    /*
     * Sono escalonado entre tentativas: 80us -> 160 -> 320 -> 640 -> 1280us
     * (teto). Total do pior caso com 12 tentativas ~ 8-10 ms por leitura
     * FALHA — o caminho feliz (1 pread OK) nao paga nada.
     */
    inline void
    RetrySleep(
        int attempt
    )
    {
        int shift = attempt;

        if (shift > 4)
            shift = 4;

        if (shift < 0)
            shift = 0;

        struct timespec ts{};

        ts.tv_sec = 0;
        ts.tv_nsec = (80L << shift) * 1000L;   /* 80us * 2^shift */

        nanosleep(&ts, nullptr);
    }

    // ========================================================================
    // (V8) CANAL SECUNDARIO — process_vm_readv/writev por SYSCALL DIRETA
    // ========================================================================
    /*
     * O canal que era principal ANTES da Task 17 e que o usuario confirmou
     * estavel ("a cadeia antiga funcionava deboa"). Volta como PLANO B por
     * chamada: so roda depois que o pread64 esgotou as re-tentativas.
     *
     * Bypass mantido: a chamada acontece DENTRO do daemon root (nunca no
     * app), via syscall() crua — o binario do app nao importa o simbolo.
     * Complementa o pread64 porque falha em condicoes diferentes:
     *   - pread64 em /proc/pid/mem falha EIO quando a PAGINA esta ausente;
     *   - process_vm_readv usa gup() com flags ligeiramente diferentes e
     *     consegue atravessar janelas onde o pread64 da EIO (e vice-versa).
     */
    inline bool
    ReadVmFallback(
        pid_t pid,
        uint64_t address,
        void* buffer,
        size_t size
    )
    {
#if defined(SYS_process_vm_readv)

        if (pid <= 0 || address == 0 || !buffer || size == 0)
            return false;

        char* out = static_cast<char*>(buffer);
        size_t total = 0;
        int attempts = 0;

        while (total < size)
        {
            iovec local{};

            local.iov_base = out + total;
            local.iov_len = size - total;

            iovec remote{};

            remote.iov_base =
                reinterpret_cast<void*>(
                    static_cast<uintptr_t>(address + total)
                );

            remote.iov_len = size - total;

            errno = 0;

            ssize_t n =
                static_cast<ssize_t>(
                    ::syscall(
                        SYS_process_vm_readv,
                        pid,
                        &local,
                        1,
                        &remote,
                        1,
                        0UL
                    ));

            if (n < 0)
            {
                const int e = errno;

                if (e == EINTR)
                {
                    if (++attempts > 8)
                        break;

                    continue;                       /* sem sono            */
                }

                if (!IsTransientErrno(e))
                    break;                          /* fatal: nao insiste  */

                if (++attempts > 4)
                    break;

                RetrySleep(attempts);

                continue;
            }

            if (n == 0)
            {
                if (++attempts > 4)
                    break;

                RetrySleep(attempts);

                continue;
            }

            total += static_cast<size_t>(n);
        }

        return total == size;

#else

        (void)pid;
        (void)address;
        (void)buffer;
        (void)size;

        return false;

#endif
    }

    inline bool
    WriteVmFallback(
        pid_t pid,
        uint64_t address,
        const void* buffer,
        size_t size
    )
    {
#if defined(SYS_process_vm_writev)

        if (pid <= 0 || address == 0 || !buffer || size == 0)
            return false;

        const char* in = static_cast<const char*>(buffer);
        size_t total = 0;
        int attempts = 0;

        while (total < size)
        {
            iovec local{};

            local.iov_base =
                const_cast<void*>(
                    static_cast<const void*>(in + total)
                );

            local.iov_len = size - total;

            iovec remote{};

            remote.iov_base =
                reinterpret_cast<void*>(
                    static_cast<uintptr_t>(address + total)
                );

            remote.iov_len = size - total;

            errno = 0;

            ssize_t n =
                static_cast<ssize_t>(
                    ::syscall(
                        SYS_process_vm_writev,
                        pid,
                        &local,
                        1,
                        &remote,
                        1,
                        0UL
                    ));

            if (n < 0)
            {
                const int e = errno;

                if (e == EINTR)
                {
                    if (++attempts > 8)
                        break;

                    continue;
                }

                if (!IsTransientErrno(e))
                    break;

                if (++attempts > 4)
                    break;

                RetrySleep(attempts);

                continue;
            }

            if (n == 0)
            {
                if (++attempts > 4)
                    break;

                RetrySleep(attempts);

                continue;
            }

            total += static_cast<size_t>(n);
        }

        return total == size;

#else

        (void)pid;
        (void)address;
        (void)buffer;
        (void)size;

        return false;

#endif
    }

    // ========================================================================
    // CAMADA 2 — CACHE DE LEITURA EM BLOCOS (corta o volume de syscalls)
    // ========================================================================
    /*
     * Como funciona:
     *   - Leitura e feita em BLOCOS de 256 bytes alinhados: um unico
     *     preenche dezenas de pedidos pequenos (u8/u32/u64/float/vetores).
     *   - Cada bloco tem TTL curto (padrao 10 ms ~ 1 frame). Dado que muda
     *     a cada frame continua fresco; leitura repetida dentro do mesmo
     *     frame custa ZERO syscall.
     *   - (V8) Entrada "negativa" so e criada depois de 2+ falhas
     *     CONSECUTIVAS do mesmo bloco nos dois canais (pread64 + vm_readv):
     *     uma falha transitória unica NAO envenena mais o bloco. TTL
     *     negativo = metade do TTL normal (min 5 ms).
     *   - Cache e chaveado por (pid, bloco): troca de alvo nunca serve
     *     dado do PID velho.
     *   - Escrita: SEMPRE syscall direta (+ fallback vm_writev) +
     *     invalidacao imediata dos blocos que tocam o range escrito.
     *   - ttlMs = 0 desliga o cache (100% direto).
     */

    static constexpr size_t    kBlockShift   = 8;
    static constexpr size_t    kBlockSize    = (size_t)1 << kBlockShift; /* 256 B  */
    static constexpr size_t    kSlotCount    = 2048;                     /* ~512KB */
    static constexpr long long kDefaultTtlMs = 10;

    static_assert(
        (kSlotCount & (kSlotCount - 1)) == 0,
        "kSlotCount precisa ser potencia de 2"
    );

    struct Slot
    {
        pid_t     pid = 0;
        uint64_t  block = 0;     /* endereco do bloco (alinhado)             */
        long long tickMs = 0;    /* momento do preenchimento                 */
        uint32_t  validLen = 0;  /* bytes validos (0 = entrada negativa)     */
        uint32_t  fails = 0;     /* (V8) falhas consecutivas do bloco        */
        bool      used = false;
        uint8_t   data[kBlockSize];
    };

    struct Stats
    {
        uint64_t hits;
        uint64_t negHits;
        uint64_t misses;
        uint64_t syscalls;

        /* (V8) telemetria do caminho de leitura/escrita */
        uint64_t retries;      /* pread64 repetidos (parcial/errno)        */
        uint64_t exactFb;      /* leituras salvas pelo range exato         */
        uint64_t vmFbReads;    /* leituras salvas pelo process_vm_readv    */
        uint64_t vmFbWrites;   /* escritas salvas pelo process_vm_writev   */
        uint64_t negCreated;   /* entradas negativas criadas               */
        uint64_t openFails;    /* openat de /proc/pid/mem falhou           */
        uint32_t lastReadErrno;/* ultimo errno de falha definitiva de RW   */
        uint32_t lastReadStage;/* 0=nada 1=bloco 2=exato 3=vm 4=open       */
    };

    /*
     * Estado unico do processo (static local de funcao inline = uma so
     * instancia, mesmo com multiplos TUs incluindo este header).
     */
    struct CacheState
    {
        std::mutex mtx;
        Slot slots[kSlotCount];
        std::atomic<long long> ttlMs{ kDefaultTtlMs };
        std::atomic<uint64_t> hits{ 0 };
        std::atomic<uint64_t> negHits{ 0 };
        std::atomic<uint64_t> misses{ 0 };
        std::atomic<uint64_t> syscalls{ 0 };

        /* (V8) */
        std::atomic<uint64_t> retries{ 0 };
        std::atomic<uint64_t> exactFb{ 0 };
        std::atomic<uint64_t> vmFbReads{ 0 };
        std::atomic<uint64_t> vmFbWrites{ 0 };
        std::atomic<uint64_t> negCreated{ 0 };
        std::atomic<uint64_t> openFails{ 0 };
        std::atomic<uint32_t> lastReadErrno{ 0 };
        std::atomic<uint32_t> lastReadStage{ 0 };
    };

    inline CacheState&
    C()
    {
        static CacheState state;
        return state;
    }

    inline long long
    NowMs()
    {
        struct timespec ts{};

        clock_gettime(
            CLOCK_MONOTONIC,
            &ts
        );

        return (long long)ts.tv_sec * 1000LL +
               ts.tv_nsec / 1000000LL;
    }

    inline size_t
    SlotIndex(
        uint64_t block
    )
    {
        uint64_t h =
            block * 0x9E3779B97F4A7C15ULL;

        h ^= h >> 29;
        h *= 0xBF58476D1CE4E5B9ULL;

        return (size_t)(h & (kSlotCount - 1));
    }

    // ========================================================================
    // API PÚBLICA
    // ========================================================================

    inline void
    SetTtlMs(
        long long ms
    )
    {
        if (ms < 0)
            ms = 0;

        if (ms > 60000)
            ms = 60000;

        C().ttlMs.store(ms);
    }

    inline long long
    GetTtlMs()
    {
        return C().ttlMs.load();
    }

    inline void
    FlushCache()
    {
        CacheState& s = C();

        std::lock_guard<std::mutex> lk(s.mtx);

        for (size_t i = 0; i < kSlotCount; i++)
            s.slots[i].used = false;
    }

    inline Stats
    GetStats()
    {
        CacheState& s = C();

        Stats st{};

        st.hits = s.hits.load();
        st.negHits = s.negHits.load();
        st.misses = s.misses.load();
        st.syscalls = s.syscalls.load();

        st.retries = s.retries.load();
        st.exactFb = s.exactFb.load();
        st.vmFbReads = s.vmFbReads.load();
        st.vmFbWrites = s.vmFbWrites.load();
        st.negCreated = s.negCreated.load();
        st.openFails = s.openFails.load();
        st.lastReadErrno = s.lastReadErrno.load();
        st.lastReadStage = s.lastReadStage.load();

        return st;
    }

    /*
     * (V8.1) COMPAT RWFIX-V7: o daemon_main do RWFIX-V7 loga
     * [READ]/[WRITE] FALHOU com StormRW::GetLastErrno(). Retorna o
     * errno da ultima falha DEFINITIVA de leitura OU escrita
     * (0 = nenhuma falha registrada ainda). Falha salva pelo canal
     * secundario (process_vm) NAO conta como falha aqui.
     */
    inline int
    GetLastErrno()
    {
        return (int)C().lastReadErrno.load(
            std::memory_order_relaxed
        );
    }

    /*
     * (V8.1) COMPAT RWFIX-V7: total de retries pread64/pwrite64
     * (parcial + EINTR/EAGAIN + errno transitorio + EOF).
     */
    inline uint64_t
    GetRetryCount()
    {
        return C().retries.load(
            std::memory_order_relaxed
        );
    }

    // ========================================================================
    // (V8) LEITURA EXATA COM RETRY — coracao do conserto
    // ========================================================================
    /*
     * Le `size` bytes a partir de `address` por cima de um fd ja aberto:
     *   - leitura PARCIAL (0 < n < size): avanca e continua (pread64 em
     *     /proc/pid/mem pode voltar curto perto de fim de mapeamento);
     *   - EINTR/EAGAIN: tenta de novo na hora (sem sono);
     *   - errno transitorio (EIO/EFAULT/...): sono escalonado e re-tenta;
     *   - errno fatal: falha imediata.
     *
     * Pior caso: kMaxAttempts tentativas ~ 8-10 ms. Caminho feliz: 1 syscall.
     */
    inline bool
    ReadExactFdRetry(
        int fd,
        uint64_t address,
        void* buffer,
        size_t size,
        int maxAttempts = 12
    )
    {
        CacheState& s = C();

        char* out = static_cast<char*>(buffer);
        size_t total = 0;
        int attempt = 0;

        while (total < size)
        {
            ssize_t n =
                SysPread(
                    fd,
                    out + total,
                    size - total,
                    address + total
                );

            if (n < 0)
            {
                const int e = errno;

                s.lastReadErrno.store(
                    (uint32_t)e,
                    std::memory_order_relaxed
                );

                if (e == EINTR || e == EAGAIN)
                {
                    s.retries.fetch_add(1);

                    if (++attempt > maxAttempts)
                        return false;

                    continue;                   /* imediato, sem sono      */
                }

                if (!IsTransientErrno(e))
                    return false;               /* EPERM/EBADF/EINVAL...   */

                s.retries.fetch_add(1);

                if (++attempt > maxAttempts)
                    return false;

                RetrySleep(attempt);

                continue;
            }

            if (n == 0)
            {
                /* 0 bytes lidos: pagina sumiu na hora do pread. */
                s.retries.fetch_add(1);

                if (++attempt > maxAttempts)
                    return false;

                RetrySleep(attempt);

                continue;
            }

            total += static_cast<size_t>(n);
        }

        return true;
    }

    /*
     * Leitura direta (sem cache) por cima de um fd ja aberto.
     * (Retrocompatibilidade: era o ReadDirectFd antigo, agora com retry.)
     */
    inline bool
    ReadDirectFd(
        int fd,
        uint64_t address,
        void* buffer,
        size_t size
    )
    {
        return ReadExactFdRetry(fd, address, buffer, size);
    }

    /*
     * Leitura SEM cache: abre-usa-fecha. Disponivel para casos criticos.
     * (V8) com fallback vm_readv se o pread64 esgotar.
     */
    inline bool
    ReadMemNoCache(
        pid_t pid,
        uint64_t address,
        void* buffer,
        size_t size
    )
    {
        CacheState& s = C();

        if (pid <= 0 || address == 0 || !buffer || size == 0)
            return false;

        const int fd =
            SysOpenMem(pid, false);

        if (fd < 0)
        {
            s.openFails.fetch_add(1);
            s.lastReadStage.store(4, std::memory_order_relaxed);
            s.lastReadErrno.store(
                (uint32_t)errno,
                std::memory_order_relaxed
            );

            /* (V8) openat falhou: tenta o canal secundario direto. */
            if (ReadVmFallback(pid, address, buffer, size))
            {
                s.vmFbReads.fetch_add(1);

                return true;
            }

            return false;
        }

        const bool ok =
            ReadExactFdRetry(fd, address, buffer, size);

        SysClose(fd);

        C().syscalls.fetch_add(3);

        if (ok)
            return true;

        /* (V8) plano B: canal secundario. */
        if (ReadVmFallback(pid, address, buffer, size))
        {
            s.vmFbReads.fetch_add(1);
            s.lastReadStage.store(3, std::memory_order_relaxed);

            return true;
        }

        return false;
    }

    /*
     * Leitura COM cache (caminho principal da ponte).
     */
    inline bool
    ReadMem(
        pid_t pid,
        uint64_t address,
        void* buffer,
        size_t size
    )
    {
        CacheState& s = C();

        if (pid <= 0 || address == 0 || !buffer || size == 0)
            return false;

        /*
         * Cache desligado (ttl 0) = 100% direto (com retry + fallback V8).
         */
        if (s.ttlMs.load(std::memory_order_relaxed) <= 0)
            return ReadMemNoCache(pid, address, buffer, size);

        /*
         * Leitura que atravessa dois blocos (ou maior que o bloco) vai
         * direta — caso raro (leituras grandes tipo skeleton).
         */
        const size_t off =
            (size_t)(address & (kBlockSize - 1));

        if (off + size > kBlockSize)
            return ReadMemNoCache(pid, address, buffer, size);

        const uint64_t block =
            address & ~(uint64_t)(kBlockSize - 1);

        const long long now =
            NowMs();

        const long long ttl =
            s.ttlMs.load(std::memory_order_relaxed);

        /*
         * 1) Tenta servir do cache.
         */
        {
            std::lock_guard<std::mutex> lk(s.mtx);

            Slot& slot =
                s.slots[SlotIndex(block)];

            if (slot.used &&
                slot.pid == pid &&
                slot.block == block &&
                (now - slot.tickMs) <= ttl)
            {
                if (slot.validLen >= off + size)
                {
                    std::memcpy(
                        buffer,
                        slot.data + off,
                        size
                    );

                    s.hits.fetch_add(1);

                    return true;                /* HIT: zero syscall      */
                }

                if (slot.validLen == 0)
                {
                    s.negHits.fetch_add(1);

                    return false;               /* HIT negativo           */
                }

                /*
                 * Bloco parcialmente valido e o pedido passa do fim:
                 * cai no preenchimento abaixo (re-tenta completar).
                 */
            }
        }

        /*
         * 2) MISS: preenche o bloco com UM pread64 (abre-usa-fecha).
         */
        const int fd =
            SysOpenMem(pid, false);

        if (fd < 0)
        {
            s.openFails.fetch_add(1);
            s.lastReadStage.store(4, std::memory_order_relaxed);
            s.lastReadErrno.store(
                (uint32_t)errno,
                std::memory_order_relaxed
            );

            /*
             * (V8) openat falhou. ANTES: entrada negativa na hora
             * (envenenava o bloco inteiro por 10 ms). AGORA: tenta o
             * canal secundario; so marca negativa depois de 2 falhas
             * consecutivas do MESMO bloco.
             */
            if (ReadVmFallback(pid, address, buffer, size))
            {
                s.vmFbReads.fetch_add(1);
                s.lastReadStage.store(3, std::memory_order_relaxed);

                std::lock_guard<std::mutex> lk(s.mtx);

                Slot& slot = s.slots[SlotIndex(block)];

                if (slot.pid == pid && slot.block == block)
                    slot.fails = 0;             /* voltou: zera contador  */

                return true;
            }

            std::lock_guard<std::mutex> lk(s.mtx);

            Slot& slot =
                s.slots[SlotIndex(block)];

            slot.pid = pid;
            slot.block = block;
            /* (V8) carimbo de tempo NA HORA da gravacao: o caminho de
             * falha pode ter gasto ~12 ms em retries antes de chegar
             * aqui; usar o 'now' do inicio da chamada faria a entrada
             * nascer praticamente expirada (negHit nunca servia).      */
            slot.tickMs = NowMs();

            slot.fails++;

            s.misses.fetch_add(1);

            if (slot.fails >= 2)
            {
                /* (V8) negativa so apos falha consecutiva (TTL/2). */
                slot.validLen = 0;
                slot.used = true;

                s.negCreated.fetch_add(1);
            }
            else
            {
                slot.used = false;              /* 1a falha: NAO envenena */
            }

            return false;
        }

        uint8_t tmp[kBlockSize];

        ssize_t n;
        int fillAttempt = 0;

        for (;;)
        {
            n =
                SysPread(
                    fd,
                    tmp,
                    kBlockSize,
                    block
                );

            if (n < 0)
            {
                const int e = errno;

                s.lastReadErrno.store(
                    (uint32_t)e,
                    std::memory_order_relaxed
                );

                if (e == EINTR || e == EAGAIN)
                {
                    s.retries.fetch_add(1);

                    if (++fillAttempt > 4)
                        break;

                    continue;
                }

                break;                          /* erro real: cai no else */
            }

            break;
        }

        /*
         * (PONTEFIX-V5, ampliado no V8) FALLBACK EXATO COM RETRY: o bloco
         * de 256 bytes e preenchido com UM pread a partir do endereco
         * ALINHADO. Se o fim do bloco invade area nao mapeada (fim de
         * regiao / PROT_NONE — comum no heap do jogo), o pread do bloco
         * falha INTEIRO ou volta parcial mesmo quando o range pedido
         * (menor, dentro do objeto) e 100% legivel. A leitura EXATA do
         * range pedido agora re-tenta ate atravessar janelas transitórias
         * (pagina descartada, GC do jogo) antes de desistir.
         */
        bool exactOk = false;

        if (n <= 0 || (size_t)n < off + size)
        {
            exactOk =
                ReadExactFdRetry(
                    fd,
                    address,
                    buffer,
                    size
                );

            if (exactOk)
                s.exactFb.fetch_add(1);
        }

        SysClose(fd);

        s.syscalls.fetch_add(3);                /* open + pread + close   */

        /*
         * (V8) PLANO B: pedido NAO satisfeito pelo pread64 — fill falhou
         * OU fill parcial + fallback exato falhou. Canal secundario
         * (process_vm_readv, syscall direta no daemon root) — o canal que
         * era principal antes da Task 17 e que historicamente atravessava
         * janelas onde o pread64 falha.
         *
         * ATENCAO (bugfix do primeiro build V8): "exactOk" so e true
         * quando o fallback exato RODOU e funcionou. Um fill COMPLETO
         * (n >= off+size) nao roda o exato — se usassemos "!exactOk"
         * sozinho, TODA leitura bem-sucedida cairia no plano B,
         * desperdicando uma syscall extra e nunca povoando o cache
         * (hit/miss zerados, vmR subindo a cada leitura). Por isso o
         * guardo "fillSatisfied" abaixo.
         */
        const bool fillSatisfied =
            (n > 0) && ((size_t)n >= off + size);

        if (!exactOk && !fillSatisfied)
        {
            if (ReadVmFallback(pid, address, buffer, size))
            {
                s.misses.fetch_add(1);
                s.vmFbReads.fetch_add(1);
                s.lastReadStage.store(3, std::memory_order_relaxed);

                std::lock_guard<std::mutex> lk(s.mtx);

                Slot& slot = s.slots[SlotIndex(block)];

                if (slot.pid == pid && slot.block == block)
                    slot.fails = 0;

                return true;
            }

            s.lastReadStage.store(1, std::memory_order_relaxed);
        }

        {
            std::lock_guard<std::mutex> lk(s.mtx);

            Slot& slot =
                s.slots[SlotIndex(block)];

            slot.pid = pid;
            slot.block = block;
            /* (V8) idem: carimbo na hora da gravacao. */
            slot.tickMs = NowMs();

            s.misses.fetch_add(1);

            if (n <= 0)
            {
                if (exactOk)
                {
                    /*
                     * (PONTEFIX-V5) leitura exata funcionou: NAO marca
                     * entrada negativa — o proximo pedido neste bloco
                     * tenta de novo normalmente.
                     */
                    slot.used = false;
                    slot.fails = 0;

                    return true;
                }

                slot.fails++;

                if (slot.fails >= 2)
                {
                    /* (V8) negativa so apos falha consecutiva. */
                    slot.validLen = 0;
                    slot.used = true;

                    s.negCreated.fetch_add(1);
                }
                else
                {
                    slot.used = false;          /* 1a falha: NAO envenena */
                }

                return false;
            }

            /*
             * (V8) BUGFIX CRITICO — a causa raiz do "ora sim ora nao" em
             * partida: o codigo da Task 17 armazenava slot.validLen = n
             * mas NUNCA copiava os dados de tmp para slot.data. Resultado:
             * o MISS servia dado real (de tmp), e TODO HIT posterior no
             * mesmo bloco de 256 B (dentro do TTL de 10 ms) servia ZEROS
             * (slot.data zero-filled na inicializacao estatica).
             * Os campos m_State(0x8C), m_LocalPlayer(0x94) e
             * m_LocalObserver(0xB4) caem NO MESMO bloco quando o Match e
             * page-aligned: a cadeia lia state=1 (miss, dado real) e
             * LocalPlayer=0 (hit, cache zerado) no mesmo frame — e a
             * "janela de zero na pagina" do GameFacade era o mesmo
             * mecanismo. Sem a copia abaixo, o cache e uma fabrica de
             * zeros com TTL de 10 ms.
             */
            std::memcpy(
                slot.data,
                tmp,
                ((size_t)n < kBlockSize) ? (size_t)n : kBlockSize
            );

            slot.validLen = (uint32_t)n;
            slot.used = true;
            slot.fails = 0;

            if ((size_t)n >= off + size)
            {
                std::memcpy(
                    buffer,
                    tmp + off,
                    size
                );

                return true;
            }

            if (exactOk)
                return true;                    /* (PONTEFIX-V5)          */
        }

        /*
         * O range pedido invade área nao mapeada do bloco (o preenchedor
         * parou antes do fim do pedido): mesmo resultado do read direto.
         */
        return false;
    }

    /*
     * Escrita: SEMPRE direta, nunca cacheada.
     * (V8) re-tentativa para errno transitorio + canal secundario
     * (process_vm_writev) se o pwrite64 esgotar. Depois de gravar,
     * invalida todo bloco que toca o range — nenhuma leitura posterior
     * serve dado velho.
     */
    inline bool
    WriteMem(
        pid_t pid,
        uint64_t address,
        const void* buffer,
        size_t size
    )
    {
        CacheState& s = C();

        if (pid <= 0 || address == 0 || !buffer || size == 0)
            return false;

        const int fd =
            SysOpenMem(pid, true);

        bool ok = false;
        int lastErr = 0; /* (V8.1) errno p/ telemetria GetLastErrno */

        if (fd >= 0)
        {
            const char* in =
                static_cast<const char*>(buffer);

            size_t total = 0;
            int attempt = 0;

            ok = true;

            while (total < size)
            {
                ssize_t n =
                    SysPwrite(
                        fd,
                        in + total,
                        size - total,
                        address + total
                    );

                if (n < 0)
                {
                    const int e = errno;

                    if (e == EINTR || e == EAGAIN)
                    {
                        s.retries.fetch_add(1);

                        if (++attempt > 8)
                        {
                            ok = false;
                            lastErr = e;

                            break;
                        }

                        continue;
                    }

                    if (IsTransientErrno(e))
                    {
                        s.retries.fetch_add(1);

                        if (++attempt > 8)
                        {
                            ok = false;
                            lastErr = e;

                            break;
                        }

                        RetrySleep(attempt);

                        continue;
                    }

                    ok = false;
                    lastErr = e;

                    break;
                }

                if (n == 0)
                {
                    s.retries.fetch_add(1);

                    if (++attempt > 8)
                    {
                        ok = false;
                        lastErr = EIO; /* EOF na escrita = EIO */

                        break;
                    }

                    RetrySleep(attempt);

                    continue;
                }

                total += static_cast<size_t>(n);
            }

            SysClose(fd);

            s.syscalls.fetch_add(3);
        }
        else
        {
            lastErr = errno; /* (V8.1) errno do openat que falhou */

            s.openFails.fetch_add(1);
        }

        /*
         * (V8) canal secundario de escrita: process_vm_writev direto.
         * Escrita e operacao critica (exploit/aim) — um plano B aqui
         * evita perda silenciosa de escrita numa janela transitória.
         */
        if (!ok)
        {
            if (WriteVmFallback(pid, address, buffer, size))
            {
                s.vmFbWrites.fetch_add(1);

                ok = true;
            }
        }

        if (ok)
        {
            std::lock_guard<std::mutex> lk(s.mtx);

            const uint64_t first =
                address & ~(uint64_t)(kBlockSize - 1);

            const uint64_t last =
                (address + (size - 1)) & ~(uint64_t)(kBlockSize - 1);

            for (uint64_t b = first; ; b += kBlockSize)
            {
                Slot& slot =
                    s.slots[SlotIndex(b)];

                if (slot.used &&
                    slot.pid == pid &&
                    slot.block == b)
                {
                    slot.used = false;
                }

                if (b >= last)
                    break;
            }
        }

        /*
         * (V8.1) telemetria: grava o errno da falha definitiva de
         * ESCRITA (o caminho de leitura ja grava nos seus proprios
         * pontos de falha). Falha salva pelo fallback vm nao chega
         * aqui (ok == true).
         */
        if (!ok)
        {
            s.lastReadErrno.store(
                (uint32_t)lastErr,
                std::memory_order_relaxed
            );
        }

        return ok;
    }

} /* namespace StormRW */