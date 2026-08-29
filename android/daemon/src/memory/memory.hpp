#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include <sys/uio.h>  // for process_vm_readv
#include <unistd.h>   // for ptrace
#include <cerrno>
#include <cstring>

class Memory {
public:
    static bool Initialize();
    static bool Restart();
    static bool RestartAsync();

    // Revalida o CR3 do processo do jogo (no Android, vamos apenas verificar se o processo ainda está vivo)
    static bool RefreshCR3();

    // Flush thread-local TLB (no-op no Android, mantido para compatibilidade)
    static void FlushTLB();
    // Flush global — invalida TODAS as threads (no-op no Android)
    static void FlushAllTLB();

    static bool Read(uintptr_t Address, void* OutValue, size_t Size);
    template<typename T>
    static bool Read(uintptr_t Address, T& OutValue) {
        return ReadBuffer(Address, &OutValue, sizeof(T));
    }
    template<typename T>
    static T Read(uintptr_t Address) {
        T Value{};
        ReadBuffer(Address, &Value, sizeof(T));
        return Value;
    }
    template<typename T>
    static bool Write(uintptr_t Address, const T& Value) {
        return WriteBuffer(Address, &Value, sizeof(T));
    }
    static std::string String(uintptr_t Address, int MaxLength = 32);

    // Funções de tradução de endereço (no Android, vamos usar endereços virtuais diretamente)
    static bool TranslateVA(uintptr_t guestVA, uintptr_t& physOut) {
        // No Android, estamos no mesmo espaço de endereçamento do kernel, mas o guestVA é o endereço virtual do processo do jogo.
        // Vamos simplesmente usar o guestVA como endereço virtual e confiar no process_vm_readv para traduzi-lo.
        physOut = guestVA;
        return true;
    }

    // GetVM não é necessário no Android, mantido para compatibilidade
    static void* GetVM() { return nullptr; }

private:
    static bool ReadBuffer(uintptr_t Address, void* Buffer, size_t Size);
    static bool WriteBuffer(uintptr_t Address, const void* Buffer, size_t Size);

    // PID do processo do jogo (Free Fire)
    static pid_t s_targetPid;
};

// Variável estática para armazenar o PID do processo do jogo
extern pid_t g_targetPid;