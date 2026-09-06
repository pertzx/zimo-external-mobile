#include "EmulatorEnv.hpp"
#include <android/log.h>
#include <string>

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "StormEmulator", __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "StormEmulator", __VA_ARGS__)

// Static members are already initialized inline in the header
// No separate definitions needed here

void EmulatorEnvironment::DetectEmulator() {
    LOGI("[EmulatorEnv] Android: Using default emulator detection");
    emulator_ = EmulatorType::BlueStacks5;  // Default for Android
}

void EmulatorEnvironment::QueryVersion() {
    LOGI("[EmulatorEnv] Android: Using default version");
    version_ = "1.0.0";
}

void EmulatorEnvironment::DetectABI() {
    #if defined(__aarch64__)
    abi_ = ABIType::X86_64;
    #elif defined(__arm__)
    abi_ = ABIType::X86;
    #else
    abi_ = ABIType::Unknown;
    #endif
    LOGI("[EmulatorEnv] Detected ABI: %d", static_cast<int>(abi_));
}

void EmulatorEnvironment::Refresh() {
    DetectEmulator();
    QueryVersion();
    DetectABI();
}

std::string EmulatorEnvironment::EmulatorName() const {
    switch (emulator_) {
        case EmulatorType::BlueStacks4: return "BlueStacks 4";
        case EmulatorType::BlueStacks5: return "BlueStacks 5";
        case EmulatorType::BlueStacks5Beta: return "BlueStacks 5 Beta";
        case EmulatorType::Bluestacks5China: return "BlueStacks 5 China";
        case EmulatorType::Msi4: return "MSI App Player 4";
        case EmulatorType::Msi5: return "MSI App Player 5";
        default: return "Unknown";
    }
}

const char* EmulatorEnvironment::ABIToString(ABIType abi) {
    switch (abi) {
        case ABIType::X86: return "x86";
        case ABIType::X86_64: return "x86_64";
        default: return "Unknown";
    }
}

EmulatorEnvironment& GetEmulatorEnv() {
    static EmulatorEnvironment env;
    return env;
}

bool KernelOffsetSelector::ApplyInitTaskOffsets(const EmulatorEnvironment& env, uint32_t& kva_init_task_32, uint64_t& kva_init_task_64) {
    // Android defaults
    kva_init_task_32 = 0xC1C32400;
    kva_init_task_64 = 0xFFFF000000000000;
    return true;
}