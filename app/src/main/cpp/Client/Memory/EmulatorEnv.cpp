#include "EmulatorEnv.hpp"

#include <android/log.h>
#include <sys/system_properties.h>

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "StormEmulator", __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "StormEmulator", __VA_ARGS__)

void EmulatorEnvironment::DetectEmulator()
{
    /*
     * O projeto roda diretamente em Android.
     * Nao assumir BlueStacks.
     */
    emulator_ = EmulatorType::Unknown;
}

void EmulatorEnvironment::QueryVersion()
{
    char value[PROP_VALUE_MAX]{};

    if (__system_property_get("ro.build.version.release", value) > 0)
        version_ = value;
    else
        version_ = "Android";
}

void EmulatorEnvironment::DetectABI()
{
#if defined(__aarch64__)
    /*
     * Processo compilado ARM64.
     */
    abi_ = ABIType::ARM64;
#elif defined(__arm__)
    /*
     * Processo compilado ARM32 / armeabi-v7a.
     */
    abi_ = ABIType::ARM32;
#elif defined(__x86_64__)
    abi_ = ABIType::X86_64;
#elif defined(__i386__)
    abi_ = ABIType::X86;
#else
    abi_ = ABIType::Unknown;
#endif

    LOGI(
        "ABI compilada detectada: %s",
        ABIToString(abi_)
    );
}

void EmulatorEnvironment::Refresh()
{
    DetectEmulator();
    QueryVersion();
    DetectABI();
}

std::string EmulatorEnvironment::EmulatorName() const
{
    switch (emulator_)
    {
        case EmulatorType::BlueStacks4:
            return "BlueStacks 4";

        case EmulatorType::BlueStacks5:
            return "BlueStacks 5";

        case EmulatorType::BlueStacks5Beta:
            return "BlueStacks 5 Beta";

        case EmulatorType::Bluestacks5China:
            return "BlueStacks 5 China";

        case EmulatorType::Msi4:
            return "MSI App Player 4";

        case EmulatorType::Msi5:
            return "MSI App Player 5";

        default:
            return "Android";
    }
}

const char* EmulatorEnvironment::ABIToString(ABIType abi)
{
    switch (abi)
    {
        case ABIType::ARM32:
            return "armeabi-v7a";

        case ABIType::ARM64:
            return "arm64-v8a";

        case ABIType::X86:
            return "x86";

        case ABIType::X86_64:
            return "x86_64";

        default:
            return "unknown";
    }
}

EmulatorEnvironment& GetEmulatorEnv()
{
    static EmulatorEnvironment env;
    return env;
}

bool KernelOffsetSelector::ApplyInitTaskOffsets(
    const EmulatorEnvironment&,
    uint32_t& kva_init_task_32,
    uint64_t& kva_init_task_64
)
{
    /*
     * Nao usar offsets arbitrarios de init_task no backend
     * Android baseado em /proc/pid/mem/process_vm_*.
     */
    kva_init_task_32 = 0;
    kva_init_task_64 = 0;

    return true;
}