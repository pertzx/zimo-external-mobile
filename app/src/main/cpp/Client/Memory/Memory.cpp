#include "Memory.hpp"

#include "BridgeClient.hpp"

#include <Globals.hpp>
#include <Offsets/Offsets.hpp>

#include <android/log.h>

#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/uio.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <errno.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#define LOGI(...) \
    __android_log_print( \
        ANDROID_LOG_INFO, \
        "StormMemory", \
        __VA_ARGS__ \
    )

#define LOGE(...) \
    __android_log_print( \
        ANDROID_LOG_ERROR, \
        "StormMemory", \
        __VA_ARGS__ \
    )

#define LOGW(...) \
    __android_log_print( \
        ANDROID_LOG_WARN, \
        "StormMemory", \
        __VA_ARGS__ \
    )

Memory g_FreeFireMemory;

uintptr_t libAddress = 0;

pid_t Memory::s_TargetPid = -1;
uintptr_t Memory::s_LibIl2Cpp = 0;
bool Memory::s_Target32Bit = false;
int Memory::s_ProcMemFd = -1;
bool Memory::s_Initialized = false;
// volatile bool Memory::s_RestartInProgress = false;
std::atomic<bool> Memory::s_RestartInProgress{ false };
std::atomic<long long> Memory::s_LastRestartMs{ 0 };
const char* Memory::s_LastInitError = "nao inicializado";

namespace
{
    static constexpr const char* MEMORY_BACKEND_VERSION =
        "StormMemory-2026-09-13-BRIDGE-V1";

    /*
     * Prioridade de acesso:
     *
     *  1. PONTE  -> daemon root em /data/local/tmp/stormdaemon
     *                (process_vm_readv/writev com fallback /proc/pid/mem,
     *                 executado COM privilegio root pelo daemon)
     *  2. VM     -> process_vm_readv/writev direto do processo do app
     *                (so funciona se o app tiver privilegio, ex: emulador
     *                 rodando como root; no aparelho normal da EPERM)
     *  3. MEMFD  -> pread64/pwrite64 em /proc/<pid>/mem aberto localmente
     *
     * A ponte sempre vem primeiro; o fallback mantem o comportamento
     * antigo quando o daemon ainda nao subiu.
     */
    static std::atomic<uint64_t> g_BridgeReads{ 0 };
    static std::atomic<uint64_t> g_BridgeWrites{ 0 };
    static std::atomic<uint64_t> g_FallbackReads{ 0 };
    static std::atomic<uint64_t> g_FallbackWrites{ 0 };
    static std::atomic<uint64_t> g_FailLogs{ 0 };

    /*
     * Anti-spam: em falha sustentada loga no maximo 1x por segundo.
     */
    static std::atomic<long long> g_LastFailLogMs{ 0 };

    static long long NowMs()
    {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()
        ).count();
    }

    static bool ShouldLogFail()
    {
        long long now = NowMs();
        long long last = g_LastFailLogMs.load();

        if (now - last < 1000)
            return false;

        return g_LastFailLogMs.compare_exchange_strong(last, now);
    }

    static bool IsNumeric(
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
            if (
                !std::isdigit(
                    static_cast<unsigned char>(*p)
                )
            )
            {
                return false;
            }
        }

        return true;
    }

    static std::string ReadCmdline(
        pid_t pid
    )
    {
        char path[64];

        std::snprintf(
            path,
            sizeof(path),
            "/proc/%d/cmdline",
            pid
        );

        int fd =
            ::open(
                path,
                O_RDONLY | O_CLOEXEC
            );

        if (fd < 0)
            return {};

        char buffer[1024]{};

        ssize_t n =
            ::read(
                fd,
                buffer,
                sizeof(buffer) - 1
            );

        ::close(fd);

        if (n <= 0)
            return {};

        buffer[n] = '\0';

        for (
            ssize_t i = 0;
            i < n;
            ++i
        )
        {
            if (buffer[i] == '\0')
                buffer[i] = ' ';
        }

        return std::string(
            buffer,
            static_cast<size_t>(n)
        );
    }

    static bool ContainsIgnoreCase(
        const std::string& value,
        const char* needle
    )
    {
        if (!needle || !*needle)
            return false;

        std::string a = value;
        std::string b = needle;

        std::transform(
            a.begin(),
            a.end(),
            a.begin(),
            [](unsigned char c)
            {
                return static_cast<char>(
                    std::tolower(c)
                );
            }
        );

        std::transform(
            b.begin(),
            b.end(),
            b.begin(),
            [](unsigned char c)
            {
                return static_cast<char>(
                    std::tolower(c)
                );
            }
        );

        return a.find(b) != std::string::npos;
    }

    static std::vector<std::string>
    GetTargetNames()
    {
        std::vector<std::string> names;

        const char* envPackage =
            std::getenv(
                "STORM_TARGET_PACKAGE"
            );

        if (
            envPackage &&
            *envPackage
        )
        {
            names.emplace_back(
                envPackage
            );
        }

        names.emplace_back(
            "com.dts.freefireth"
        );

        names.emplace_back(
            "com.dts.freefiremax"
        );

        return names;
    }

    static bool HasIl2CppMapping(
        pid_t pid
    )
    {
        char mapsPath[64];

        std::snprintf(
            mapsPath,
            sizeof(mapsPath),
            "/proc/%d/maps",
            pid
        );

        FILE* maps =
            std::fopen(
                mapsPath,
                "r"
            );

        if (!maps)
        {
            LOGE(
                "[PID] PID=%d nao abriu %s: %s",
                pid,
                mapsPath,
                strerror(errno)
            );

            return false;
        }

        char line[4096];
        bool found = false;

        while (
            std::fgets(
                line,
                sizeof(line),
                maps
            )
        )
        {
            if (
                std::strstr(
                    line,
                    "libil2cpp.so"
                )
            )
            {
                found = true;

                LOGI(
                    "[PID] PID=%d possui libil2cpp: %s",
                    pid,
                    line
                );

                break;
            }
        }

        std::fclose(maps);

        return found;
    }
}

pid_t Memory::FindTargetPid()
{
    LOGI(
        "[VERSION] %s",
        MEMORY_BACKEND_VERSION
    );

    const std::vector<std::string> names =
        GetTargetNames();

    /*
     * ----------------------------------------------------------------
     * CAMINHO 1: ponte (daemon root varre /proc com privilegio)
     * ----------------------------------------------------------------
     */
    if (BridgeClient::EnsureConnected())
    {
        int64_t bridgePid =
            BridgeClient::FindPid(names);

        if (bridgePid > 0)
        {
            LOGI(
                "[PID] processo selecionado via PONTE: %lld",
                (long long)bridgePid
            );

            return static_cast<pid_t>(bridgePid);
        }

        LOGW(
            "[PID] ponte nao achou o processo (jogo aberto?)"
        );
    }
    else
    {
        LOGW(
            "[PID] ponte indisponivel em %s - usando busca local",
            BridgeClient::GetSocketPath()
        );
    }

    /*
     * ----------------------------------------------------------------
     * CAMINHO 2: varredura local de /proc (fallback)
     * ----------------------------------------------------------------
     */
    DIR* proc =
        ::opendir("/proc");

    if (!proc)
    {
        LOGE(
            "[PID] Falha abrindo /proc: %s",
            strerror(errno)
        );

        return -1;
    }

    pid_t selectedPid = -1;

    while (
        dirent* entry =
            ::readdir(proc)
    )
    {
        if (
            !IsNumeric(
                entry->d_name
            )
        )
        {
            continue;
        }

        pid_t pid =
            static_cast<pid_t>(
                std::strtol(
                    entry->d_name,
                    nullptr,
                    10
                )
            );

        if (pid <= 0)
            continue;

        std::string cmdline =
            ReadCmdline(pid);

        if (cmdline.empty())
            continue;

        bool packageMatch = false;

        for (
            const std::string& target :
            names
        )
        {
            if (
                ContainsIgnoreCase(
                    cmdline,
                    target.c_str()
                )
            )
            {
                packageMatch = true;
                break;
            }
        }

        if (!packageMatch)
            continue;

        LOGI(
            "[PID] candidato PID=%d cmdline=%s",
            pid,
            cmdline.c_str()
        );

        if (!HasIl2CppMapping(pid))
        {
            LOGI(
                "[PID] PID=%d ignorado: libil2cpp.so nao encontrada",
                pid
            );

            continue;
        }

        selectedPid = pid;

        LOGI(
            "[PID] processo selecionado (local): %d",
            selectedPid
        );

        break;
    }

    ::closedir(proc);

    if (selectedPid > 0)
    {
        LOGI(
            "PID encontrado: %d",
            selectedPid
        );
    }
    else
    {
        LOGE(
            "Nenhum processo alvo com libil2cpp.so encontrado"
        );
    }

    return selectedPid;
}

uintptr_t Memory::FindModuleBase(
    pid_t pid,
    const char* moduleName,
    int index
    )
{
    if (pid <= 0 || moduleName == nullptr || moduleName[0] == '\0') {
        LOGE("[LIB] parametros invalidos");
        return 0;
    }

    /*
     * A ponte trabalha com o primeiro mapping (index 1), que é o único
     * caso usado pelo projeto inteiro.
     */
    if (index == 1 && BridgeClient::IsConnected())
    {
        uint64_t bridgeBase =
            BridgeClient::ModuleBase(
                static_cast<uint32_t>(pid),
                moduleName
            );

        if (bridgeBase != 0)
        {
            LOGI(
                "[LIB] base via PONTE: %s @ 0x%lX (PID=%d)",
                moduleName,
                static_cast<unsigned long>(bridgeBase),
                pid
            );

            return static_cast<uintptr_t>(bridgeBase);
        }

        LOGW(
            "[LIB] ponte nao achou %s no PID=%d - tentando local",
            moduleName,
            pid
        );
    }

    char mapsPath[64];

    snprintf(
        mapsPath,
        sizeof(mapsPath),
        "/proc/%d/maps",
        pid
    );

    FILE* fp = fopen(mapsPath, "r");

    if (!fp) {
        LOGE(
            "[LIB] falha ao abrir %s: errno=%d",
            mapsPath,
            errno
        );
        return 0;
    }

    char line[1024];
    int currentIndex = 0;
    uintptr_t start = 0;

    while (fgets(line, sizeof(line), fp)) {

        if (strstr(line, moduleName) == nullptr)
            continue;

        currentIndex++;

        if (currentIndex != index)
            continue;

        unsigned long long mapStart = 0;

        if (sscanf(
                line,
                "%llx-%*llx",
                &mapStart
            ) == 1)
        {
            start = static_cast<uintptr_t>(mapStart);

            LOGI("[LIB] encontrada: %s", moduleName);
            LOGI("[LIB] PID=%d", pid);
            LOGI("[LIB] index=%d", index);
            LOGI(
                "[LIB] start=0x%lX",
                static_cast<unsigned long>(start)
            );
            LOGI("[LIB] linha=%s", line);
        }

        break;
    }

    fclose(fp);

    if (start == 0) {
        LOGE(
            "[LIB] %s nao encontrada no PID=%d index=%d",
            moduleName,
            pid,
            index
        );
    }

    return start;
}

bool Memory::DetectTarget32Bit(
    pid_t pid,
    bool& outIs32
)
{
    outIs32 = false;

    if (pid <= 0)
        return false;

    /*
     * CAMINHO 1: ponte (daemon lê /proc/<pid>/exe com privilegio root).
     * Este é o caminho confiável — autoritativo.
     */
    if (BridgeClient::IsConnected())
    {
        bool is32 = false;

        if (BridgeClient::Is32Bit(
                static_cast<uint32_t>(pid),
                is32
            ))
        {
            LOGI(
                "Target ELF = %s (via PONTE)",
                is32 ? "32-bit" : "64-bit"
            );

            outIs32 = is32;

            return true;
        }

        LOGW(
            "Ponte conectada mas IS_32BIT falhou (daemon antigo/protocolo diferente?)"
        );
    }

    /*
     * CAMINHO 2: leitura local do ELF (fallback).
     *
     * ATENÇÃO: sem root, readlink(/proc/<pid>/exe) de OUTRO app falha
     * com EACCES. Nesse caso a resposta é "DESCONHECIDO" — e NUNCA
     * mais "false = 64-bit" (era isso que detectava FF 32-bit como 64).
     */
    char path[64];

    std::snprintf(
        path,
        sizeof(path),
        "/proc/%d/exe",
        pid
    );

    char exePath[PATH_MAX]{};

    ssize_t len =
        ::readlink(
            path,
            exePath,
            sizeof(exePath) - 1
        );

    if (len <= 0)
    {
        LOGE(
            "readlink(%s) falhou: %s (sem root nao da pra ler o ELF localmente)",
            path,
            strerror(errno)
        );

        return false;
    }

    exePath[len] = '\0';

    int fd =
        ::open(
            exePath,
            O_RDONLY | O_CLOEXEC
        );

    if (fd < 0)
    {
        LOGE(
            "Nao foi possivel abrir ELF do processo: %s",
            strerror(errno)
        );

        return false;
    }

    unsigned char ident[5]{};

    ssize_t readBytes =
        ::pread(
            fd,
            ident,
            sizeof(ident),
            0
        );

    ::close(fd);

    if (
        readBytes !=
        sizeof(ident)
    )
    {
        LOGE(
            "Falha lendo ELF"
        );

        return false;
    }

    if (
        ident[0] != 0x7F ||
        ident[1] != 'E' ||
        ident[2] != 'L' ||
        ident[3] != 'F'
    )
    {
        LOGE(
            "ELF invalido"
        );

        return false;
    }

    if (ident[4] == 1)
    {
        LOGI(
            "Target ELF = 32-bit (local)"
        );

        outIs32 = true;

        return true;
    }

    if (ident[4] == 2)
    {
        LOGI(
            "Target ELF = 64-bit (local)"
        );

        outIs32 = false;

        return true;
    }

    LOGE(
        "ELF class desconhecida: %u",
        ident[4]
    );

    return false;
}

bool Memory::OpenProcessMemory(
    pid_t pid
)
{
    CloseProcessMemory();

    if (pid <= 0)
        return false;

    /*
     * A ponte NAO precisa de fd local: ela abre /proc/<pid>/mem dentro
     * do daemon, como root. Este fd local existe apenas para o fallback
     * (casos em que o proprio app tem privilegio).
     */
    char path[64];

    std::snprintf(
        path,
        sizeof(path),
        "/proc/%d/mem",
        pid
    );

    s_ProcMemFd =
        ::open(
            path,
            O_RDWR | O_CLOEXEC
        );

    if (s_ProcMemFd < 0)
    {
        LOGI(
            "fd local %s indisponivel (%s) - leitura/escrita sera 100%% pela PONTE",
            path,
            strerror(errno)
        );

        s_ProcMemFd = -1;

        return true;
    }

    LOGI(
        "fd local /proc/%d/mem aberto (fallback ativo)",
        pid
    );

    return true;
}

void Memory::CloseProcessMemory()
{
    if (s_ProcMemFd >= 0)
    {
        ::close(
            s_ProcMemFd
        );

        s_ProcMemFd = -1;
    }
}

bool Memory::ReadProcessVm(
    uintptr_t address,
    void* buffer,
    size_t size
)
{
#if defined(SYS_process_vm_readv)

    if (
        s_TargetPid <= 0 ||
        !buffer ||
        size == 0
    )
    {
        return false;
    }

    iovec local{};

    local.iov_base = buffer;
    local.iov_len = size;

    iovec remote{};

    remote.iov_base =
        reinterpret_cast<void*>(
            address
        );

    remote.iov_len = size;

    errno = 0;

    ssize_t result =
        static_cast<ssize_t>(
            ::syscall(
                SYS_process_vm_readv,
                s_TargetPid,
                &local,
                1,
                &remote,
                1,
                0
            )
        );

    return result ==
        static_cast<ssize_t>(
            size
        );

#else

    (void)address;
    (void)buffer;
    (void)size;

    return false;

#endif
}

bool Memory::WriteProcessVm(
    uintptr_t address,
    const void* buffer,
    size_t size
)
{
#if defined(SYS_process_vm_writev)

    if (
        s_TargetPid <= 0 ||
        !buffer ||
        size == 0
    )
    {
        return false;
    }

    iovec local{};

    local.iov_base =
        const_cast<void*>(
            buffer
        );

    local.iov_len = size;

    iovec remote{};

    remote.iov_base =
        reinterpret_cast<void*>(
            address
        );

    remote.iov_len = size;

    errno = 0;

    ssize_t result =
        static_cast<ssize_t>(
            ::syscall(
                SYS_process_vm_writev,
                s_TargetPid,
                &local,
                1,
                &remote,
                1,
                0
            )
        );

    return result ==
        static_cast<ssize_t>(
            size
        );

#else

    (void)address;
    (void)buffer;
    (void)size;

    return false;

#endif
}

bool Memory::ReadExactFile(
    int fd,
    uintptr_t address,
    void* buffer,
    size_t size
)
{
    if (
        fd < 0 ||
        !buffer ||
        size == 0
    )
    {
        return false;
    }

    size_t total = 0;

    while (
        total < size
    )
    {
        ssize_t n =
            ::pread64(
                fd,
                static_cast<char*>(
                    buffer
                ) + total,
                size - total,
                static_cast<off64_t>(
                    address + total
                )
            );

        if (n <= 0)
            return false;

        total +=
            static_cast<size_t>(
                n
            );
    }

    return true;
}

bool Memory::WriteExactFile(
    int fd,
    uintptr_t address,
    const void* buffer,
    size_t size
)
{
    if (
        fd < 0 ||
        !buffer ||
        size == 0
    )
    {
        return false;
    }

    size_t total = 0;

    while (
        total < size
    )
    {
        ssize_t n =
            ::pwrite64(
                fd,
                static_cast<const char*>(
                    buffer
                ) + total,
                size - total,
                static_cast<off64_t>(
                    address + total
                )
            );

        if (n <= 0)
            return false;

        total +=
            static_cast<size_t>(
                n
            );
    }

    return true;
}

bool Memory::ReadProcMem(
    uintptr_t address,
    void* buffer,
    size_t size
)
{
    if (s_ProcMemFd < 0)
        return false;

    return ReadExactFile(
        s_ProcMemFd,
        address,
        buffer,
        size
    );
}

bool Memory::WriteProcMem(
    uintptr_t address,
    const void* buffer,
    size_t size
)
{
    if (s_ProcMemFd < 0)
        return false;

    return WriteExactFile(
        s_ProcMemFd,
        address,
        buffer,
        size
    );
}

bool Memory::Read(
    uintptr_t address,
    void* outValue,
    size_t size
)
{
    /*
     * IMPORTANTE:
     *
     * Nao usamos s_Initialized aqui.
     *
     * GameConfig() roda DURANTE Initialize()
     * e precisa conseguir ler memoria antes
     * de s_Initialized virar true.
     */
    if (
        s_TargetPid <= 0 ||
        address == 0 ||
        !outValue ||
        size == 0
    )
    {
        return false;
    }

    /*
     * ----------------------------------------------------------------
     * CAMINHO 1: PONTE (daemon root). Este é o caminho principal em
     * produção - o processo do app não tem permissão de ptrace no
     * jogo, o daemon tem.
     * ----------------------------------------------------------------
     */
    if (
        BridgeClient::ReadMem(
            static_cast<uint32_t>(s_TargetPid),
            static_cast<uint64_t>(address),
            outValue,
            static_cast<uint32_t>(size)
        )
    )
    {
        g_BridgeReads++;

        return true;
    }

    /*
     * ----------------------------------------------------------------
     * CAMINHO 2/3: fallback local (process_vm_readv -> /proc/pid/mem).
     * Funciona apenas quando o app roda privilegiado (ex: emulador
     * com root). Loga uma vez por segundo para nao floodar.
     * ----------------------------------------------------------------
     */
    if (
        ReadProcessVm(
            address,
            outValue,
            size
        )
    )
    {
        g_FallbackReads++;

        return true;
    }

    if (
        ReadProcMem(
            address,
            outValue,
            size
        )
    )
    {
        g_FallbackReads++;

        return true;
    }

    if (ShouldLogFail())
    {
        BridgeClient::Stats s =
            BridgeClient::GetStats();

        LOGE(
            "[READ FALHOU] addr=0x%lX size=%zu pid=%d | ponte: reads=%llu writes=%llu erros=%llu reconexoes=%llu conectado=%d",
            static_cast<unsigned long>(address),
            size,
            s_TargetPid,
            (unsigned long long)s.Reads,
            (unsigned long long)s.Writes,
            (unsigned long long)s.Errors,
            (unsigned long long)s.Reconnects,
            BridgeClient::IsConnected() ? 1 : 0
        );
    }

    return false;
}

bool Memory::ReadBatch(
    const BatchItem* items,
    size_t count,
    std::vector<uint8_t>& outBlob
)
{
    outBlob.clear();

    if (!items || count == 0 || s_TargetPid <= 0)
        return false;

    /*
     * Converte os itens para o formato da ponte (uint64 addr) e faz UM
     * pedido só. A ponte devolve os dados concatenados; item com falha
     * vem zerado, que é exatamente o que os leitores da cadeia esperam
     * (ponteiro 0 = descarta a entidade, igual ao READ individual).
     */
    std::vector<BridgeClient::BatchItem> bridgeItems(count);

    for (size_t i = 0; i < count; i++)
    {
        bridgeItems[i].address =
            static_cast<uint64_t>(items[i].address);

        bridgeItems[i].size = items[i].size;
    }

    return BridgeClient::ReadBatch(
        bridgeItems.data(),
        static_cast<uint32_t>(count),
        outBlob
    );
}

bool Memory::Write(
    uintptr_t address,
    const void* value,
    size_t size
)
{
    if (
        s_TargetPid <= 0 ||
        address == 0 ||
        !value ||
        size == 0
    )
    {
        return false;
    }

    /*
     * CAMINHO 1: PONTE (daemon root). Toda escrita de exploit sai daqui.
     */
    if (
        BridgeClient::WriteMem(
            static_cast<uint32_t>(s_TargetPid),
            static_cast<uint64_t>(address),
            value,
            static_cast<uint32_t>(size)
        )
    )
    {
        g_BridgeWrites++;

        return true;
    }

    /*
     * CAMINHOS 2/3: fallback local.
     */
    if (
        WriteProcessVm(
            address,
            value,
            size
        )
    )
    {
        g_FallbackWrites++;

        return true;
    }

    if (
        WriteProcMem(
            address,
            value,
            size
        )
    )
    {
        g_FallbackWrites++;

        return true;
    }

    if (ShouldLogFail())
    {
        LOGE(
            "[WRITE FALHOU] addr=0x%lX size=%zu pid=%d (ponte conectada=%d)",
            static_cast<unsigned long>(address),
            size,
            s_TargetPid,
            BridgeClient::IsConnected() ? 1 : 0
        );
    }

    return false;
}

std::string Memory::String(
    uintptr_t address,
    int maxLength
)
{
    if (
        address == 0 ||
        maxLength <= 0
    )
    {
        return {};
    }

    if (maxLength > 256)
        maxLength = 256;

    const uintptr_t lengthAddress =
        s_Target32Bit
            ? address + 0x8
            : address + 0x10;

    const uintptr_t charsAddress =
        s_Target32Bit
            ? address + 0xC
            : address + 0x14;

    int32_t length = 0;

    if (
        !Read(
            lengthAddress,
            length
        )
    )
    {
        return {};
    }

    if (
        length <= 0 ||
        length > maxLength
    )
    {
        return {};
    }

    std::vector<uint16_t> chars(
        static_cast<size_t>(
            length
        ) + 1
    );

    if (
        !Read(
            charsAddress,
            chars.data(),
            static_cast<size_t>(
                length
            ) *
            sizeof(uint16_t)
        )
    )
    {
        return {};
    }

    std::string result;

    result.reserve(
        static_cast<size_t>(
            length
        )
    );

    for (
        int i = 0;
        i < length;
        ++i
    )
    {
        uint16_t c =
            chars[
                static_cast<size_t>(
                    i
                )
            ];

        if (c == 0)
            break;

        if (c < 0x80)
        {
            result.push_back(
                static_cast<char>(c)
            );
        }
        else if (c < 0x800)
        {
            result.push_back(
                static_cast<char>(
                    0xC0 |
                    (c >> 6)
                )
            );

            result.push_back(
                static_cast<char>(
                    0x80 |
                    (c & 0x3F)
                )
            );
        }
        else
        {
            result.push_back(
                static_cast<char>(
                    0xE0 |
                    (c >> 12)
                )
            );

            result.push_back(
                static_cast<char>(
                    0x80 |
                    ((c >> 6) & 0x3F)
                )
            );

            result.push_back(
                static_cast<char>(
                    0x80 |
                    (c & 0x3F)
                )
            );
        }
    }

    return result;
}

uintptr_t Memory::GetModuleAddress(
    const char* moduleName
)
{
    if (s_TargetPid <= 0)
        return 0;

    return FindModuleBase(
        s_TargetPid,
        moduleName
    );
}

std::vector<uintptr_t>
Memory::GetModuleAddress(
    bool N32
)
{
    (void)N32;

    std::vector<uintptr_t> result;

    uintptr_t base =
        GetModuleAddress(
            "libil2cpp.so"
        );

    if (base != 0)
        result.push_back(base);

    return result;
}

pid_t Memory::GetTargetPid()
{
    return s_TargetPid;
}

bool Memory::IsTarget32Bit()
{
    return s_Target32Bit;
}

uintptr_t Memory::GetLibIl2Cpp()
{
    return s_LibIl2Cpp;
}

void Memory::EnableOpLogging(
    bool enabled
)
{
    BridgeClient::EnableOpLogging(enabled);

    LOGI(
        "log individual de READ/WRITE: %s",
        enabled ? "ON" : "OFF"
    );
}

bool Memory::IsInitialized()
{
    return s_Initialized;
}

const char* Memory::GetLastInitError()
{
    return s_LastInitError;
}

bool Memory::IsBridgeConnected()
{
    return BridgeClient::IsConnected();
}

bool Memory::Initialize()
{
    LOGI("[INIT] ========================================");
    LOGI("[INIT] Memory::Initialize()");
    LOGI("[INIT] Backend=%s", MEMORY_BACKEND_VERSION);
    LOGI("[INIT] Ponte=%s em %s",
         BridgeClient::EnsureConnected() ? "CONECTADA" : "INDISPONIVEL (fallback local)",
         BridgeClient::GetSocketPath());

    LOGI("[INIT] Procurando processo do Free Fire...");

    if (s_Initialized)
    {
        LOGI("[INIT] Memory ja inicializada: pid=%d lib=0x%lX",
             s_TargetPid, static_cast<unsigned long>(s_LibIl2Cpp));
        return true;
    }

    pid_t pid = FindTargetPid();

    if (pid <= 0)
    {
        LOGE("[INIT] FALHA: PID nao encontrado");
        s_LastInitError = "jogo nao encontrado (FF aberto? daemon rodando?)";
        return false;
    }

    LOGI("[INIT] PID encontrado: %d", pid);

    /*
     * ================================================================
     * ARQUITETURA: vem do combo "Game Type" da Settings — NADA de
     * deteccao automatica (DetectTarget32Bit saiu do caminho).
     *
     *   GameProfile 0 = FF v7a b75  -> 32-bit, ponteiros de 4 bytes
     *   GameProfile 1 = FF v7a b76  -> 32-bit, ponteiros de 4 bytes
     *   GameProfile 2 = FF v8a      -> 64-bit, ponteiros de 8 bytes
     * ================================================================
     */
    const bool profileIs32 = (g_Globals.General.GameProfile != 2);

    LOGI("[INIT] Game Type: %s (GameProfile=%d)",
         profileIs32 ? "FF v7a / 32-bit (ptr 4 bytes)" : "FF v8a / 64-bit (ptr 8 bytes)",
         g_Globals.General.GameProfile);

    /*
     * A UNICA coisa que o app ainda procura sozinho: a base da libil2cpp.
     */
    uintptr_t il2cpp = FindModuleBase(pid, "libil2cpp.so");

    if (il2cpp == 0)
    {
        LOGE("[INIT] FALHA: libil2cpp.so nao encontrada no PID %d", pid);
        s_LastInitError = "libil2cpp.so nao encontrada no processo";
        return false;
    }

    LOGI("[INIT] libil2cpp.so = 0x%lX", static_cast<unsigned long>(il2cpp));

    s_TargetPid   = pid;
    s_Target32Bit = profileIs32;
    s_LibIl2Cpp   = il2cpp;

    libAddress = il2cpp;

    bool opened = OpenProcessMemory(pid);

    LOGI("[INIT] OpenProcessMemory = %s", opened ? "OK" : "FALHA");

    if (!opened)
    {
        LOGE("[INIT] FALHA abrindo memoria do processo");
        s_LastInitError = "falha abrindo memoria do processo (ponte/local)";
        Shutdown();
        return false;
    }

    Offsets::LibIl2Cpp = il2cpp;

    Offsets::LibIl2CppCandidates.clear();
    Offsets::LibIl2CppCandidates.push_back(il2cpp);

    /*
     * Flags derivadas da escolha — o ReadLoop/Silent/Skeleton leem
     * ponteiros com o template N32 (v7a = Read<uint32_t> 4 bytes,
     * v8a = Read<uint64_t> 8 bytes). Nao ha nada manual aqui.
     */
    g_Globals.General.N32     = profileIs32;
    g_Globals.General.V31     = profileIs32;
    g_Globals.General.NoAnogs = profileIs32;

    LOGI("[INIT] N32=%d V31=%d (leitura de ptr: %d bytes)",
         g_Globals.General.N32 ? 1 : 0,
         g_Globals.General.V31 ? 1 : 0,
         profileIs32 ? 4 : 8);

    LOGI("[INIT] Offsets::LibIl2Cpp = 0x%lX", static_cast<unsigned long>(Offsets::LibIl2Cpp));

    LOGI("[INIT] Chamando Offsets::GameConfig()...");
    Offsets::GameConfig();
    LOGI("[INIT] GameConfig terminou (perfil aplicado direto, sem validacao)");

    /*
     * SEM gate de Loaded(): inicia direto. Se algum offset-chave ficou
     * 0, o GameConfig ja avisou no logcat (tag StormOffsets) e o log da
     * cadeia mostra exatamente onde a leitura para.
     */
    s_Initialized = true;

    LOGI("[INIT] Memory::Initialize() = SUCCESS");
    LOGI("[INIT] ========================================");

    return true;
}

bool Memory::Restart()
{
    LOGI("[RESTART] reiniciando memoria (pid/base podem ter mudado)");

    BridgeClient::Disconnect();

    Shutdown();

    std::this_thread::sleep_for(
        std::chrono::milliseconds(
            100
        )
    );

    return Initialize();
}

bool Memory::RestartAsync()
{
    /*
     * COOLDOWN: sem isso, os 3 chamadores (init por frame com base==0,
     * ELF-fail a cada 20 frames e o watchdog de snapshot stale) geram
     * dezenas de ciclos Disconnect->Shutdown->Initialize por segundo
     * quando a ponte/jogo nao estao disponiveis. Maximo: 1 restart
     * real a cada 2s.
     */
    static constexpr long long RESTART_COOLDOWN_MS = 2000;

    const long long now =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()
        ).count();

    if (now - s_LastRestartMs.load(std::memory_order_relaxed) < RESTART_COOLDOWN_MS)
        return false;

    /*
     * SINGLE-FLIGHT real: exchange atomica. A versao antiga usava
     * volatile bool com check-then-set (corrida) — duas threads
     * passavam juntas e rodavam Restart() em paralelo.
     */
    if (s_RestartInProgress.exchange(true, std::memory_order_acquire))
        return false;

    s_LastRestartMs.store(now, std::memory_order_relaxed);

    std::thread(
        []
        {
            Memory::Restart();

            /*
             * Cooldown conta a partir do FIM do restart tambem: se o
             * Restart() demorar mais que o cooldown, nao deixamos um
             * novo restart partir instantaneamente apos o fim deste.
             */
            s_LastRestartMs.store(
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now().time_since_epoch()
                ).count(),
                std::memory_order_relaxed
            );

            s_RestartInProgress.store(false, std::memory_order_release);
        }
    ).detach();

    return true;
}

bool Memory::RefreshCR3()
{
    pid_t currentPid =
        FindTargetPid();

    if (currentPid <= 0)
        return false;

    uintptr_t currentLib =
        FindModuleBase(
            currentPid,
            "libil2cpp.so"
        );

    if (currentLib == 0)
        return false;

    bool changed =
        currentPid != s_TargetPid ||
        currentLib != s_LibIl2Cpp;

    if (!changed)
        return false;

    LOGI(
        "[REFRESH] alvo mudou: pid %d -> %d, lib 0x%lX -> 0x%lX — re-aplicando GameProfile",
        s_TargetPid,
        currentPid,
        static_cast<unsigned long>(s_LibIl2Cpp),
        static_cast<unsigned long>(currentLib)
    );

    Shutdown();

    const bool profileIs32 = (g_Globals.General.GameProfile != 2);

    s_TargetPid   = currentPid;
    s_Target32Bit = profileIs32;
    s_LibIl2Cpp   = currentLib;

    libAddress =
        currentLib;

    if (!OpenProcessMemory(currentPid))
    {
        Shutdown();

        return false;
    }

    Offsets::LibIl2Cpp =
        currentLib;

    Offsets::LibIl2CppCandidates.clear();
    Offsets::LibIl2CppCandidates.push_back(currentLib);

    g_Globals.General.N32     = profileIs32;
    g_Globals.General.V31     = profileIs32;
    g_Globals.General.NoAnogs = profileIs32;

    Offsets::GameConfig();

    s_Initialized = true;

    LOGI(
        "Target atualizado: PID=%d lib=0x%lX (perfil %d re-aplicado)",
        currentPid,
        static_cast<unsigned long>(currentLib),
        g_Globals.General.GameProfile
    );

    return true;
}

void Memory::Shutdown()
{
    CloseProcessMemory();

    s_Initialized = false;
    s_TargetPid = -1;
    s_LibIl2Cpp = 0;
    s_Target32Bit = false;

    libAddress = 0;
}

void Memory::FlushTLB()
{
    /*
     * Com acesso via /proc/<pid>/mem (local ou pela ponte) não há
     * TLB de kernel a invalidar do lado do leitor. Mantido como
     * no-op para não quebrar a API.
     */
}

void Memory::FlushAllTLB()
{
}

bool Memory::TranslateVA(
    uintptr_t guestVA,
    uintptr_t& physicalOut
)
{
    physicalOut =
        guestVA;

    return guestVA != 0;
}
