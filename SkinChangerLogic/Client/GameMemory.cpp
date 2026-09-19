#include "GameMemory.hpp"

#include <IMPLEMENTACAO/includes.hpp>
#include "XorStr.hpp"

namespace
{
    bool g_Attached = false;
    uint64_t g_Il2Cpp = 0;
    uint16_t g_Machine = 0;
    std::string g_Status = "Not attached";

    // ELF e_machine
    constexpr uint16_t kEM_386 = 3;       // legacy
    constexpr uint16_t kEM_ARM = 40;      // armeabi-v7a
    constexpr uint16_t kEM_X86_64 = 62;
    constexpr uint16_t kEM_AARCH64 = 183; // arm64-v8a

    static const char* ArchName(uint16_t m)
    {
        switch (m)
        {
        case kEM_386:     return "x86";
        case kEM_ARM:     return "armeabi-v7a";
        case kEM_X86_64:  return "x86_64";
        case kEM_AARCH64: return "arm64-v8a";
        default:          return "unknown";
        }
    }

    static bool IsSupported32(uint16_t m)
    {
        return m == kEM_ARM || m == kEM_386;
    }
}

namespace GameMemory
{
    bool Attach(bool verbose)
    {
        g_Attached = false;
        g_Il2Cpp = 0;
        g_Machine = 0;
        g_Status = "Attaching to HD-Player...";

        const char* packages[] = {
            "com.dts.freefireth",
            "com.dts.freefiremax",
        };

        bool setupOk = false;
        for (const char* pkg : packages)
        {
            g_Status = std::string("Setup ") + pkg + "...";
            if (bluestacks->setup(pkg, verbose))
            {
                setupOk = true;
                break;
            }
            bluestacks->destroy();
        }

        if (!setupOk)
        {
            g_Status = "HD-Player / FF package not found";
            return false;
        }

        g_Status = "Resolving libil2cpp.so (v7a)...";
        const std::string mod = XorStrS("libil2cpp.so");

        uint64_t bestBase = 0;
        uint16_t bestMachine = 0;

        // Prefer armeabi-v7a (40); try several candidates
        for (usize cand = 1; cand <= 6; ++cand)
        {
            const uint64_t base = bluestacks->get_module_base(mod, 1, verbose, cand);
            if (base == 0)
                continue;

            uint16_t machine = 0;
            // Fresh read from ELF when possible
            uint32_t magic = 0;
            if (memory->read(base, magic) && magic == 0x464C457Fu)
                memory->read(base + 0x12, machine);

            if (machine == 0)
                machine = bluestacks->get_module_machine_type(mod);

            // Strong preference: v7a
            if (machine == kEM_ARM)
            {
                bestBase = base;
                bestMachine = machine;
                break;
            }

            // Keep first readable 32-bit as fallback
            if (bestBase == 0 && IsSupported32(machine))
            {
                bestBase = base;
                bestMachine = machine;
            }

            // Keep any base as last resort
            if (bestBase == 0)
            {
                bestBase = base;
                bestMachine = machine;
            }
        }

        if (bestBase == 0)
        {
            g_Status = "libil2cpp.so not found";
            bluestacks->destroy();
            return false;
        }

        g_Il2Cpp = bestBase;
        g_Machine = bestMachine;
        g_Attached = true;

        if (IsSupported32(bestMachine))
            g_Status = std::string("Attached (") + ArchName(bestMachine) + ")";
        else
            g_Status = std::string("Attached (") + ArchName(bestMachine)
                + ") — need v7a 32-bit, cloth may fail";

        return true;
    }

    void Detach()
    {
        bluestacks->destroy();
        g_Attached = false;
        g_Il2Cpp = 0;
        g_Machine = 0;
        g_Status = "Detached";
    }

    bool IsAttached()
    {
        return g_Attached && g_Il2Cpp != 0 && memory->is_valid();
    }

    uint64_t Il2CppBase() { return g_Il2Cpp; }
    uint16_t MachineType() { return g_Machine; }
    bool IsPtr32() { return IsSupported32(g_Machine); }

    const std::string& Status() { return g_Status; }

    bool ReadU32(uint64_t address, uint32_t& out)
    {
        if (!IsAttached() || address == 0)
            return false;
        return memory->read(address, out);
    }

    bool ReadI32(uint64_t address, int32_t& out)
    {
        if (!IsAttached() || address == 0)
            return false;
        return memory->read(address, out);
    }

    bool ReadU8(uint64_t address, uint8_t& out)
    {
        if (!IsAttached() || address == 0)
            return false;
        return memory->read(address, out);
    }

    bool WriteU32(uint64_t address, uint32_t value)
    {
        if (!IsAttached() || address == 0)
            return false;
        return memory->write(address, value);
    }

    bool WriteI32(uint64_t address, int32_t value)
    {
        if (!IsAttached() || address == 0)
            return false;
        return memory->write(address, value);
    }

    bool WriteU8(uint64_t address, uint8_t value)
    {
        if (!IsAttached() || address == 0)
            return false;
        return memory->write(address, value);
    }

    bool ReadBytes(uint64_t address, void* buffer, size_t size)
    {
        if (!IsAttached() || address == 0 || !buffer || size == 0)
            return false;
        return memory->read(address, buffer, size);
    }

    bool WriteBytes(uint64_t address, const void* buffer, size_t size)
    {
        if (!IsAttached() || address == 0 || !buffer || size == 0)
            return false;
        return memory->write(address, buffer, size);
    }
}
