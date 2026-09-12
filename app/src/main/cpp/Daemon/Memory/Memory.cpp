#include "Memory.hpp"

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

Memory g_FreeFireMemory;

uintptr_t libAddress = 0;

pid_t Memory::s_TargetPid = -1;
uintptr_t Memory::s_LibIl2Cpp = 0;
bool Memory::s_Target32Bit = false;
int Memory::s_ProcMemFd = -1;
bool Memory::s_Initialized = false;
volatile bool Memory::s_RestartInProgress = false;

namespace
{
    static constexpr const char* MEMORY_BACKEND_VERSION =
        "StormMemory-2026-09-12-READINIT-V3";

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
            "[PID] processo selecionado: %d",
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
    pid_t pid
)
{
    if (pid <= 0)
        return false;

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
            "readlink(%s) falhou: %s",
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
            "Target ELF = 32-bit"
        );

        return true;
    }

    if (ident[4] == 2)
    {
        LOGI(
            "Target ELF = 64-bit"
        );

        return false;
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
        LOGE(
            "Nao abriu %s: %s",
            path,
            strerror(errno)
        );

        s_ProcMemFd = -1;

        return true;
    }

    LOGI(
        "/proc/%d/mem aberto",
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

    if (
        ReadProcessVm(
            address,
            outValue,
            size
        )
    )
    {
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
        return true;
    }

    return false;
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

    if (
        WriteProcessVm(
            address,
            value,
            size
        )
    )
    {
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
        return true;
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

bool Memory::Initialize()
{
    LOGI(
        "[INIT] ========================================"
    );

    LOGI(
        "[INIT] Memory::Initialize()"
    );

    LOGI(
        "[INIT] Backend=%s",
        MEMORY_BACKEND_VERSION
    );

    LOGI(
        "[INIT] Procurando processo do Free Fire..."
    );

    if (s_Initialized)
    {
        LOGI(
            "[INIT] Memory ja inicializada: pid=%d lib=0x%lX",
            s_TargetPid,
            static_cast<unsigned long>(
                s_LibIl2Cpp
            )
        );

        return true;
    }

    pid_t pid =
        FindTargetPid();

    if (pid <= 0)
    {
        LOGE(
            "[INIT] FALHA: PID nao encontrado"
        );

        return false;
    }

    LOGI(
        "[INIT] PID encontrado: %d",
        pid
    );

    bool target32 =
        DetectTarget32Bit(
            pid
        );

    LOGI(
        "[INIT] Arquitetura alvo: %s",
        target32
            ? "32-bit / armeabi-v7a"
            : "64-bit / arm64-v8a"
    );

    uintptr_t il2cpp =
        FindModuleBase(
            pid,
            "libil2cpp.so"
        );

    if (il2cpp == 0)
    {
        LOGE(
            "[INIT] FALHA: libil2cpp.so nao encontrada no PID %d",
            pid
        );

        return false;
    }

    LOGI(
        "[INIT] libil2cpp.so = 0x%lX",
        static_cast<unsigned long>(
            il2cpp
        )
    );

    /*
     * Configura o estado minimo necessario para que
     * g_FreeFireMemory.Read() possa funcionar durante
     * Offsets::GameConfig().
     */
    s_TargetPid = pid;
    s_Target32Bit = target32;
    s_LibIl2Cpp = il2cpp;

    libAddress = il2cpp;

    bool opened =
        OpenProcessMemory(
            pid
        );

    LOGI(
        "[INIT] OpenProcessMemory = %s",
        opened
            ? "OK"
            : "FALHA"
    );

    if (!opened)
    {
        LOGE(
            "[INIT] FALHA abrindo memoria do processo"
        );

        Shutdown();

        return false;
    }

    Offsets::LibIl2Cpp =
        il2cpp;

    Offsets::LibIl2CppCandidates.clear();

    Offsets::LibIl2CppCandidates.push_back(
        il2cpp
    );

    g_Globals.General.N32 =
        s_Target32Bit;

    LOGI(
        "[INIT] General.N32 = %d",
        g_Globals.General.N32
            ? 1
            : 0
    );

    LOGI(
        "[INIT] Offsets::LibIl2Cpp = 0x%lX",
        static_cast<unsigned long>(
            Offsets::LibIl2Cpp
        )
    );

    LOGI(
        "[INIT] LibIl2CppCandidates.size() = %zu",
        Offsets::LibIl2CppCandidates.size()
    );

    LOGI(
        "[INIT] Chamando Offsets::GameConfig()..."
    );

    Offsets::GameConfig();

LOGI("[INIT] GameConfig terminou");
LOGI("[INIT] LibIl2Cpp = 0x%lX",
     static_cast<unsigned long>(Offsets::LibIl2Cpp));

if (Offsets::LibIl2Cpp == 0) {
    LOGI("[INIT] Nenhuma configuracao de offsets foi identificada.");
    LOGI("[INIT] Continuando mesmo assim: a base de libil2cpp permanece valida.");
    
    Offsets::LibIl2Cpp = libAddress;
}

if (Offsets::LibIl2Cpp == 0) {
    LOGE("[INIT] LibIl2Cpp continua invalida apos fallback");
    return false;
}

LOGI("[INIT] LibIl2Cpp final = 0x%lX",
     static_cast<unsigned long>(Offsets::LibIl2Cpp));

    LOGI(
        "[INIT] Teste de offsets principais:"
    );

    LOGI(
        "[INIT] GameFacade.TypeInfo = 0x%lX",
        static_cast<unsigned long>(
            Offsets::GameFacade::GameFacade_TypeInfo
        )
    );

    LOGI(
        "[INIT] GameFacade.CurrentMatchGame = 0x%lX",
        static_cast<unsigned long>(
            Offsets::GameFacade::CurrentMatchGame
        )
    );

    LOGI(
        "[INIT] MatchGame.m_Match = 0x%lX",
        static_cast<unsigned long>(
            Offsets::MatchGame::m_Match
        )
    );

    LOGI(
        "[INIT] Match.m_LocalPlayer = 0x%lX",
        static_cast<unsigned long>(
            Offsets::Match::m_LocalPlayer
        )
    );

    LOGI(
        "[INIT] Match.m_AttackableEntities = 0x%lX",
        static_cast<unsigned long>(
            Offsets::Match::m_AttackableEntities
        )
    );

    s_Initialized = true;

    LOGI(
        "[INIT] Memory::Initialize() = SUCCESS"
    );

    LOGI(
        "[INIT] ========================================"
    );

    return true;
}

bool Memory::Restart()
{
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
    if (s_RestartInProgress)
        return false;

    s_RestartInProgress = true;

    std::thread(
        []
        {
            Memory::Restart();

            s_RestartInProgress =
                false;
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

    Shutdown();

    s_TargetPid =
        currentPid;

    s_Target32Bit =
        DetectTarget32Bit(
            currentPid
        );

    s_LibIl2Cpp =
        currentLib;

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

    Offsets::LibIl2CppCandidates.push_back(
        currentLib
    );

    g_Globals.General.N32 =
        s_Target32Bit;

    Offsets::GameConfig();

    if (
        Offsets::LibIl2Cpp == 0
    )
    {
        LOGE(
            "RefreshCR3: GameConfig falhou"
        );

        Shutdown();

        return false;
    }

    s_Initialized = true;

    LOGI(
        "Target atualizado: PID=%d lib=0x%lX",
        currentPid,
        static_cast<unsigned long>(
            currentLib
        )
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