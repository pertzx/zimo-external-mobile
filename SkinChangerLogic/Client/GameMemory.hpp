#pragma once

#include <cstdint>
#include <string>

namespace GameMemory
{
    bool Attach(bool verbose = false);
    void Detach();
    bool IsAttached();
    uint64_t Il2CppBase();
    uint16_t MachineType();
    // 32-bit guest pointers (armeabi-v7a = 40, legacy x86 = 3)
    bool IsPtr32();
    const std::string& Status();

    bool ReadU32(uint64_t address, uint32_t& out);
    bool ReadI32(uint64_t address, int32_t& out);
    bool ReadU8(uint64_t address, uint8_t& out);
    bool WriteU32(uint64_t address, uint32_t value);
    bool WriteI32(uint64_t address, int32_t value);
    bool WriteU8(uint64_t address, uint8_t value);
    bool ReadBytes(uint64_t address, void* buffer, size_t size);
    bool WriteBytes(uint64_t address, const void* buffer, size_t size);
}
