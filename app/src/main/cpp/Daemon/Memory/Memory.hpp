#pragma once

#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>
#include <sys/types.h>

class Memory
{
public:
    static bool Initialize();
    static bool Restart();
    static bool RestartAsync();
    static bool RefreshCR3();
    static void Shutdown();

    static void FlushTLB();
    static void FlushAllTLB();

    static bool Read(uintptr_t address, void* outValue, size_t size);
    static bool Write(uintptr_t address, const void* value, size_t size);

    template<typename T>
    static bool Read(uintptr_t address, T& outValue)
    {
        return Read(address, &outValue, sizeof(T));
    }

    template<typename T>
    static T Read(uintptr_t address)
    {
        T value{};
        Read(address, &value, sizeof(T));
        return value;
    }

    template<typename T>
    static bool Write(uintptr_t address, const T& value)
    {
        return Write(address, &value, sizeof(T));
    }

    static std::string String(uintptr_t address, int maxLength = 64);

    static uintptr_t GetModuleAddress(const char* moduleName);
    static std::vector<uintptr_t> GetModuleAddress(bool N32);

    static pid_t GetTargetPid();
    static bool IsTarget32Bit();
    static uintptr_t GetLibIl2Cpp();

    static bool TranslateVA(uintptr_t guestVA, uintptr_t& physicalOut);

    static void* GetVM()
    {
        return nullptr;
    }

private:
    static pid_t FindTargetPid();
    static uintptr_t FindModuleBase(pid_t pid, const char* moduleName, int index = 1);
    static bool DetectTarget32Bit(pid_t pid);

    static bool OpenProcessMemory(pid_t pid);
    static void CloseProcessMemory();

    static bool ReadProcessVm(uintptr_t address, void* buffer, size_t size);
    static bool WriteProcessVm(uintptr_t address, const void* buffer, size_t size);

    static bool ReadProcMem(uintptr_t address, void* buffer, size_t size);
    static bool WriteProcMem(uintptr_t address, const void* buffer, size_t size);

    static bool ReadExactFile(int fd, uintptr_t address, void* buffer, size_t size);
    static bool WriteExactFile(int fd, uintptr_t address, const void* buffer, size_t size);

    static pid_t s_TargetPid;
    static uintptr_t s_LibIl2Cpp;
    static bool s_Target32Bit;
    static int s_ProcMemFd;
    static bool s_Initialized;

    static volatile bool s_RestartInProgress;
};

extern Memory g_FreeFireMemory;

/*
 * Compatibilidade com código antigo que usa libAddress.
 * Não atribua esse valor manualmente.
 */
extern uintptr_t libAddress;