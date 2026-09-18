// ============================================================================
// StormSysRW.hpp — READ/WRITE POR SYSCALL DIRETA (__NR_pread64/__NR_pwrite64)
// ============================================================================
// (Task 17) Substitui process_vm_readv/process_vm_writev e as wrappers da
// libc (pread/pwrite) por chamadas de kernel DIRETAS via syscall(__NR_*):
//
//   - Leitura : __NR_pread64  em /proc/<pid>/mem (fd aberto e fechado na hora)
//   - Escrita : __NR_pwrite64 em /proc/<pid>/mem (idem)
//   - Open    : __NR_openat   (nem o open() da libc é usado)
//   - Close   : __NR_close
//
// POR QUE ISSO REDUZ DETECÇÃO:
//   1. O binário para de IMPORTAR process_vm_readv/process_vm_writev — nomes
//      que são assinatura clássica de cheat na tabela de símbolos/PLT/GOT.
//   2. Nenhuma wrapper da libc de memória é chamada — não existe símbolo
//      pra hookar; a unica "api" usada é syscall(), genérica e onipresente.
//   3. O cache corta o VOLUME de syscalls (menos tráfego no /proc/<pid>/mem,
//      menos ruído estatístico, menos CPU). Escrita NUNCA é cacheada: sempre
//      direta, e invalida os blocos que tocam o range escrito.
//
// FUNCIONA EM 32 E 64 BITS (v7a e v8a):
//   - O que decide a FORMA do syscall é a ABI do PRÓPRIO daemon, não a do
//     alvo. O /proc/<pid>/mem recebe offset de 64 bits, então um daemon
//     64-bit lê tanto FF 32-bit (v7a) quanto FF 64-bit (v8a), e vice-versa.
//   - arm64 (v8a): o offset 64-bit passa como UM argumento (um registrador).
//   - arm (v7a): o kernel espera o offset 64-bit num PAR de registradores
//     ALINHADO PAR — r4=low, r5=high — com um argumento de PADDING em r3.
//     Ordem confirmada em 3 fontes: musl (__SYSCALL_LL_O = "0, lo, hi"),
//     stub AAPCS do bionic (64-bit cai em par par r4:r5) e o wrapper
//     syscall() do bionic (a4->r3, a5->r4, a6->r5).
//
// THREAD-SAFE: o daemon atende 1 thread por cliente; todo acesso ao cache
// é protegido por mutex e os contadores são atômicos.
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

#include <atomic>
#include <mutex>

namespace StormRW
{
    // ========================================================================
    // CAMADA 1 — SYSCALL CRU (sem passar por NENHUMA wrapper de memória)
    // ========================================================================

    /*
     * Números vindos de <sys/syscall.h> (corretos por arquitetura):
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
         * CAMINHO DE TESTE (só para validar sintaxe do split 32-bit em
         * compilação nativa). NUNCA usado em produção.
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
         * ABI 32-bit (daemon v7a): PADDING + LOW + HIGH (ver cabeçalho).
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
         * Não é usado no Android; mantido só pra compilar em qualquer lugar.
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
     * Abre /proc/<pid>/mem via __NR_openat (nem open() da libc é usado).
     * O fd existe SOMENTE durante a operação: abre-usa-fecha (higiene da
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
    // CAMADA 2 — CACHE DE LEITURA EM BLOCOS (corta o volume de syscalls)
    // ========================================================================
    /*
     * Como funciona:
     *   - Leitura é feita em BLOCOS de 256 bytes alinhados: um único
     *     preenche dezenas de pedidos pequenos (u8/u32/u64/float/vetores).
     *   - Cada bloco tem TTL curto (padrão 10 ms ~ 1 frame). Dado que muda
     *     a cada frame continua fresco; leitura repetida dentro do mesmo
     *     frame custa ZERO syscall.
     *   - Entrada "negativa": endereço que falhou (não mapeado / fd negado)
     *     fica lembrado pelo TTL pra não martelar endereço morto.
     *   - Cache é chaveado por (pid, bloco): troca de alvo nunca serve
     *     dado do PID velho.
     *   - Escrita: SEMPRE syscall direta + invalidação imediata de todos
     *     os blocos que tocam o range escrito (coerência garantida).
     *   - ttlMs = 0 desliga o cache (100% direto).
     */

    static constexpr size_t    kBlockShift   = 8;
    static constexpr size_t    kBlockSize    = (size_t)1 << kBlockShift; /* 256 B */
    static constexpr size_t    kSlotCount    = 2048;                     /* ~512 KB */
    static constexpr long long kDefaultTtlMs = 10;

    static_assert(
        (kSlotCount & (kSlotCount - 1)) == 0,
        "kSlotCount precisa ser potencia de 2"
    );

    struct Slot
    {
        pid_t     pid = 0;
        uint64_t  block = 0;     /* endereço do bloco (alinhado)          */
        long long tickMs = 0;    /* momento do preenchimento              */
        uint32_t  validLen = 0;  /* bytes válidos (0 = entrada negativa)  */
        bool      used = false;
        uint8_t   data[kBlockSize];
    };

    struct Stats
    {
        uint64_t hits;
        uint64_t negHits;
        uint64_t misses;
        uint64_t syscalls;
    };

    /*
     * Estado único do processo (static local de função inline = uma só
     * instância, mesmo com múltiplos TUs incluindo este header).
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

        return st;
    }

    /*
     * Leitura direta (sem cache) por cima de um fd já aberto.
     */
    inline bool
    ReadDirectFd(
        int fd,
        uint64_t address,
        void* buffer,
        size_t size
    )
    {
        char* out = static_cast<char*>(buffer);
        size_t total = 0;

        while (total < size)
        {
            ssize_t n =
                SysPread(
                    fd,
                    out + total,
                    size - total,
                    address + total
                );

            if (n < 0 && errno == EINTR)
                continue;

            if (n <= 0)
                return false;

            total += static_cast<size_t>(n);
        }

        return true;
    }

    /*
     * Leitura SEM cache: abre-usa-fecha. Disponível para casos críticos.
     */
    inline bool
    ReadMemNoCache(
        pid_t pid,
        uint64_t address,
        void* buffer,
        size_t size
    )
    {
        if (pid <= 0 || address == 0 || !buffer || size == 0)
            return false;

        const int fd =
            SysOpenMem(pid, false);

        if (fd < 0)
            return false;

        const bool ok =
            ReadDirectFd(fd, address, buffer, size);

        SysClose(fd);

        C().syscalls.fetch_add(3);

        return ok;
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
        if (pid <= 0 || address == 0 || !buffer || size == 0)
            return false;

        CacheState& s = C();

        /*
         * Cache desligado (ttl 0) = 100% direto.
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
                (now - slot.tickMs) <= s.ttlMs.load(std::memory_order_relaxed))
            {
                if (slot.validLen >= off + size)
                {
                    std::memcpy(
                        buffer,
                        slot.data + off,
                        size
                    );

                    s.hits.fetch_add(1);

                    return true;                /* HIT: zero syscall */
                }

                if (slot.validLen == 0)
                {
                    s.negHits.fetch_add(1);

                    return false;               /* HIT negativo */
                }
            }
        }

        /*
         * 2) MISS: preenche o bloco com UM pread64 (abre-usa-fecha).
         */
        const int fd =
            SysOpenMem(pid, false);

        if (fd < 0)
        {
            std::lock_guard<std::mutex> lk(s.mtx);

            Slot& slot =
                s.slots[SlotIndex(block)];

            slot.pid = pid;
            slot.block = block;
            slot.tickMs = now;
            slot.validLen = 0;
            slot.used = true;                   /* negativa: nem abriu */

            s.misses.fetch_add(1);

            return false;
        }

        uint8_t tmp[kBlockSize];

        ssize_t n;

        for (;;)
        {
            n =
                SysPread(
                    fd,
                    tmp,
                    kBlockSize,
                    block
                );

            if (n < 0 && errno == EINTR)
                continue;

            break;
        }

        SysClose(fd);

        s.syscalls.fetch_add(3);                /* open + pread + close */

        {
            std::lock_guard<std::mutex> lk(s.mtx);

            Slot& slot =
                s.slots[SlotIndex(block)];

            slot.pid = pid;
            slot.block = block;
            slot.tickMs = now;
            slot.used = true;

            s.misses.fetch_add(1);

            if (n <= 0)
            {
                slot.validLen = 0;              /* negativa: pread falhou */

                return false;
            }

            slot.validLen = (uint32_t)n;

            if ((size_t)n >= off + size)
            {
                std::memcpy(
                    buffer,
                    tmp + off,
                    size
                );

                return true;
            }
        }

        /*
         * O range pedido invade área não mapeada do bloco (o preenchedor
         * parou antes do fim do pedido): mesmo resultado do read direto.
         */
        return false;
    }

    /*
     * Escrita: SEMPRE direta, nunca cacheada. Depois de gravar, invalida
     * todo bloco que toca o range — nenhuma leitura posterior serve dado
     * velho.
     */
    inline bool
    WriteMem(
        pid_t pid,
        uint64_t address,
        const void* buffer,
        size_t size
    )
    {
        if (pid <= 0 || address == 0 || !buffer || size == 0)
            return false;

        CacheState& s = C();

        const int fd =
            SysOpenMem(pid, true);

        if (fd < 0)
            return false;

        const char* in =
            static_cast<const char*>(buffer);

        size_t total = 0;
        bool ok = true;

        while (total < size)
        {
            ssize_t n =
                SysPwrite(
                    fd,
                    in + total,
                    size - total,
                    address + total
                );

            if (n < 0 && errno == EINTR)
                continue;

            if (n <= 0)
            {
                ok = false;

                break;
            }

            total += static_cast<size_t>(n);
        }

        SysClose(fd);

        s.syscalls.fetch_add(3);

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

        return ok;
    }

} /* namespace StormRW */
