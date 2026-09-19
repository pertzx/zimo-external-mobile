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
     * PONTE READ: syscall direta __NR_pread64 + cache de blocos.
     */
    bool BridgeReadMem(
        pid_t pid,
        uint64_t address,
        void* buffer,
        size_t size
    )
    {
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
                    sizeof(BridgeStatsPayload) == 160,
                    "payload de stats mudou — atualize o cliente junto"
                );

                const StormRW::Stats rs = StormRW::GetStats();

                BridgeStatsPayload sp{};

                sp.bridgeReads       = g_TotalReads.load();
                sp.bridgeWrites      = g_TotalWrites.load();
                sp.bridgeErrors      = g_TotalErrors.load();
                sp.uptimeSec         = (uint64_t)((NowMsDaemon() - g_StartMs) / 1000LL);

                sp.cacheHits         = rs.hits;
                sp.cacheNegHits      = rs.negHits;
                sp.cacheMisses       = rs.misses;
                sp.syscalls          = rs.syscalls;
                sp.retries           = rs.retries;

                sp.directReads       = rs.directReads;
                sp.exactFb           = rs.exactFb;
                sp.vmFbReads         = rs.vmFbReads;

                sp.directWrites      = rs.directWrites;
                sp.vmFbWrites        = rs.vmFbWrites;

                sp.negCreated        = rs.negCreated;
                sp.openFails         = rs.openFails;
                sp.wrRefused         = rs.wrRefused;
                sp.wrKilled          = rs.wrKilled;

                sp.writesOn          = rs.writesOn;
                sp.vmFallbackOn      = (uint32_t)STORM_VM_FALLBACK;
                sp.daemonProtoVersion = BRIDGE_PROTO_VERSION;
                sp.reserved0         = 0;

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

                uint64_t base =
                    FindModuleBase(
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
                        "[STATS] reads=%llu (+%llu) writes=%llu (+%llu) erros=%llu | cache: hit=%llu neg=%llu miss=%llu syscalls=%llu retries=%llu ttl=%lldms | bypass: directR=%llu exact=%llu vmfbR=%llu directW=%llu vmfbW=%llu wlock=%llu on=%d fb=%d",
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
                        (int)STORM_VM_FALLBACK
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
