/*
 * ============================================================================
 * stormdaemon - PONTE ROOT DE READ/WRITE
 * ============================================================================
 *
 * Este executável vive em /data/local/tmp/stormdaemon e roda como root
 * (iniciado via su pelo DaemonService). Ele é SOMENTE UMA PONTE:
 *
 *   - recebe pedidos READ/WRITE/FIND_PID/MODULE_BASE/IS_32BIT do client
 *     (libclient.so) por um Unix socket,
 *   - executa no processo alvo com privilégio root,
 *   - responde o resultado.
 *
 * NÃO existe aqui: offsets, lógica do jogo, config, ESP, nada. Qualquer
 * inteligência fica no client. Se o daemon morre, o client cai no fallback
 * local e o Java (DaemonService) reinicia a ponte.
 *
 * Log: logcat (tag "StormBridge") + arquivo /data/local/tmp/stormbridge.log
 *
 * Uso:
 *   /data/local/tmp/stormdaemon
 *   /data/local/tmp/stormdaemon --socket /data/local/tmp/stormbridge.sock
 * ============================================================================
 */

#include <Shared/Bridge/BridgeProtocol.hpp>
#include <Shared/Bridge/StormSysRW.hpp>
#include <Daemon/RTmodules.h>

#include <android/log.h>

#include <chrono>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <signal.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/prctl.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

#include <atomic>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#define LOG_TAG "StormBridge"

/*
 * ============================================================================
 * STEALTH (Task 12) — O DAEMON NAO SE ENTREGA MAIS
 * ============================================================================
 * Antes: logcat com tag "StormBridge" (leitor de logcat com root achava
 * na hora) + arquivo /data/local/tmp/stormbridge.log crescente em disco
 * (evidencia estatica que varredura de anti-cheat acha por nome).
 *
 * Agora: NADA de log por padrao. LOGx so fala com --verbose (debug);
 * FileLog so escreve se --log for passado explicitamente. O Java NAO
 * passa mais --log. Em producao o daemon e MUDO e nao deixa rastro.
 * ============================================================================
 */
static std::atomic<int> g_Verbose{ 0 };
static std::atomic<int> g_LogFileEnabled{ 0 };

#define LOGI(...) \
    do { if (g_Verbose.load()) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__); } while (0)

#define LOGW(...) \
    do { if (g_Verbose.load()) __android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__); } while (0)

#define LOGE(...) \
    do { if (g_Verbose.load()) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__); } while (0)

/*
 * ============================================================================
 * ESTADO GLOBAL DO DAEMON
 * ============================================================================
 */

static std::string g_SocketPath =
    "/data/local/tmp/stormbridge.sock";

static std::string g_PidFilePath =
    "/data/local/tmp/stormbridge.pid";

static std::string g_LogFilePath =
    "/data/local/tmp/stormbridge.log";

static std::atomic<bool> g_Stop{ false };
static std::atomic<uint64_t> g_TotalReads{ 0 };
static std::atomic<uint64_t> g_TotalWrites{ 0 };
static std::atomic<uint64_t> g_TotalErrors{ 0 };

/* (V8.7) instante do start (CLOCK_MONOTONIC, ms) para uptime nos STATS */
static const long long g_StartMs = [] {
    struct timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000LL + ts.tv_nsec / 1000000LL;
}();

static long long NowMsDaemon()
{
    struct timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000LL + ts.tv_nsec / 1000000LL;
}
static std::atomic<uint64_t> g_TotalConnections{ 0 };

/*
 * ============================================================================
 * (V10 KERNEL) MODO KERNEL — READ/WRITE VIA DRIVER (RTmodules.h)
 * ============================================================================
 * Quando o usuario liga o toggle "Kernel RW" no painel (BRIDGE_CMD_KERNEL_SET),
 * TODO READ/WRITE da ponte passa a sair por ioctl do driver de kernel — sem
 * pread64/pwrite64, sem abrir /proc/pid/mem (nada de handles no /proc, nada
 * que anti-cheat enumere). O toggle e estado RUNTIME: o client re-aplica
 * sozinho apos qualquer reconexao do daemon.
 * ============================================================================
 */
static std::atomic<int> g_KernelMode{ 0 };
static std::atomic<uint64_t> g_KernelReads{ 0 };
static std::atomic<uint64_t> g_KernelWrites{ 0 };
static std::atomic<uint64_t> g_KernelErrors{ 0 };
static std::atomic<long long> g_KernelLastFbLogMs{ -1000000 };
static std::atomic<long long> g_KernelLastReprobeMs{ -1000000 };

/*
 * Log em arquivo (append). Cada linha é gravada com um único write()
 * já que o Java/logs externos podem ler o arquivo a qualquer momento.
 */
static std::mutex g_LogFileMutex;

static void FileLog(
    const char* format,
    ...
)
{
    /* MUDO por padrao: so grava se --log foi passado (debug). */
    if (!g_LogFileEnabled.load())
        return;

    std::lock_guard<std::mutex> lock(g_LogFileMutex);

    FILE* fp =
        fopen(
            g_LogFilePath.c_str(),
            "a"
        );

    if (!fp)
        return;

    struct timespec ts{};

    clock_gettime(
        CLOCK_REALTIME,
        &ts
    );

    struct tm tmBuf{};

    localtime_r(
        &ts.tv_sec,
        &tmBuf
    );

    fprintf(
        fp,
        "[%02d-%02d %02d:%02d:%02d.%03ld] ",
        tmBuf.tm_mon + 1,
        tmBuf.tm_mday,
        tmBuf.tm_hour,
        tmBuf.tm_min,
        tmBuf.tm_sec,
        ts.tv_nsec / 1000000L
    );

    va_list args;

    va_start(args, format);

    vfprintf(
        fp,
        format,
        args
    );

    va_end(args);

    fputc('\n', fp);

    fclose(fp);
}

/*
 * ============================================================================
 * OPERAÇÕES DE MEMÓRIA (o coração da ponte)
 * ============================================================================
 */

namespace
{
    /*
     * ============================================================================
     * STEALTH (Task 17) — READ/WRITE 100% SYSCALL DIRETA + CACHE
     * ============================================================================
     * Substitui o caminho antigo (process_vm_readv/writev + wrappers libc
     * pread/pwrite) por syscalls de kernel DIRETAS, sem nenhum fallback:
     *
     *     __NR_openat    -> abre /proc/<pid>/mem (abre-usa-fecha: nenhum
     *                       handle persistente exposto no /proc)
     *     __NR_pread64   -> leitura (com cache de blocos de 256 B, TTL 10 ms)
     *     __NR_pwrite64  -> escrita (SEMPRE direta + invalidacao do cache)
     *     __NR_close     -> fecha
     *
     * Motivacao anti-deteccao:
     *   - o binario para de IMPORTAR process_vm_readv/process_vm_writev
     *     (nomes que sao assinatura classica de cheat em PLT/GOT/simbolos);
     *   - nenhuma wrapper de memoria da libc e chamada (nada pra hookar);
     *   - o cache corta o VOLUME de syscalls: leitura repetida no mesmo
     *     frame custa ZERO syscall (menos ruido, menos CPU);
     *   - funciona em v7a e v8a: a ABI do syscall e a do PROPRIO daemon
     *     (o split do offset 64-bit em 32-bit fica no StormSysRW.hpp).
     *
     * Os caminhos antigos ficam COMENTADOS no bloco #if 0 logo abaixo.
     * NAO existe caminho alternativo: so a syscall direta roda.
     * ============================================================================
     */

    /*
     * ============================================================================
     * (V10 KERNEL) WRAPPERS DE MEMORIA VIA DRIVER DE KERNEL (RTmodules.h)
     * ============================================================================
     * RT::KernelRead/KernelWrite com pid EXPLICITO por chamada — o daemon
     * atende varios clientes em threads separadas, e o ioctl e a unica
     * parte "stateful". Contadores alimentam STATS/KERNEL_STATUS.
     * ============================================================================
     */

    bool KernelReadMem(
        pid_t pid,
        uint64_t address,
        void* buffer,
        size_t size
    )
    {
        if (RT::KernelRead(pid, (uintptr_t)address, buffer, size))
        {
            g_KernelReads++;
            return true;
        }

        g_KernelErrors++;
        return false;
    }

    bool KernelWriteMem(
        pid_t pid,
        uint64_t address,
        const void* buffer,
        size_t size
    )
    {
        if (RT::KernelWrite(pid, (uintptr_t)address, buffer, size))
        {
            g_KernelWrites++;
            return true;
        }

        g_KernelErrors++;
        return false;
    }

    /*
     * Contingencia (V10 KERNEL): toggle LIGADO mas device sumiu na sessao
     * (driver descarregado / device removido). Cai pro caminho pread64
     * pra o client NAO MORRER junto, com LOG CLARO 1x/10s. Aproveita pra
     * re-probe 1x/15s (o usuario pode ter re-carregado o modulo).
     */
    void LogKernelFallbackOnce()
    {
        const long long now = NowMsDaemon();

        long long last = g_KernelLastFbLogMs.load();

        if (now - last >= 10000 &&
            g_KernelLastFbLogMs.compare_exchange_strong(last, now))
        {
            LOGW("[KERNEL] toggle LIGADO mas device INDISPONIVEL — usando pread64/pwrite64 de contingencia (re-probe automatico)");
            FileLog("kernel fallback: device ausente, caminho syscall em uso");
        }

        long long lastRe = g_KernelLastReprobeMs.load();

        if (now - lastRe >= 15000 &&
            g_KernelLastReprobeMs.compare_exchange_strong(lastRe, now))
        {
            RT::Probe();  /* rate-limit interno: 1x/3s no maximo */
        }
    }

    /*
     * PONTE READ: syscall direta __NR_pread64 + cache de blocos.
     */
    bool BridgeReadMem(
        pid_t pid,
        uint64_t address,
        void* buffer,
        size_t size
    )
    {
        /*
         * (V10 KERNEL) toggle LIGADO + device VIVO -> SOMENTE kernel.
         * NAO existe fallback silencioso no modo kernel: falha de ioctl
         * e erro REAL devolvido pro client (mesmo contrato do pread64),
         * com errno logado na tag RTKernel.
         */
        if (g_KernelMode.load(std::memory_order_relaxed))
        {
            if (RT::IsAvailable())
                return KernelReadMem(pid, address, buffer, size);

            LogKernelFallbackOnce();
        }

        return StormRW::ReadMem(pid, address, buffer, size);
    }

    /*
     * PONTE WRITE: syscall direta __NR_pwrite64 (nunca cacheada).
     */
    bool BridgeWriteMem(
        pid_t pid,
        uint64_t address,
        const void* buffer,
        size_t size
    )
    {
        /* (V10 KERNEL) mesmo contrato do READ: kernel SO quando ligado. */
        if (g_KernelMode.load(std::memory_order_relaxed))
        {
            if (RT::IsAvailable())
                return KernelWriteMem(pid, address, buffer, size);

            LogKernelFallbackOnce();
        }

        return StormRW::WriteMem(pid, address, buffer, size);
    }

    /*
     * ============================================================================
     * FALLBACKS ANTIGOS — DESATIVADOS (Task 17)
     * ============================================================================
     * O bloco #if 0 abaixo guarda INTENCIONALMENTE o codigo antigo
     * (process_vm_readv/process_vm_writev + pread64/pwrite64 da libc).
     * Ele NAO compila e NAO roda: nenhum fallback pode ser usado, a ponte
     * e exclusivamente syscall direta via StormSysRW.hpp.
     * ============================================================================
     */
#if 0


    /*
     * process_vm_readv - caminho principal (sem fd, sem rastro de handle).
     */
    bool SyscallRead(
        pid_t pid,
        uint64_t address,
        void* buffer,
        size_t size
    )
    {
#if defined(SYS_process_vm_readv)

        iovec local{};

        local.iov_base = buffer;
        local.iov_len = size;

        iovec remote{};

        remote.iov_base =
            reinterpret_cast<void*>(
                static_cast<uintptr_t>(address)
            );

        remote.iov_len = size;

        ssize_t n =
            process_vm_readv(
                pid,
                &local,
                1,
                &remote,
                1,
                0
            );

        return n ==
            static_cast<ssize_t>(size);

#else

        (void)pid;
        (void)address;
        (void)buffer;
        (void)size;

        return false;

#endif
    }

    /*
     * process_vm_writev - caminho principal.
     */
    bool SyscallWrite(
        pid_t pid,
        uint64_t address,
        const void* buffer,
        size_t size
    )
    {
#if defined(SYS_process_vm_writev)

        iovec local{};

        local.iov_base =
            const_cast<void*>(buffer);

        local.iov_len = size;

        iovec remote{};

        remote.iov_base =
            reinterpret_cast<void*>(
                static_cast<uintptr_t>(address)
            );

        remote.iov_len = size;

        ssize_t n =
            process_vm_writev(
                pid,
                &local,
                1,
                &remote,
                1,
                0
            );

        return n ==
            static_cast<ssize_t>(size);

#else

        (void)pid;
        (void)address;
        (void)buffer;
        (void)size;

        return false;

#endif
    }

    /*
     * pread64 em /proc/<pid>/mem - fallback. ABRE-USA-FECHA na mesma
     * chamada: nenhum handle do mem do jogo fica exposto entre ops.
     */
    bool FileRead(
        pid_t pid,
        uint64_t address,
        void* buffer,
        size_t size
    )
    {
        char path[64];

        snprintf(
            path,
            sizeof(path),
            "/proc/%d/mem",
            pid
        );

        int fd =
            open(
                path,
                O_RDONLY | O_CLOEXEC
            );

        if (fd < 0)
            return false;

        size_t total = 0;
        bool ok = true;

        while (total < size)
        {
            ssize_t n =
                pread64(
                    fd,
                    static_cast<char*>(buffer) + total,
                    size - total,
                    static_cast<off64_t>(address + total)
                );

            if (n <= 0)
            {
                ok = false;

                break;
            }

            total +=
                static_cast<size_t>(n);
        }

        close(fd);

        return ok;
    }

    /*
     * pwrite64 em /proc/<pid>/mem - fallback. ABRE-USA-FECHA idem.
     */
    bool FileWrite(
        pid_t pid,
        uint64_t address,
        const void* buffer,
        size_t size
    )
    {
        char path[64];

        snprintf(
            path,
            sizeof(path),
            "/proc/%d/mem",
            pid
        );

        int fd =
            open(
                path,
                O_WRONLY | O_CLOEXEC
            );

        if (fd < 0)
            return false;

        size_t total = 0;
        bool ok = true;

        while (total < size)
        {
            ssize_t n =
                pwrite64(
                    fd,
                    static_cast<const char*>(buffer) + total,
                    size - total,
                    static_cast<off64_t>(address + total)
                );

            if (n <= 0)
            {
                ok = false;

                break;
            }

            total +=
                static_cast<size_t>(n);
        }

        close(fd);

        return ok;
    }

    bool BridgeReadMem(
        pid_t pid,
        uint64_t address,
        void* buffer,
        size_t size
    )
    {
        if (pid <= 0 || address == 0 || !buffer || size == 0)
            return false;

        if (SyscallRead(pid, address, buffer, size))
            return true;

        return FileRead(pid, address, buffer, size);
    }

    bool BridgeWriteMem(
        pid_t pid,
        uint64_t address,
        const void* buffer,
        size_t size
    )
    {
        if (pid <= 0 || address == 0 || !buffer || size == 0)
            return false;

        if (SyscallWrite(pid, address, buffer, size))
            return true;

        return FileWrite(pid, address, buffer, size);
    }

#endif /* FALLBACKS ANTIGOS DESATIVADOS (Task 17) */

    /*
     * Lê um arquivo de texto inteiro (usado nos helpers de procfs).
     */
    bool ReadTextFile(
        const std::string& path,
        std::string& out
    )
    {
        int fd =
            open(
                path.c_str(),
                O_RDONLY | O_CLOEXEC
            );

        if (fd < 0)
            return false;

        char buffer[8192];

        out.clear();

        for (;;)
        {
            ssize_t n =
                read(
                    fd,
                    buffer,
                    sizeof(buffer)
                );

            if (n < 0)
            {
                if (errno == EINTR)
                    continue;

                break;
            }

            if (n == 0)
            {
                close(fd);

                return true;
            }

            out.append(
                buffer,
                static_cast<size_t>(n)
            );
        }

        close(fd);

        return false;
    }
}

/*
 * ============================================================================
 * HELPERS DE PROCFS (genéricos - sem nada específico do jogo)
 * ============================================================================
 */

namespace
{
    bool IsNumeric(
        const char* text
    )
    {
        if (!text || !*text)
            return false;

        for (
            const char* p = text;
            *p;
            ++p
        )
        {
            if (*p < '0' || *p > '9')
                return false;
        }

        return true;
    }

    std::string ReadCmdline(
        pid_t pid
    )
    {
        std::string out;

        ReadTextFile(
            "/proc/" +
            std::to_string(pid) +
            "/cmdline",
            out
        );

        for (
            char& c : out
        )
        {
            if (c == '\0')
                c = ' ';
        }

        return out;
    }

    bool ContainsIgnoreCase(
        const std::string& haystack,
        const std::string& needle
    )
    {
        if (needle.empty())
            return false;

        auto toLower =
            [](std::string s)
            {
                for (
                    char& c : s
                )
                {
                    if (c >= 'A' && c <= 'Z')
                        c =
                            static_cast<char>(
                                c - 'A' + 'a'
                            );
                }

                return s;
            };

        return
            toLower(haystack)
                .find(toLower(needle)) !=
            std::string::npos;
    }

    /*
     * Procura um processo cujo cmdline contenha algum dos nomes.
     */
    pid_t FindPidByNames(
        const std::vector<std::string>& names
    )
    {
        DIR* proc =
            opendir("/proc");

        if (!proc)
        {
            LOGE(
                "opendir(/proc) falhou: %s",
                strerror(errno)
            );

            return -1;
        }

        pid_t found = -1;

        struct dirent* entry;

        while (
            (entry = readdir(proc)) !=
                nullptr &&
            found < 0
        )
        {
            if (!IsNumeric(entry->d_name))
                continue;

            pid_t pid =
                static_cast<pid_t>(
                    atoi(entry->d_name)
                );

            if (pid <= 0)
                continue;

            std::string cmdline =
                ReadCmdline(pid);

            if (cmdline.empty())
                continue;

            for (
                const std::string& name : names
            )
            {
                if (!name.empty() &&
                    ContainsIgnoreCase(cmdline, name))
                {
                    found = pid;

                    LOGI(
                        "[PROC] FIND_PID: pid=%d cmdline='%s' match='%s'",
                        pid,
                        cmdline.c_str(),
                        name.c_str()
                    );

                    break;
                }
            }
        }

        closedir(proc);

        return found;
    }

    /*
     * Primeiro mapping de um módulo no /proc/<pid>/maps.
     */
    uint64_t FindModuleBase(
        pid_t pid,
        const std::string& moduleName
    )
    {
        std::string maps;

        if (!ReadTextFile(
                "/proc/" +
                std::to_string(pid) +
                "/maps",
                maps
            ))
        {
            LOGE(
                "[PROC] nao abriu maps do pid=%d: %s",
                pid,
                strerror(errno)
            );

            return 0;
        }

        size_t pos = 0;

        while (
            (pos = maps.find(moduleName, pos)) !=
                std::string::npos
        )
        {
            /*
             * Volta para o início da linha deste mapping.
             */
            size_t lineStart =
                maps.rfind('\n', pos);

            lineStart =
                (lineStart == std::string::npos)
                    ? 0
                    : lineStart + 1;

            unsigned long long start = 0;

            if (
                sscanf(
                    maps.c_str() + lineStart,
                    "%llx-%*llx",
                    &start
                ) == 1 &&
                start != 0
            )
            {
                return static_cast<uint64_t>(start);
            }

            pos +=
                moduleName.size();
        }

        return 0;
    }

    /*
     * Classe ELF do executável do processo (1 = 32-bit, 0 = 64-bit).
     */
    bool IsProcess32Bit(
        pid_t pid,
        bool& out32
    )
    {
        char exePath[64];

        snprintf(
            exePath,
            sizeof(exePath),
            "/proc/%d/exe",
            pid
        );

        char target[PATH_MAX]{};

        ssize_t len =
            readlink(
                exePath,
                target,
                sizeof(target) - 1
            );

        if (len <= 0)
        {
            LOGE(
                "readlink(%s) falhou: %s",
                exePath,
                strerror(errno)
            );

            return false;
        }

        target[len] = '\0';

        int fd =
            open(
                target,
                O_RDONLY | O_CLOEXEC
            );

        if (fd < 0)
        {
            LOGE(
                "open(%s) falhou: %s",
                target,
                strerror(errno)
            );

            return false;
        }

        unsigned char ident[5]{};

        ssize_t n =
            pread(
                fd,
                ident,
                sizeof(ident),
                0
            );

        close(fd);

        if (n != sizeof(ident) ||
            ident[0] != 0x7F ||
            ident[1] != 'E' ||
            ident[2] != 'L' ||
            ident[3] != 'F')
        {
            LOGE(
                "[PROC] ELF invalido para pid=%d",
                pid
            );

            return false;
        }

        out32 =
            (ident[4] == 1);

        return true;
    }

    /*
     * ========================================================================
     * (V9) AUTO-RESOLVE DE TYPEINFO — varredura dos slots de Il2CppClass.
     * ========================================================================
     *
     * Encontra TODOS os slots (dentro dos mapeamentos legíveis da lib
     * pedida) cujo valor é um ponteiro para um Il2CppClass cujo campo
     * `name` bate EXATAMENTE com o nome da classe. Isso torna o painel
     * IMUNE a atualização do jogo: GameFacade_TypeInfo, GameVarDef_TypeInfo
     * e AvatarWardrobeDataManager_TypeInfo se re-resolvem sozinhos.
     *
     * 100% via syscall (SysPread = __NR_pread64 direto), em blocos de
     * 256 KB. O klass é validado lendo o ponteiro do nome + a string —
     * falso positivo é praticamente impossível.
     */

    struct TypeInfoHitLocal
    {
        uint64_t rva;
        uint64_t klass;
    };

    struct LibMapping
    {
        uint64_t start;
        uint64_t end;
        bool readable;
        bool exec;
    };

    bool ParseMapsOfLib(
        pid_t pid,
        const std::string& libName,
        std::vector<LibMapping>& out,
        uint64_t& libStart,
        uint64_t& libEnd
    )
    {
        std::string maps;

        if (!ReadTextFile(
                "/proc/" + std::to_string(pid) + "/maps",
                maps
            ))
        {
            return false;
        }

        libStart = UINT64_MAX;
        libEnd   = 0;

        size_t pos = 0;

        while (pos < maps.size())
        {
            size_t eol = maps.find('\n', pos);

            if (eol == std::string::npos)
                eol = maps.size();

            std::string line = maps.substr(pos, eol - pos);
            pos = eol + 1;

            if (line.find(libName) == std::string::npos)
                continue;

            unsigned long long s = 0, e = 0;
            char perms[8] = {};

            if (sscanf(
                    line.c_str(),
                    "%llx-%llx %7s",
                    &s, &e, perms
                ) != 3)
                continue;

            LibMapping m{};
            m.start    = s;
            m.end      = e;
            m.readable = (perms[0] == 'r');
            m.exec     = (perms[2] == 'x');

            out.push_back(m);

            if (s < libStart) libStart = s;
            if (e > libEnd)   libEnd   = e;
        }

        return !out.empty();
    }

    /*
     * Lê `size` bytes do /proc/<pid>/mem via syscall direta.
     * Falha curta (EOF de página morta) é tolerada com leituras de
     * acompanhamento menores — o scanner só precisa dos bytes.
     */
    bool ScanPread(
        int fd,
        uint64_t addr,
        void* buf,
        size_t size
    )
    {
        char* out = static_cast<char*>(buf);
        size_t total = 0;

        while (total < size)
        {
            ssize_t n = StormRW::SysPread(
                fd,
                out + total,
                size - total,
                addr + total
            );

            if (n < 0)
            {
                if (errno == EINTR || errno == EAGAIN)
                    continue;

                return false;
            }

            if (n == 0)
                return false;

            total += static_cast<size_t>(n);
        }

        return true;
    }

    bool ReadTargetString(
        int fd,
        uint64_t strPtr,
        const std::string& want,
        size_t ptrSize
    )
    {
        char buf[64] = {};

        const size_t wantLen = want.size();

        if (wantLen == 0 || wantLen >= sizeof(buf))
            return false;

        if (!ScanPread(
                fd,
                strPtr,
                buf,
                wantLen + 1
            ))
            return false;

        /*
         * (V9.1) MATCH EXATO: buf[wantLen] precisa ser o '\0' REAL lido
         * da memoria. Antes o byte era SOBRESCRITO com 0 e o memcmp
         * virava prefix-match: "GameFacade" casava "GameFacadeManager"
         * etc., e o primeiro hit podia ser o slot da classe ERRADA.
         */
        return buf[wantLen] == '\0' &&
               memcmp(buf, want.c_str(), wantLen) == 0;
    }

    bool FindTypeInfoSlots(
        pid_t pid,
        const std::string& libName,
        const std::string& className,
        std::vector<TypeInfoHitLocal>& out
    )
    {
        std::vector<LibMapping> maps;

        uint64_t libStart = 0, libEnd = 0;

        if (!ParseMapsOfLib(
                pid,
                libName,
                maps,
                libStart,
                libEnd
            ))
        {
            LOGE(
                "[TYPEINFO] lib '%s' nao encontrada no pid=%d",
                libName.c_str(),
                pid
            );

            return false;
        }

        /*
         * (V9.1) BITNESS e resolvido DEPOIS de abrir o fd (abaixo): o
         * EI_CLASS vem do ELF DA LIB lido direto da memoria do alvo.
         * O metodo antigo (readlink /proc/pid/exe) nunca funcionou no
         * Android — exe aponta pro base.apk (ZIP), o pread ve PK\x03\x04
         * em vez de ELF e o fallback assumia 32-bit. Num jogo 64-bit
         * isso forçava ptrSize=4 / nameOff=0x8 (gc_desc!) => 0 hits
         * garantido pra qualquer classe — exatamente o bug do v8a.
         */
        bool target32 = false;

        /*
         * Cache por (pid, lib, classe): o scan custa ~0.5-2 s uma vez.
         */
        struct CacheKey
        {
            pid_t pid;
            std::string lib;
            std::string cls;

            bool operator<(const CacheKey& o) const
            {
                if (pid != o.pid) return pid < o.pid;
                if (lib != o.lib) return lib < o.lib;
                return cls < o.cls;
            }
        };

        static std::map<CacheKey, std::vector<TypeInfoHitLocal>> s_Cache;
        static std::mutex s_CacheMutex;

        CacheKey key{ pid, libName, className };

        {
            std::lock_guard<std::mutex> lk(s_CacheMutex);

            auto it = s_Cache.find(key);

            if (it != s_Cache.end())
            {
                out = it->second;

                return true;
            }
        }

        const int fd = StormRW::SysOpenMem(pid, false);

        if (fd < 0)
        {
            LOGE(
                "[TYPEINFO] open /proc/%d/mem falhou: %s",
                pid,
                strerror(errno)
            );

            return false;
        }

        /*
         * (V9.1) BITNESS REAL: le o ELF header DA LIB direto da memoria
         * do alvo em libStart (mesmo canal das leituras normais, 100%
         * syscall). ident[4] == 1 -> 32-bit, == 2 -> 64-bit. Fallback:
         * IsProcess32Bit (exe), e por ultimo assume 32-bit como antes.
         */
        bool haveBitness = false;

        if (libStart != 0)
        {
            unsigned char elfId[6]{};

            if (ScanPread(
                    fd,
                    libStart,
                    elfId,
                    sizeof(elfId)
                ) &&
                elfId[0] == 0x7F &&
                elfId[1] == 'E' &&
                elfId[2] == 'L' &&
                elfId[3] == 'F')
            {
                target32    = (elfId[4] == 1);
                haveBitness = true;

                LOGI(
                    "[TYPEINFO] bitness via ELF da lib: %d-bit (pid=%d)",
                    target32 ? 32 : 64,
                    pid
                );
            }
        }

        if (!haveBitness && !IsProcess32Bit(pid, target32))
        {
            target32 = true;   /* legacy fallback (FF TH 32-bit) */
        }

        const size_t ptrSize = target32 ? 4 : 8;
        const size_t nameOff = target32 ? 0x8 : 0x10;

        /*
         * Varre cada mapeamento legível da lib em blocos de 256 KB.
         */
        static const size_t kChunk = 256 * 1024;

        std::vector<uint8_t> chunk(kChunk);
        std::vector<TypeInfoHitLocal> hits;
        size_t candidateCount = 0;

        for (const LibMapping& m : maps)
        {
            if (!m.readable)
                continue;

            /*
             * (V9.1) Pula seções executaveis (.text): slot de TypeInfo
             * e ponteiro de DADOS — vive em .data.rel.ro (RELRO, r--),
             * .data/.bss (rw-), nunca no codigo. A lib v8a do FF tem
             * centenas de MB de .text; pular corta o scan em ~70%.
             */
            if (m.exec)
                continue;

            /*
             * Alinha o início ao passo do ponteiro dentro do bloco.
             */
            for (uint64_t a = m.start; a < m.end; )
            {
                const uint64_t chunkStart = a;
                const size_t want = static_cast<size_t>(
                    (m.end - a) < kChunk ? (m.end - a) : kChunk
                );

                if (!ScanPread(
                        fd,
                        chunkStart,
                        chunk.data(),
                        want
                    ))
                {
                    a = chunkStart + kChunk;
                    continue;
                }

                for (size_t off = 0;
                     off + ptrSize <= want;
                     off += ptrSize)
                {
                    uint64_t v = 0;

                    if (ptrSize == 4)
                        v = *reinterpret_cast<uint32_t*>(
                            chunk.data() + off);
                    else
                        v = *reinterpret_cast<uint64_t*>(
                            chunk.data() + off);

                    if (v == 0)
                        continue;

                    if ((v & (ptrSize - 1)) != 0)
                        continue;

                    /*
                     * O klass vive FORA da lib (heap/anon).
                     */
                    if (v >= libStart && v < libEnd)
                        continue;

                    if (target32)
                    {
                        if (v < 0x10000 || v > 0xE0000000ULL)
                            continue;
                    }
                    else
                    {
                        /*
                         * Heap userspace Android arm64 — descarta lixo
                         * de código (valores < 4 GB).
                         */
                        if (v < 0x100000000ULL)
                            continue;
                    }

                    ++candidateCount;

                    /*
                     * Il2CppClass::name é o 3º ponteiro:
                     * 32-bit: image 0x0, gc_desc 0x4, name 0x8
                     * 64-bit: image 0x0, gc_desc 0x8, name 0x10
                     */
                    uint64_t namePtr = 0;

                    if (!ScanPread(
                            fd,
                            v + nameOff,
                            &namePtr,
                            ptrSize
                        ))
                        continue;

                    if (namePtr == 0 ||
                        (namePtr >= libStart && namePtr < libEnd))
                        continue;

                    if (!ReadTargetString(
                            fd,
                            namePtr,
                            className,
                            ptrSize
                        ))
                        continue;

                    TypeInfoHitLocal h{};
                    h.rva   = (chunkStart + off) - libStart;
                    h.klass = v;

                    hits.push_back(h);

                    LOGI(
                        "[TYPEINFO] '%s' slot @ rva=0x%llX klass=0x%llX",
                        className.c_str(),
                        (unsigned long long)h.rva,
                        (unsigned long long)h.klass
                    );

                    if (hits.size() >= 16)
                        break;
                }

                if (hits.size() >= 16)
                    break;

                a = chunkStart + want;
            }

            if (hits.size() >= 16)
                break;
        }

        StormRW::SysClose(fd);

        LOGI(
            "[TYPEINFO] scan '%s' em '%s': %zu candidatos, %zu hits",
            className.c_str(),
            libName.c_str(),
            candidateCount,
            hits.size()
        );

        FileLog(
            "TYPEINFO %s em %s: candidatos=%zu hits=%zu",
            className.c_str(),
            libName.c_str(),
            candidateCount,
            hits.size()
        );

        /*
         * (V9.1) NAO cacheia resultado VAZIO: no arranque do jogo o slot
         * ainda pode estar com token encoded / classe nao criada. Se
         * cacheasse 0 hits, o retry de 30 s do cliente receberia a
         * resposta vazia guardada PARA SEMPRE (por pid). Vazio = nova
         * varredura no proximo pedido.
         */
        if (!hits.empty())
        {
            std::lock_guard<std::mutex> lk(s_CacheMutex);

            s_Cache[key] = hits;
        }

        out = hits;

        return true;   /* true = scan executou (hits pode ser 0) */
    }
}

/*
 * ============================================================================
 * PROCESSAMENTO DOS PEDIDOS
 * ============================================================================
 */

namespace
{
    /*
     * Log detalhado das primeiras operações de cada tipo, depois
     * só erros + estatística periódica (o volume de reads é alto).
     *
     * FIX "LOG SÓ MOSTRA ERRO MESMO COM WRITE FUNCIONANDO": antes os
     * sucessos eram logados apenas nas PRIMEIRAS amostras (16 writes /
     * 8 reads) e os erros eram logados SEMPRE — depois das primeiras
     * amostras o log só mostrava "ERRO", mesmo com ~100% de sucesso.
     * Agora: sucessos continuam sendo amostrados periodicamente
     * (1 log a cada N ok) e erros passam a ter contador acumulado
     * (1 log a cada M falhas, mostrando o total) — o log fica fiel
     * nos dois sentidos sem virar spam.
     */
    static std::atomic<uint64_t> g_LogSampleReads{ 0 };
    static std::atomic<uint64_t> g_LogSampleWrites{ 0 };
    static std::atomic<uint64_t> g_ReadErrTotal{ 0 };
    static std::atomic<uint64_t> g_WriteErrTotal{ 0 };

    void HandleRequest(
        int clientFd,
        const BridgeRequest& req,
        const std::vector<uint8_t>& payloadIn,
        BridgeResponse& resp,
        std::vector<uint8_t>& payloadOut
    )
    {
        memset(
            &resp,
            0,
            sizeof(resp)
        );

        resp.Magic =
            BRIDGE_MAGIC;

        resp.Version =
            BRIDGE_PROTO_VERSION;

        resp.Cmd =
            req.Cmd;

        resp.Seq =
            req.Seq;

        switch (req.Cmd)
        {
            case BRIDGE_CMD_PING:
            {
                resp.Status =
                    BRIDGE_OK;

                resp.Value =
                    BRIDGE_PROTO_VERSION;

                break;
            }

            case BRIDGE_CMD_READ:
            {
                if (req.Size == 0 ||
                    req.Size > BRIDGE_MAX_PAYLOAD)
                {
                    resp.Status =
                        BRIDGE_ERR_TOOBIG;

                    g_TotalErrors++;

                    break;
                }

                payloadOut.resize(req.Size);

                if (BridgeReadMem(
                        static_cast<pid_t>(req.Pid),
                        req.Address,
                        payloadOut.data(),
                        req.Size
                    ))
                {
                    resp.Status =
                        BRIDGE_OK;

                    resp.PayloadSize =
                        req.Size;

                    g_TotalReads++;

                    /*
                     * Sucesso: 8 primeiros detalhados + 1 amostra a cada
                     * 2000 (o volume de reads é alto; a amostra prova que
                     * a leitura segue saudável no log).
                     */
                    const uint64_t readSample =
                        g_LogSampleReads.fetch_add(1);

                    if (readSample < 8 ||
                        (readSample % 2000) == 0)
                    {
                        LOGI(
                            "[READ] pid=%u addr=0x%llX size=%u OK (total=%llu)",
                            req.Pid,
                            (unsigned long long)req.Address,
                            req.Size,
                            (unsigned long long)(readSample + 1)
                        );

                        FileLog(
                            "READ pid=%u addr=0x%llX size=%u ok (total=%llu)",
                            req.Pid,
                            (unsigned long long)req.Address,
                            req.Size,
                            (unsigned long long)(readSample + 1)
                        );
                    }
                }
                else
                {
                    resp.Status =
                        (errno == EPERM)
                            ? BRIDGE_ERR_PERM
                            : BRIDGE_ERR_GENERIC;

                    payloadOut.clear();

                    g_TotalErrors++;

                    /*
                     * Erro: 16 primeiros detalhados + 1 a cada 200 com
                     * total acumulado (antes: LOGE SEMPRE — spam que
                     * escondia os sucessos no log).
                     */
                    const uint64_t readErr =
                        g_ReadErrTotal.fetch_add(1) + 1;

                    if (readErr <= 16 || (readErr % 200) == 0)
                    {
                        /*
                         * (RWFIX-V7) errno REAL da ultima syscall de RW
                         * (antes: strerror(errno) depois de mutex/memcpy
                         * logava errno trocado). errno=0 = falha servida
                         * pelo cache negativo (sem syscall nova).
                         */
                        const int rwErr =
                            (int)StormRW::GetLastErrno();

                        LOGE(
                            "[READ] pid=%u addr=0x%llX size=%u FALHOU (%s errno=%d) erros=%llu",
                            req.Pid,
                            (unsigned long long)req.Address,
                            req.Size,
                            strerror(rwErr),
                            rwErr,
                            (unsigned long long)readErr
                        );

                        FileLog(
                            "READ pid=%u addr=0x%llX size=%u ERRO (errno=%d %s) erros=%llu",
                            req.Pid,
                            (unsigned long long)req.Address,
                            req.Size,
                            rwErr,
                            strerror(rwErr),
                            (unsigned long long)readErr
                        );
                    }
                }

                break;
            }

            case BRIDGE_CMD_WRITE:
            {
                if (req.Size == 0 ||
                    req.Size > BRIDGE_MAX_PAYLOAD ||
                    payloadIn.size() != req.Size)
                {
                    resp.Status =
                        BRIDGE_ERR_INVALID;

                    g_TotalErrors++;

                    break;
                }

                if (BridgeWriteMem(
                        static_cast<pid_t>(req.Pid),
                        req.Address,
                        payloadIn.data(),
                        req.Size
                    ))
                {
                    resp.Status =
                        BRIDGE_OK;

                    g_TotalWrites++;

                    /*
                     * Sucesso: 16 primeiros detalhados + 1 amostra a cada
                     * 200 (antes parava de logar "ok" para sempre após a
                     * 16ª — o log parecia 100% erro mesmo escrevendo).
                     */
                    const uint64_t writeSample =
                        g_LogSampleWrites.fetch_add(1);

                    if (writeSample < 16 ||
                        (writeSample % 200) == 0)
                    {
                        LOGI(
                            "[WRITE] pid=%u addr=0x%llX size=%u OK (total=%llu)",
                            req.Pid,
                            (unsigned long long)req.Address,
                            req.Size,
                            (unsigned long long)(writeSample + 1)
                        );

                        FileLog(
                            "WRITE pid=%u addr=0x%llX size=%u ok (total=%llu)",
                            req.Pid,
                            (unsigned long long)req.Address,
                            req.Size,
                            (unsigned long long)(writeSample + 1)
                        );
                    }
                }
                else
                {
                    resp.Status =
                        (errno == EPERM)
                            ? BRIDGE_ERR_PERM
                            : BRIDGE_ERR_GENERIC;

                    g_TotalErrors++;

                    /*
                     * Erro: 16 primeiros detalhados + 1 a cada 100 com
                     * total acumulado (antes: LOGE SEMPRE).
                     */
                    const uint64_t writeErr =
                        g_WriteErrTotal.fetch_add(1) + 1;

                    if (writeErr <= 16 || (writeErr % 100) == 0)
                    {
                        /* (RWFIX-V7) errno REAL da ultima syscall de RW */
                        const int rwErr =
                            (int)StormRW::GetLastErrno();

                        LOGE(
                            "[WRITE] pid=%u addr=0x%llX size=%u FALHOU (%s errno=%d) erros=%llu",
                            req.Pid,
                            (unsigned long long)req.Address,
                            req.Size,
                            strerror(rwErr),
                            rwErr,
                            (unsigned long long)writeErr
                        );

                        FileLog(
                            "WRITE pid=%u addr=0x%llX size=%u ERRO (errno=%d %s) erros=%llu",
                            req.Pid,
                            (unsigned long long)req.Address,
                            req.Size,
                            rwErr,
                            strerror(rwErr),
                            (unsigned long long)writeErr
                        );
                    }
                }

                break;
            }

            case BRIDGE_CMD_READ_BATCH:
            {
                /*
                 * Payload in : N x 12 bytes { uint64 addr; uint32 size; }
                 * Payload out: dados concatenados (item falho = zeros).
                 * Uma unica viagem de socket para N leituras — e isso que
                 * faz o ESP atualizar rapido no Android.
                 */
                if (req.PayloadSize == 0 ||
                    req.PayloadSize > BRIDGE_MAX_PAYLOAD ||
                    (req.PayloadSize % 12) != 0)
                {
                    resp.Status = BRIDGE_ERR_INVALID;
                    g_TotalErrors++;
                    break;
                }

                const size_t itemCount = req.PayloadSize / 12;
                const uint8_t* in = payloadIn.data();

                /* 1a passada: soma os tamanhos (limita a 1 MiB). */
                size_t totalBytes = 0;

                for (size_t i = 0; i < itemCount; i++)
                {
                    uint32_t sz;
                    memcpy(&sz, in + i * 12 + 8, sizeof(sz));
                    totalBytes += sz;
                }

                if (totalBytes > BRIDGE_MAX_PAYLOAD)
                {
                    resp.Status = BRIDGE_ERR_TOOBIG;
                    g_TotalErrors++;
                    break;
                }

                payloadOut.assign(totalBytes, 0);

                /*
                 * (V10 KERNEL) toggle kernel LIGADO: o lote roda item a item
                 * pelo driver (ioctl OP_CMD_READ por item — o driver nao tem
                 * op em lote). Item falho vem ZERADO, igual ao caminho
                 * pread64 — mesmo contrato pro client. Device morto ->
                 * contingencia pread64 com log (LogKernelFallbackOnce).
                 */
                if (g_KernelMode.load(std::memory_order_relaxed))
                {
                    if (RT::IsAvailable())
                    {
                        size_t kFailed = 0;
                        size_t kOffset = 0;

                        for (size_t i = 0; i < itemCount; i++)
                        {
                            uint64_t kAddr;
                            uint32_t kSz;

                            memcpy(&kAddr, in + i * 12, sizeof(kAddr));
                            memcpy(&kSz, in + i * 12 + 8, sizeof(kSz));

                            if (kSz == 0)
                                continue;

                            if (!KernelReadMem(
                                    static_cast<pid_t>(req.Pid),
                                    kAddr,
                                    payloadOut.data() + kOffset,
                                    kSz))
                            {
                                memset(payloadOut.data() + kOffset, 0, kSz);
                                kFailed++;
                            }

                            kOffset += kSz;
                        }

                        resp.PayloadSize =
                            static_cast<uint32_t>(totalBytes);

                        resp.Status =
                            (kFailed == 0) ? BRIDGE_OK : BRIDGE_ERR_PARTIAL;

                        g_TotalReads += (uint64_t)itemCount;

                        if (kFailed != 0)
                        {
                            static std::atomic<uint64_t> s_KBatchErrTotal{ 0 };

                            const uint64_t kBatchErr =
                                s_KBatchErrTotal.fetch_add(1) + 1;

                            if (kBatchErr <= 8 || (kBatchErr % 100) == 0)
                            {
                                LOGE(
                                    "[KERNEL][READBATCH] pid=%u itens=%zu falhas=%zu total=%zu (ocorrencias=%llu)",
                                    req.Pid, itemCount, kFailed, totalBytes,
                                    (unsigned long long)kBatchErr
                                );
                            }
                        }

                        break;
                    }

                    LogKernelFallbackOnce();
                }

                /*
                 * (V8.6-PERF) UM openat/close pra o lote inteiro. Antes:
                 * BridgeReadMem por item = open+pread+close POR ITEM que
                 * der miss no cache de 256 B — com ~100 itens por onda e
                 * ~25 ondas por tick do ESP, eram dezenas de milhares de
                 * openat/s no aparelho (o lag/travamento do ESP). Os preads
                 * continuam 1 por bloco, com o mesmo cache/fallback/plan B.
                 */
                std::vector<StormRW::BatchItemRW> rwItems(itemCount);

                size_t offset = 0;

                for (size_t i = 0; i < itemCount; i++)
                {
                    uint64_t addr;
                    uint32_t sz;

                    memcpy(&addr, in + i * 12, sizeof(addr));
                    memcpy(&sz, in + i * 12 + 8, sizeof(sz));

                    rwItems[i].address = addr;
                    rwItems[i].size    = sz;
                    rwItems[i].out     = payloadOut.data() + offset;

                    offset += sz;
                }

                size_t failed = 0;

                StormRW::ReadBatchMem(
                    static_cast<pid_t>(req.Pid),
                    rwItems.data(),
                    itemCount,
                    &failed);

                resp.PayloadSize = static_cast<uint32_t>(totalBytes);
                resp.Status = (failed == 0) ? BRIDGE_OK : BRIDGE_ERR_PARTIAL;

                g_TotalReads += (uint64_t)itemCount;

                if (failed != 0)
                {
                    static std::atomic<uint64_t> s_BatchErrTotal{ 0 };

                    const uint64_t batchErr =
                        s_BatchErrTotal.fetch_add(1) + 1;

                    if (batchErr <= 8 || (batchErr % 100) == 0)
                    {
                        LOGE(
                            "[READBATCH] pid=%u itens=%zu falhas=%zu total=%zu (ocorrencias=%llu)",
                            req.Pid, itemCount, failed, totalBytes,
                            (unsigned long long)batchErr
                        );
                    }
                }

                break;
            }

            case BRIDGE_CMD_STATS:
            {
                /*
                 * (V8.7) PROVA DE BYPASS: devolve os contadores internos
                 * do daemon (StormRW::GetStats + totals da ponte) numa
                 * struct fixa (BridgeStatsPayload). O painel mostra os
                 * numeros ao usuario: directReads (pread64) vs vmFbReads
                 * (fallback process_vm — 0 = bypass 100% pread64).
                 */
                static_assert(
                    sizeof(BridgeStatsPayloadV2) == 192,
                    "payload de stats mudou — atualize o cliente junto"
                );

                const StormRW::Stats rs = StormRW::GetStats();

                BridgeStatsPayloadV2 sp{};

                sp.base.bridgeReads       = g_TotalReads.load();
                sp.base.bridgeWrites      = g_TotalWrites.load();
                sp.base.bridgeErrors      = g_TotalErrors.load();
                sp.base.uptimeSec         = (uint64_t)((NowMsDaemon() - g_StartMs) / 1000LL);

                sp.base.cacheHits         = rs.hits;
                sp.base.cacheNegHits      = rs.negHits;
                sp.base.cacheMisses       = rs.misses;
                sp.base.syscalls          = rs.syscalls;
                sp.base.retries           = rs.retries;

                sp.base.directReads       = rs.directReads;
                sp.base.exactFb           = rs.exactFb;
                sp.base.vmFbReads         = rs.vmFbReads;

                sp.base.directWrites      = rs.directWrites;
                sp.base.vmFbWrites        = rs.vmFbWrites;

                sp.base.negCreated        = rs.negCreated;
                sp.base.openFails         = rs.openFails;
                sp.base.wrRefused         = rs.wrRefused;
                sp.base.wrKilled          = rs.wrKilled;

                sp.base.writesOn          = rs.writesOn;
                sp.base.vmFallbackOn      = (uint32_t)STORM_VM_FALLBACK;
                sp.base.daemonProtoVersion = BRIDGE_PROTO_VERSION;
                sp.base.reserved0         = 0;

                /* (V10 KERNEL) contadores do modo kernel (fim do struct) */
                sp.kernel.kReads     = g_KernelReads.load();
                sp.kernel.kWrites    = g_KernelWrites.load();
                sp.kernel.kErrs      = g_KernelErrors.load();
                sp.kernel.kActive    = (uint32_t)g_KernelMode.load();
                sp.kernel.kAvail     = RT::IsAvailable() ? 1u : 0u;

                payloadOut.assign(
                    sizeof(sp),
                    0
                );

                memcpy(
                    payloadOut.data(),
                    &sp,
                    sizeof(sp)
                );

                resp.PayloadSize = static_cast<uint32_t>(payloadOut.size());
                resp.Status = BRIDGE_OK;

                break;
            }

            case BRIDGE_CMD_FIND_TYPEINFO:
            {
                /*
                 * (V9) Payload do pedido: "libName|ClassName".
                 * Resposta: uint32 count + count x TypeInfoHitPayload.
                 */
                std::string joined(
                    payloadIn.begin(),
                    payloadIn.end()
                );

                const size_t sep = joined.find('|');

                if (sep == std::string::npos ||
                    req.Pid <= 0 ||
                    sep == 0 ||
                    sep + 1 >= joined.size())
                {
                    resp.Status = BRIDGE_ERR_INVALID;
                    break;
                }

                const std::string libName =
                    joined.substr(0, sep);

                const std::string className =
                    joined.substr(sep + 1);

                std::vector<TypeInfoHitLocal> hits;

                if (!FindTypeInfoSlots(
                        static_cast<pid_t>(req.Pid),
                        libName,
                        className,
                        hits
                    ))
                {
                    resp.Status = BRIDGE_ERR_GENERIC;
                    break;
                }

                const uint32_t count =
                    static_cast<uint32_t>(hits.size());

                payloadOut.assign(
                    sizeof(uint32_t) +
                        count * sizeof(TypeInfoHitPayload),
                    0
                );

                memcpy(
                    payloadOut.data(),
                    &count,
                    sizeof(uint32_t)
                );

                for (uint32_t i = 0; i < count; ++i)
                {
                    TypeInfoHitPayload hp{};

                    hp.rva   = hits[i].rva;
                    hp.klass = hits[i].klass;

                    memcpy(
                        payloadOut.data() +
                            sizeof(uint32_t) +
                            i * sizeof(TypeInfoHitPayload),
                        &hp,
                        sizeof(hp)
                    );
                }

                resp.PayloadSize =
                    static_cast<uint32_t>(payloadOut.size());

                resp.Value = count;
                resp.Status = BRIDGE_OK;

                break;
            }

            case BRIDGE_CMD_KERNEL_SET:
            {
                /*
                 * (V10 KERNEL) Payload do pedido: uint32 enable.
                 *   1 = ativa modo kernel SO (device precisa existir)
                 *   0 = desativa (volta pro pread64/pwrite64 direto)
                 * Resposta: Value=1 modo ativo; Status=NOTFOUND se o
                 * driver nao estiver carregado (o toggle no painel
                 * mostra o motivo via KERNEL_STATUS).
                 */
                uint32_t enable = 0;

                if (payloadIn.size() >= sizeof(uint32_t))
                    memcpy(&enable, payloadIn.data(), sizeof(uint32_t));

                if (enable)
                {
                    /*
                     * Re-probe antes de decidir: o usuario pode ter
                     * carregado o modulo AGORA (Probe tem rate-limit
                     * interno de 1x/3s, entao e barato).
                     */
                    if (!RT::IsAvailable())
                        RT::Probe();

                    if (RT::IsAvailable())
                    {
                        g_KernelMode = 1;

                        /* kernel ON = logs RTKernel ON (opt-in do user) */
                        RT::SetLogVerbose(true);

                        resp.Value = 1;
                        resp.Status = BRIDGE_OK;

                        LOGI(
                            "[KERNEL] ATIVADO via client — device=%s | READ/WRITE agora SOMENTE via driver (leitura %s, escrita %s)",
                            RT::DevicePath(),
                            "OK",
                            RT::WriteSelfTestOk() ? "OK" : "FALHOU"
                        );

                        FileLog(
                            "KERNEL ativado device=%s writeOk=%d",
                            RT::DevicePath(),
                            RT::WriteSelfTestOk() ? 1 : 0
                        );
                    }
                    else
                    {
                        g_KernelMode = 0;

                        resp.Value = 0;
                        resp.Status = BRIDGE_ERR_NOTFOUND;

                        LOGW(
                            "[KERNEL] client pediu ATIVAR mas nenhum driver foi encontrado — modulo .ko nao carregado?"
                        );

                        FileLog("KERNEL recusado: driver ausente");
                    }
                }
                else
                {
                    g_KernelMode = 0;

                    /* logs de kernel voltam a seguir o --verbose */
                    RT::SetLogVerbose(g_Verbose.load() ? true : false);

                    resp.Value = 0;
                    resp.Status = BRIDGE_OK;

                    LOGI(
                        "[KERNEL] DESATIVADO via client — de volta a pread64/pwrite64 direto"
                    );

                    FileLog("KERNEL desativado");
                }

                break;
            }

            case BRIDGE_CMD_KERNEL_STATUS:
            {
                /*
                 * (V10 KERNEL) Estado do driver pro painel. Se ainda nao
                 * ha driver, o proprio status dispara um re-probe (com
                 * rate-limit interno) — abrir a aba Settings ja basta
                 * pra detectar modulo carregado agora.
                 */
                if (!RT::IsAvailable())
                    RT::Probe();

                KernelStatusPayload ks{};

                ks.supported = 1;  /* daemon compilado com RTmodules.h */
                ks.available = RT::IsAvailable() ? 1u : 0u;
                ks.active    = g_KernelMode.load() ? 1u : 0u;
                ks.writeOk   = RT::WriteSelfTestOk() ? 1u : 0u;

                ks.reads  = g_KernelReads.load();
                ks.writes = g_KernelWrites.load();
                ks.errs   = g_KernelErrors.load();

                strncpy(ks.devPath, RT::DevicePath(), sizeof(ks.devPath) - 1);
                ks.devPath[sizeof(ks.devPath) - 1] = '\0';

                payloadOut.assign(sizeof(ks), 0);

                memcpy(payloadOut.data(), &ks, sizeof(ks));

                resp.PayloadSize =
                    static_cast<uint32_t>(sizeof(ks));

                resp.Value = ks.active;
                resp.Status = BRIDGE_OK;

                break;
            }

            case BRIDGE_CMD_FIND_PID:
            {
                std::vector<std::string> names;

                std::string joined(
                    payloadIn.begin(),
                    payloadIn.end()
                );

                size_t start = 0;

                while (start <= joined.size())
                {
                    size_t sep =
                        joined.find('|', start);

                    if (sep == std::string::npos)
                    {
                        if (start < joined.size())
                            names.push_back(
                                joined.substr(start)
                            );

                        break;
                    }

                    names.push_back(
                        joined.substr(start, sep - start)
                    );

                    start =
                        sep + 1;
                }

                pid_t pid =
                    FindPidByNames(names);

                resp.Value =
                    static_cast<uint64_t>(
                        pid > 0 ? pid : 0
                    );

                resp.Status =
                    (pid > 0)
                        ? BRIDGE_OK
                        : BRIDGE_ERR_NOTFOUND;

                break;
            }

            case BRIDGE_CMD_MODULE_BASE:
            {
                std::string moduleName(
                    payloadIn.begin(),
                    payloadIn.end()
                );

                if (moduleName.empty() ||
                    req.Pid <= 0)
                {
                    resp.Status =
                        BRIDGE_ERR_INVALID;

                    break;
                }

                uint64_t base = 0;

                /*
                 * (V10 KERNEL) modo kernel: base vem do DRIVER (ioctl
                 * OP_CMD_BASE — o kernel le a lista de modulos do alvo
                 * sem /proc/pid/maps). Sem driver ou modulo nao achado
                 * pelo driver, cai no parser de maps de sempre.
                 */
                if (g_KernelMode.load(std::memory_order_relaxed) &&
                    RT::IsAvailable())
                {
                    base = RT::KernelModuleBase(
                        static_cast<pid_t>(req.Pid),
                        moduleName.c_str(),
                        0
                    );
                }

                if (base == 0)
                    base = FindModuleBase(
                        static_cast<pid_t>(req.Pid),
                        moduleName
                    );

                resp.Value = base;

                resp.Status =
                    (base != 0)
                        ? BRIDGE_OK
                        : BRIDGE_ERR_NOTFOUND;

                if (base != 0)
                {
                    LOGI(
                        "[PROC] MODULE_BASE pid=%u '%s' = 0x%llX",
                        req.Pid,
                        moduleName.c_str(),
                        (unsigned long long)base
                    );

                    FileLog(
                        "MODULE_BASE pid=%u %s = 0x%llX",
                        req.Pid,
                        moduleName.c_str(),
                        (unsigned long long)base
                    );
                }

                break;
            }

            case BRIDGE_CMD_IS_32BIT:
            {
                bool is32 = false;

                if (IsProcess32Bit(
                        static_cast<pid_t>(req.Pid),
                        is32
                    ))
                {
                    resp.Status =
                        BRIDGE_OK;

                    resp.Value =
                        is32 ? 1 : 0;

                    LOGI(
                        "[PROC] IS_32BIT pid=%u -> %s",
                        req.Pid,
                        is32 ? "32-bit" : "64-bit"
                    );
                }
                else
                {
                    resp.Status =
                        BRIDGE_ERR_NOTFOUND;
                }

                break;
            }

            case BRIDGE_CMD_SHUTDOWN:
            {
                LOGI(
                    "[CTRL] SHUTDOWN solicitado pelo client"
                );

                FileLog("shutdown via socket");

                resp.Status =
                    BRIDGE_OK;

                g_Stop =
                    true;

                break;
            }

            default:
            {
                resp.Status =
                    BRIDGE_ERR_INVALID;

                g_TotalErrors++;

                LOGW(
                    "[CTRL] comando desconhecido: %u",
                    req.Cmd
                );

                break;
            }
        }
    }

    bool RecvAll(
        int fd,
        void* buffer,
        size_t size
    )
    {
        char* p =
            static_cast<char*>(buffer);

        size_t total = 0;

        while (total < size)
        {
            ssize_t n =
                recv(
                    fd,
                    p + total,
                    size - total,
                    MSG_CMSG_CLOEXEC
                );

            if (n < 0)
            {
                if (errno == EINTR)
                    continue;

                return false;
            }

            if (n == 0)
                return false;

            total +=
                static_cast<size_t>(n);
        }

        return true;
    }

    bool SendAll(
        int fd,
        const void* buffer,
        size_t size
    )
    {
        const char* p =
            static_cast<const char*>(buffer);

        size_t total = 0;

        while (total < size)
        {
            ssize_t n =
                send(
                    fd,
                    p + total,
                    size - total,
                    MSG_NOSIGNAL
                );

            if (n < 0)
            {
                if (errno == EINTR)
                    continue;

                return false;
            }

            total +=
                static_cast<size_t>(n);
        }

        return true;
    }

    void ClientThread(
        int clientFd
    )
    {
        g_TotalConnections++;

        LOGI(
            "[CONN] cliente conectado (total conexoes: %llu)",
            (unsigned long long)g_TotalConnections.load()
        );

        FileLog("cliente conectado");

        for (;;)
        {
            BridgeRequest req{};

            if (!RecvAll(
                    clientFd,
                    &req,
                    sizeof(req)
                ))
            {
                break;
            }

            if (req.Magic != BRIDGE_MAGIC ||
                req.Version != BRIDGE_PROTO_VERSION)
            {
                LOGE(
                    "[CONN] magic/version invalido (0x%X v%u) - fechando",
                    req.Magic,
                    req.Version
                );

                FileLog("magic/version invalido - fecho cliente");

                break;
            }

            std::vector<uint8_t> payloadIn;

            if (req.PayloadSize > 0)
            {
                if (req.PayloadSize > BRIDGE_MAX_PAYLOAD)
                {
                    LOGE(
                        "[CONN] payload %u acima do limite",
                        req.PayloadSize
                    );

                    break;
                }

                payloadIn.resize(req.PayloadSize);

                if (!RecvAll(
                        clientFd,
                        payloadIn.data(),
                        payloadIn.size()
                    ))
                {
                    break;
                }
            }

            BridgeResponse resp{};
            std::vector<uint8_t> payloadOut;

            HandleRequest(
                clientFd,
                req,
                payloadIn,
                resp,
                payloadOut
            );

            if (!SendAll(
                    clientFd,
                    &resp,
                    sizeof(resp)
                ))
            {
                break;
            }

            if (resp.PayloadSize > 0 &&
                !payloadOut.empty())
            {
                if (!SendAll(
                        clientFd,
                        payloadOut.data(),
                        resp.PayloadSize
                    ))
                {
                    break;
                }
            }

            if (g_Stop.load())
                break;
        }

        close(clientFd);

        LOGI(
            "[CONN] cliente desconectado (reads=%llu writes=%llu erros=%llu)",
            (unsigned long long)g_TotalReads.load(),
            (unsigned long long)g_TotalWrites.load(),
            (unsigned long long)g_TotalErrors.load()
        );

        FileLog(
            "cliente desconectado (reads=%llu writes=%llu erros=%llu)",
            (unsigned long long)g_TotalReads.load(),
            (unsigned long long)g_TotalWrites.load(),
            (unsigned long long)g_TotalErrors.load()
        );
    }
}

/*
 * ============================================================================
 * MAIN
 * ============================================================================
 */

static void OnSignal(
    int sig
)
{
    (void)sig;

    g_Stop =
        true;
}

static void WritePidFile()
{
    FILE* fp =
        fopen(
            g_PidFilePath.c_str(),
            "w"
        );

    if (!fp)
    {
        LOGW(
            "nao consegui escrever pidfile %s: %s",
            g_PidFilePath.c_str(),
            strerror(errno)
        );

        return;
    }

    fprintf(
        fp,
        "%d\n",
        getpid()
    );

    fclose(fp);
}

int main(
    int argc,
    char** argv
)
{
    for (int i = 1; i < argc; i++)
    {
        if (strcmp(argv[i], "--socket") == 0 &&
            i + 1 < argc)
        {
            g_SocketPath =
                argv[++i];
        }
        else if (strcmp(argv[i], "--pidfile") == 0 &&
                 i + 1 < argc)
        {
            g_PidFilePath =
                argv[++i];
        }
        else if (strcmp(argv[i], "--log") == 0 &&
                 i + 1 < argc)
        {
            g_LogFilePath =
                argv[++i];

            /* So grava log se --log for passado (debug). */
            g_LogFileEnabled =
                1;
        }
        else if (strcmp(argv[i], "--verbose") == 0)
        {
            g_Verbose =
                1;
        }
    }

    /*
     * ============================================================================
     * STEALTH (Task 12) — MASQUERADE DO PROCESSO
     * ============================================================================
     * comm (o que `ps`/`pgrep` mostram) e cmdline(/proc/<pid>/cmdline) viram
     * um nome neutro de sistema. Nenhum "storm", nenhum caminho de
     * /data/local/tmp fica visivel no processo. Feito DEPOIS do parse dos
     * argumentos (o socket path randomizado fica fora da cmdline).
     * ============================================================================
     */
    prctl(
        PR_SET_NAME,
        "appstats",
        0,
        0,
        0
    );

    if (argc > 0 && argv[0] != nullptr)
    {
        const size_t arg0Len =
            strlen(argv[0]);

        memset(
            argv[0],
            0,
            arg0Len
        );

        snprintf(
            argv[0],
            arg0Len,
            "/system/bin/appstats"
        );
    }

    for (int i = 1; i < argc; i++)
    {
        if (argv[i] != nullptr)
        {
            memset(
                argv[i],
                0,
                strlen(argv[i])
            );
        }
    }

    signal(SIGPIPE, SIG_IGN);

    struct sigaction sa{};

    sa.sa_handler =
        OnSignal;

    sigemptyset(&sa.sa_mask);

    sigaction(SIGTERM, &sa, nullptr);
    sigaction(SIGINT, &sa, nullptr);

    LOGI(
        "========================================"
    );

    LOGI(
        "stormdaemon (PONTE READ/WRITE) iniciando"
    );

    LOGI(
        "pid=%d socket=%s",
        getpid(),
        g_SocketPath.c_str()
    );

    LOGI(
        "========================================"
    );

    FileLog(
        "=== stormdaemon iniciado pid=%d socket=%s ===",
        getpid(),
        g_SocketPath.c_str()
    );

    /*
     * ============================================================================
     * (V10 KERNEL) PROBE DO DRIVER DE KERNEL NO START
     * ============================================================================
     * O resultado fica guardado (RT*) e o painel consulta via
     * BRIDGE_CMD_KERNEL_STATUS. Logs do RTKernel seguem o --verbose aqui
     * no start; quando o usuario liga o toggle "Kernel RW" no painel,
     * os logs do driver ligam sozinhos (opt-in).
     * ============================================================================
     */
    RT::SetLogVerbose(g_Verbose.load() ? true : false);

    if (RT::Probe())
    {
        LOGI(
            "[KERNEL] driver de kernel DISPONIVEL em %s (self-test: leitura OK, escrita %s) — pronto pro toggle do painel",
            RT::DevicePath(),
            RT::WriteSelfTestOk() ? "OK" : "FALHOU"
        );

        FileLog(
            "kernel driver disponivel: %s writeOk=%d",
            RT::DevicePath(),
            RT::WriteSelfTestOk() ? 1 : 0
        );
    }
    else
    {
        LOGI(
            "[KERNEL] driver de kernel NAO encontrado — modo kernel fica indisponivel ate o modulo .ko ser carregado"
        );

        FileLog("kernel driver: ausente no start");
    }

    unlink(g_SocketPath.c_str());

    int serverFd =
        socket(
            AF_UNIX,
            SOCK_STREAM,
            0
        );

    if (serverFd < 0)
    {
        LOGE(
            "socket() falhou: %s",
            strerror(errno)
        );

        return 1;
    }

    sockaddr_un addr{};

    addr.sun_family =
        AF_UNIX;

    strncpy(
        addr.sun_path,
        g_SocketPath.c_str(),
        sizeof(addr.sun_path) - 1
    );

    if (bind(
            serverFd,
            reinterpret_cast<sockaddr*>(&addr),
            sizeof(addr)
        ) < 0)
    {
        LOGE(
            "bind(%s) falhou: %s",
            g_SocketPath.c_str(),
            strerror(errno)
        );

        return 1;
    }

    /*
     * O client roda no processo do app (uid app). O socket precisa
     * ficar acessível para ele conectar.
     */
    chmod(
        g_SocketPath.c_str(),
        0666
    );

    if (listen(serverFd, 8) < 0)
    {
        LOGE(
            "listen() falhou: %s",
            strerror(errno)
        );

        return 1;
    }

    WritePidFile();

    LOGI(
        "[OK] ponte pronta e aguardando client em %s",
        g_SocketPath.c_str()
    );

    FileLog("ponte pronta, aguardando client");

    /*
     * Thread de estatística periódica: ajuda a confirmar no logcat
     * que a ponte está realmente sendo usada.
     */
    std::thread(
        []
        {
            uint64_t lastReads = 0;
            uint64_t lastWrites = 0;

            while (!g_Stop.load())
            {
                for (int i = 0; i < 10 && !g_Stop.load(); i++)
                    std::this_thread::sleep_for(
                        std::chrono::seconds(1)
                    );

                if (g_Stop.load())
                    break;

                uint64_t reads =
                    g_TotalReads.load();

                uint64_t writes =
                    g_TotalWrites.load();

                if (reads != lastReads ||
                    writes != lastWrites)
                {

                    const StormRW::Stats rwStats = StormRW::GetStats();
                    LOGI(
                        "[STATS] reads=%llu (+%llu) writes=%llu (+%llu) erros=%llu | cache: hit=%llu neg=%llu miss=%llu syscalls=%llu retries=%llu ttl=%lldms | bypass: directR=%llu exact=%llu vmfbR=%llu directW=%llu vmfbW=%llu wlock=%llu on=%d fb=%d | kernel: on=%d avail=%d kR=%llu kW=%llu kE=%llu",
                        (unsigned long long)reads,
                        (unsigned long long)(reads - lastReads),
                        (unsigned long long)writes,
                        (unsigned long long)(writes - lastWrites),
                        (unsigned long long)g_TotalErrors.load(),
                        (unsigned long long)rwStats.hits,
                        (unsigned long long)rwStats.negHits,
                        (unsigned long long)rwStats.misses,
                        (unsigned long long)rwStats.syscalls,
                        (unsigned long long)rwStats.retries,
                        StormRW::GetTtlMs(),
                        (unsigned long long)rwStats.directReads,
                        (unsigned long long)rwStats.exactFb,
                        (unsigned long long)rwStats.vmFbReads,
                        (unsigned long long)rwStats.directWrites,
                        (unsigned long long)rwStats.vmFbWrites,
                        (unsigned long long)rwStats.wrKilled,
                        (int)rwStats.writesOn,
                        (int)STORM_VM_FALLBACK,
                        (int)g_KernelMode.load(),
                        RT::IsAvailable() ? 1 : 0,
                        (unsigned long long)g_KernelReads.load(),
                        (unsigned long long)g_KernelWrites.load(),
                        (unsigned long long)g_KernelErrors.load()
                    );

                    FileLog(
                        "stats reads=%llu writes=%llu erros=%llu | cache hit=%llu neg=%llu miss=%llu syscalls=%llu retries=%llu ttl=%lldms | bypass directR=%llu vmfbR=%llu directW=%llu vmfbW=%llu",
                        (unsigned long long)reads,
                        (unsigned long long)writes,
                        (unsigned long long)g_TotalErrors.load(),
                        (unsigned long long)rwStats.hits,
                        (unsigned long long)rwStats.negHits,
                        (unsigned long long)rwStats.misses,
                        (unsigned long long)rwStats.syscalls,
                        (unsigned long long)rwStats.retries,
                        StormRW::GetTtlMs(),
                        (unsigned long long)rwStats.directReads,
                        (unsigned long long)rwStats.vmFbReads,
                        (unsigned long long)rwStats.directWrites,
                        (unsigned long long)rwStats.vmFbWrites
                    );

                    lastReads = reads;
                    lastWrites = writes;
                }
            }
        }
    ).detach();

    while (!g_Stop.load())
    {
        struct pollfd pfd{};

        pfd.fd = serverFd;
        pfd.events = POLLIN;

        int pr =
            poll(
                &pfd,
                1,
                500
            );

        if (pr < 0)
        {
            if (errno == EINTR)
                continue;

            LOGE(
                "poll() falhou: %s",
                strerror(errno)
            );

            break;
        }

        if (pr == 0)
            continue;

        int clientFd =
            accept(
                serverFd,
                nullptr,
                nullptr
            );

        if (clientFd < 0)
        {
            if (errno == EINTR)
                continue;

            LOGE(
                "accept() falhou: %s",
                strerror(errno)
            );

            continue;
        }

        struct timeval tv{};

        tv.tv_sec = 5;
        tv.tv_usec = 0;

        setsockopt(
            clientFd,
            SOL_SOCKET,
            SO_RCVTIMEO,
            &tv,
            sizeof(tv)
        );

        setsockopt(
            clientFd,
            SOL_SOCKET,
            SO_SNDTIMEO,
            &tv,
            sizeof(tv)
        );

        std::thread(
            ClientThread,
            clientFd
        ).detach();
    }

    LOGI("stormdaemon encerrando...");

    close(serverFd);

    unlink(g_SocketPath.c_str());
    unlink(g_PidFilePath.c_str());

    FileLog("=== stormdaemon encerrado ===");

    LOGI("stormdaemon encerrado");

    return 0;
}
