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

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "StormMemory", __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "StormMemory", __VA_ARGS__)

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
    static bool IsNumeric(const char* text)
    {
        if (!text || !*text)
            return false;

        for (const char* p = text; *p; ++p)
        {
            if (!std::isdigit(static_cast<unsigned char>(*p)))
                return false;
        }

        return true;
    }

    static std::string ReadCmdline(pid_t pid)
    {
        char path[64];
        std::snprintf(path, sizeof(path), "/proc/%d/cmdline", pid);

        int fd = ::open(path, O_RDONLY | O_CLOEXEC);
        if (fd < 0)
            return {};

        char buffer[512]{};
        ssize_t n = ::read(fd, buffer, sizeof(buffer) - 1);
        ::close(fd);

        if (n <= 0)
            return {};

        buffer[n] = '\0';

        for (ssize_t i = 0; i < n; ++i)
        {
            if (buffer[i] == '\0')
                buffer[i] = ' ';
        }

        return std::string(buffer, static_cast<size_t>(n));
    }

    static bool ContainsIgnoreCase(const std::string& value, const char* needle)
    {
        if (!needle || !*needle)
            return false;

        std::string a = value;
        std::string b = needle;

        std::transform(a.begin(), a.end(), a.begin(),
                       [](unsigned char c)
                       {
                           return static_cast<char>(std::tolower(c));
                       });

        std::transform(b.begin(), b.end(), b.begin(),
                       [](unsigned char c)
                       {
                           return static_cast<char>(std::tolower(c));
                       });

        return a.find(b) != std::string::npos;
    }

    static std::vector<std::string> GetTargetNames()
    {
        std::vector<std::string> names;

        const char* envPackage = std::getenv("STORM_TARGET_PACKAGE");
        if (envPackage && *envPackage)
            names.emplace_back(envPackage);

        /*
         * Free Fire normal.
         */
        names.emplace_back("com.dts.freefireth");

        /*
         * Free Fire MAX.
         */
        names.emplace_back("com.dts.freefiremax");

        return names;
    }
}

pid_t Memory::FindTargetPid()
{
    const std::vector<std::string> names = GetTargetNames();

    DIR* proc = ::opendir("/proc");
    if (!proc)
    {
        LOGE("Nao foi possivel abrir /proc: %s", strerror(errno));
        return -1;
    }

    pid_t result = -1;

    while (dirent* entry = ::readdir(proc))
    {
        if (!IsNumeric(entry->d_name))
            continue;

        pid_t pid = static_cast<pid_t>(std::strtol(entry->d_name, nullptr, 10));
        if (pid <= 0)
            continue;

        std::string cmdline = ReadCmdline(pid);
        if (cmdline.empty())
            continue;

        for (const std::string& target : names)
        {
            if (ContainsIgnoreCase(cmdline, target.c_str()))
            {
                result = pid;
                break;
            }
        }

        if (result > 0)
            break;
    }

    ::closedir(proc);

    if (result > 0)
    {
        LOGI("PID encontrado: %d", result);
    }
    else
    {
        LOGE("Nenhum processo alvo encontrado");
    }

    return result;
}

uintptr_t Memory::FindModuleBase(pid_t pid, const char* moduleName)
{
    if (pid <= 0 || !moduleName || !*moduleName)
        return 0;

    char path[64];
    std::snprintf(path, sizeof(path), "/proc/%d/maps", pid);

    FILE* file = std::fopen(path, "r");
    if (!file)
    {
        LOGE("Falha abrindo %s: %s", path, strerror(errno));
        return 0;
    }

    char line[4096];

    while (std::fgets(line, sizeof(line), file))
    {
        unsigned long long start = 0;
        unsigned long long end = 0;
        unsigned long long offset = 0;
        char perms[8]{};
        char mappedPath[PATH_MAX]{};

        int parsed = std::sscanf(
            line,
            "%llx-%llx %7s %llx %*s %*s %4095[^\n]",
            &start,
            &end,
            perms,
            &offset,
            mappedPath
        );

        if (parsed < 5)
            continue;

        if (offset != 0)
            continue;

        if (std::strstr(mappedPath, moduleName) == nullptr)
            continue;

        std::fclose(file);

        LOGI(
            "Modulo %s encontrado: 0x%llX",
            moduleName,
            start
        );

        return static_cast<uintptr_t>(start);
    }

    std::fclose(file);

    LOGE(
        "Modulo %s nao encontrado no PID %d",
        moduleName,
        pid
    );

    return 0;
}

bool Memory::DetectTarget32Bit(pid_t pid)
{
    if (pid <= 0)
        return false;

    char path[64];
    std::snprintf(path, sizeof(path), "/proc/%d/exe", pid);

    char exePath[PATH_MAX]{};

    ssize_t len = ::readlink(
        path,
        exePath,
        sizeof(exePath) - 1
    );

    if (len <= 0)
    {
        LOGE("readlink(%s) falhou: %s", path, strerror(errno));
        return false;
    }

    exePath[len] = '\0';

    int fd = ::open(exePath, O_RDONLY | O_CLOEXEC);
    if (fd < 0)
    {
        LOGE("Nao foi possivel abrir ELF do processo: %s", strerror(errno));
        return false;
    }

    unsigned char ident[5]{};

    ssize_t readBytes = ::pread(
        fd,
        ident,
        sizeof(ident),
        0
    );

    ::close(fd);

    if (readBytes != sizeof(ident))
    {
        LOGE("Falha lendo ELF");
        return false;
    }

    if (ident[0] != 0x7F ||
        ident[1] != 'E' ||
        ident[2] != 'L' ||
        ident[3] != 'F')
    {
        LOGE("ELF invalido");
        return false;
    }

    /*
     * ELFCLASS32 = 1
     * ELFCLASS64 = 2
     */
    if (ident[4] == 1)
    {
        LOGI("Target ELF = 32-bit");
        return true;
    }

    if (ident[4] == 2)
    {
        LOGI("Target ELF = 64-bit");
        return false;
    }

    LOGE("ELF class desconhecida: %u", ident[4]);
    return false;
}

bool Memory::OpenProcessMemory(pid_t pid)
{
    CloseProcessMemory();

    if (pid <= 0)
        return false;

    char path[64];
    std::snprintf(path, sizeof(path), "/proc/%d/mem", pid);

    /*
     * Nao fazemos deste FD uma dependencia obrigatoria.
     * process_vm_* sera tentado primeiro.
     */
    s_ProcMemFd = ::open(
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

        /*
         * process_vm_readv/process_vm_writev ainda podem funcionar.
         */
        s_ProcMemFd = -1;
        return true;
    }

    LOGI("/proc/%d/mem aberto", pid);
    return true;
}

void Memory::CloseProcessMemory()
{
    if (s_ProcMemFd >= 0)
    {
        ::close(s_ProcMemFd);
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

    if (s_TargetPid <= 0 || !buffer || size == 0)
        return false;

    iovec local{};
    local.iov_base = buffer;
    local.iov_len = size;

    iovec remote{};
    remote.iov_base = reinterpret_cast<void*>(address);
    remote.iov_len = size;

    errno = 0;

    ssize_t result = static_cast<ssize_t>(
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

    if (result == static_cast<ssize_t>(size))
        return true;

    return false;

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

    if (s_TargetPid <= 0 || !buffer || size == 0)
        return false;

    iovec local{};
    local.iov_base = const_cast<void*>(buffer);
    local.iov_len = size;

    iovec remote{};
    remote.iov_base = reinterpret_cast<void*>(address);
    remote.iov_len = size;

    errno = 0;

    ssize_t result = static_cast<ssize_t>(
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

    if (result == static_cast<ssize_t>(size))
        return true;

    return false;

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
    if (fd < 0 || !buffer || size == 0)
        return false;

    size_t total = 0;

    while (total < size)
    {
        ssize_t n = ::pread64(
            fd,
            static_cast<char*>(buffer) + total,
            size - total,
            static_cast<off64_t>(address + total)
        );

        if (n <= 0)
            return false;

        total += static_cast<size_t>(n);
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
    if (fd < 0 || !buffer || size == 0)
        return false;

    size_t total = 0;

    while (total < size)
    {
        ssize_t n = ::pwrite64(
            fd,
            static_cast<const char*>(buffer) + total,
            size - total,
            static_cast<off64_t>(address + total)
        );

        if (n <= 0)
            return false;

        total += static_cast<size_t>(n);
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
    if (!s_Initialized ||
        s_TargetPid <= 0 ||
        address == 0 ||
        !outValue ||
        size == 0)
    {
        return false;
    }

    /*
     * Primeiro método:
     * process_vm_readv
     */
    if (ReadProcessVm(address, outValue, size))
        return true;

    /*
     * Fallback:
     * /proc/<pid>/mem
     */
    if (ReadProcMem(address, outValue, size))
        return true;

    return false;
}

bool Memory::Write(
    uintptr_t address,
    const void* value,
    size_t size
)
{
    if (!s_Initialized ||
        s_TargetPid <= 0 ||
        address == 0 ||
        !value ||
        size == 0)
    {
        return false;
    }

    /*
     * Primeiro método:
     * process_vm_writev
     */
    if (WriteProcessVm(address, value, size))
        return true;

    /*
     * Fallback:
     * /proc/<pid>/mem
     */
    if (WriteProcMem(address, value, size))
        return true;

    return false;
}

std::string Memory::String(
    uintptr_t address,
    int maxLength
)
{
    if (address == 0 || maxLength <= 0)
        return {};

    if (maxLength > 256)
        maxLength = 256;

    /*
     * IL2CPP string:
     *
     * 32-bit:
     * +0x08 = length
     * +0x0C = UTF-16 chars
     *
     * 64-bit:
     * +0x10 = length
     * +0x14 = UTF-16 chars
     */
    const uintptr_t lengthAddress =
        s_Target32Bit ? address + 0x8 : address + 0x10;

    const uintptr_t charsAddress =
        s_Target32Bit ? address + 0xC : address + 0x14;

    int32_t length = 0;

    if (!Read(lengthAddress, length))
        return {};

    if (length <= 0 || length > maxLength)
        return {};

    std::vector<uint16_t> chars(
        static_cast<size_t>(length) + 1
    );

    if (!Read(
            charsAddress,
            chars.data(),
            static_cast<size_t>(length) * sizeof(uint16_t)))
    {
        return {};
    }

    std::string result;
    result.reserve(static_cast<size_t>(length));

    for (int i = 0; i < length; ++i)
    {
        uint16_t c = chars[static_cast<size_t>(i)];

        if (c == 0)
            break;

        if (c < 0x80)
        {
            result.push_back(static_cast<char>(c));
        }
        else if (c < 0x800)
        {
            result.push_back(
                static_cast<char>(0xC0 | (c >> 6))
            );

            result.push_back(
                static_cast<char>(0x80 | (c & 0x3F))
            );
        }
        else
        {
            result.push_back(
                static_cast<char>(0xE0 | (c >> 12))
            );

            result.push_back(
                static_cast<char>(0x80 | ((c >> 6) & 0x3F))
            );

            result.push_back(
                static_cast<char>(0x80 | (c & 0x3F))
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

std::vector<uintptr_t> Memory::GetModuleAddress(
    bool N32
)
{
    (void)N32;

    std::vector<uintptr_t> result;

    uintptr_t base = GetModuleAddress(
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
    if (s_Initialized)
        return true;

    pid_t pid = FindTargetPid();

    if (pid <= 0)
    {
        LOGE("Initialize: PID nao encontrado");
        return false;
    }

    bool target32 = DetectTarget32Bit(pid);

    uintptr_t il2cpp = FindModuleBase(
        pid,
        "libil2cpp.so"
    );

    if (il2cpp == 0)
    {
        LOGE("Initialize: libil2cpp.so nao encontrada");
        return false;
    }

    s_TargetPid = pid;
    s_Target32Bit = target32;
    s_LibIl2Cpp = il2cpp;

    libAddress = il2cpp;

    OpenProcessMemory(pid);

    Offsets::LibIl2Cpp = il2cpp;
    Offsets::LibIl2CppCandidates.clear();
    Offsets::LibIl2CppCandidates.push_back(il2cpp);

    /*
     * Para o FF v7a, isso seleciona os offsets
     * FFTHV7A75/76 existentes no projeto.
     */
    Offsets::GameConfig();

    if (Offsets::LibIl2Cpp == 0)
        Offsets::LibIl2Cpp = il2cpp;

    g_Globals.General.N32 = s_Target32Bit;

    LOGI(
        "Memory OK: pid=%d abi=%s libil2cpp=0x%lX",
        pid,
        s_Target32Bit ? "32-bit" : "64-bit",
        static_cast<unsigned long>(s_LibIl2Cpp)
    );

    s_Initialized = true;
    return true;
}

bool Memory::Restart()
{
    Shutdown();

    std::this_thread::sleep_for(
        std::chrono::milliseconds(100)
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
            s_RestartInProgress = false;
        }
    ).detach();

    return true;
}

bool Memory::RefreshCR3()
{
    /*
     * O backend Android nao utiliza CR3/VirtualBox.
     *
     * Revalidamos PID + libil2cpp pelo /proc.
     */
    pid_t currentPid = FindTargetPid();

    if (currentPid <= 0)
        return false;

    uintptr_t currentLib = FindModuleBase(
        currentPid,
        "libil2cpp.so"
    );

    if (currentLib == 0)
        return false;

    bool changed =
        currentPid != s_TargetPid ||
        currentLib != s_LibIl2Cpp;

    if (changed)
    {
        Shutdown();

        s_TargetPid = currentPid;
        s_Target32Bit = DetectTarget32Bit(currentPid);
        s_LibIl2Cpp = currentLib;

        libAddress = currentLib;

        OpenProcessMemory(currentPid);

        Offsets::LibIl2Cpp = currentLib;
        Offsets::LibIl2CppCandidates.clear();
        Offsets::LibIl2CppCandidates.push_back(currentLib);

        Offsets::GameConfig();

        if (Offsets::LibIl2Cpp == 0)
            Offsets::LibIl2Cpp = currentLib;

        g_Globals.General.N32 = s_Target32Bit;

        s_Initialized = true;

        LOGI(
            "Target atualizado: PID=%d lib=0x%lX",
            currentPid,
            static_cast<unsigned long>(currentLib)
        );
    }

    return changed;
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
     * Sem TLB de page-table nesse backend.
     */
}

void Memory::FlushAllTLB()
{
    /*
     * Sem TLB de page-table nesse backend.
     */
}

bool Memory::TranslateVA(
    uintptr_t guestVA,
    uintptr_t& physicalOut
)
{
    /*
     * O novo backend trabalha diretamente com VA
     * do processo alvo.
     */
    physicalOut = guestVA;
    return guestVA != 0;
}