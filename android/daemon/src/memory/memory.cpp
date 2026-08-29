#include "memory.hpp"
#include <android/log.h>
#include <string>
#include <vector>
#include <thread>
#include <chrono>
#include <cstring>
#include <cerrno>
#include <unistd.h>
#include <sys/ptrace.h>
#include <sys/wait.h>
#include <dirent.h>
#include <fstream>
#include <sstream>

#define LOG_TAG "ZmInternal-Memory"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__)

pid_t Memory::s_targetPid = 0;

bool Memory::Initialize() {
    LOGI("Memory::Initialize() - Placeholder for Android memory initialization");
    // In a real implementation, we would:
    // 1. Set up any necessary permissions
    // 2. Verify we can access the target process
    // For now, just return true as a placeholder
    return true;
}

bool Memory::Restart() {
    LOGI("Memory::Restart() - Placeholder");
    return true;
}

bool Memory::RestartAsync() {
    LOGI("Memory::RestartAsync() - Placeholder");
    return true;
}

bool Memory::RefreshCR3() {
    LOGI("Memory::RefreshCR3() - Placeholder (not needed on Android)");
    // On Android, we don't need to refresh CR3 as we're using process_vm_readv
    // which works with virtual addresses directly
    return false; // Indicate no change
}

void Memory::FlushTLB() {
    LOGI("Memory::FlushTLB() - Placeholder (no-op on Android)");
}

void Memory::FlushAllTLB() {
    LOGI("Memory::FlushAllTLB() - Placeholder (no-op on Android)");
}

bool Memory::ReadBuffer(uintptr_t address, void* buffer, size_t size) {
    if (!buffer || size == 0 || s_targetPid == 0) {
        return false;
    }

    // Try process_vm_readv first (preferred method)
    struct iovec local = { buffer, size };
    struct iovec remote = { reinterpret_cast<void*>(address), size };

    ssize_t result = process_vm_readv(s_targetPid, &local, 1, &remote, 1, 0);

    if (result == static_cast<ssize_t>(size)) {
        return true;
    }

    // Fallback to ptrace if process_vm_readv failed
    LOGW("process_vm_readv failed (%zd, errno=%d), falling back to ptrace", result, errno);

    for (size_t i = 0; i < size; i += sizeof(uintptr_t)) {
        size_t chunkSize = std::min(sizeof(uintptr_t), size - i);
        uintptr_t value = ptrace(PTRACE_PEEKDATA, s_targetPid,
                                reinterpret_cast<void*>(address + i), nullptr);

        if (errno != 0) {
            LOGE("ptrace(PTRACE_PEEKDATA) failed at address %p, errno=%d",
                 reinterpret_cast<void*>(address + i), errno);
            return false;
        }

        memcpy(static_cast<uint8_t*>(buffer) + i, &value, chunkSize);
    }

    return true;
}

bool Memory::WriteBuffer(uintptr_t address, const void* buffer, size_t size) {
    if (!buffer || size == 0 || s_targetPid == 0) {
        return false;
    }

    // Try process_vm_writev first (preferred method)
    struct iovec local = { const_cast<void*>(buffer), size };
    struct iovec remote = { reinterpret_cast<void*>(address), size };

    ssize_t result = process_vm_writev(s_targetPid, &local, 1, &remote, 1, 0);

    if (result == static_cast<ssize_t>(size)) {
        return true;
    }

    // Fallback to ptrace if process_vm_writev failed
    LOGW("process_vm_writev failed (%zd, errno=%d), falling back to ptrace", result, errno);

    for (size_t i = 0; i < size; i += sizeof(uintptr_t)) {
        size_t chunkSize = std::min(sizeof(uintptr_t), size - i);
        uintptr_t value;
        memcpy(&value, static_cast<const uint8_t*>(buffer) + i, chunkSize);

        if (ptrace(PTRACE_POKEDATA, s_targetPid,
                  reinterpret_cast<void*>(address + i),
                  reinterpret_cast<void*>(value)) == -1) {
            LOGE("ptrace(PTRACE_POKEDATA) failed at address %p, errno=%d",
                 reinterpret_cast<void*>(address + i), errno);
            return false;
        }
    }

    return true;
}

bool Memory::Read(uintptr_t Address, void* OutValue, size_t Size) {
    return ReadBuffer(Address, OutValue, Size);
}

std::string Memory::String(uintptr_t Address, int MaxLength) {
    if (!Address || MaxLength <= 0 || s_targetPid == 0) {
        return "";
    }

    std::string result;
    result.reserve(MaxLength);
    const int BLOCK_SIZE = 128;
    uintptr_t currentAddress = Address;

    for (int remaining = MaxLength; remaining > 0; ) {
        int toRead = std::min(BLOCK_SIZE, remaining);
        std::vector<char> buffer(toRead);

        if (!ReadBuffer(currentAddress, buffer.data(), toRead)) {
            break;
        }

        for (int i = 0; i < toRead; i++) {
            if (buffer[i] == '\0') {
                return result;
            }
            result.push_back(buffer[i]);
        }

        remaining -= toRead;
        currentAddress += toRead;
    }

    return result;
}

// Setter for target PID (would be called from daemon main)
void Memory::SetTargetPid(pid_t pid) {
    s_targetPid = pid;
    LOGI("Memory::SetTargetPid() - Set target PID to %d", pid);
}