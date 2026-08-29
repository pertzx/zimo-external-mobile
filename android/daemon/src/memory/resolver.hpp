#pragma once

#include <cstdint>
#include <string>

namespace ZmInternal {
    namespace Daemon {
        namespace Memory {

            class Resolver {
            public:
                Resolver();
                ~Resolver();

                // Initialize resolver
                bool Initialize();

                // Find the PID of the target process (Free Fire)
                bool FindTargetProcess(uintptr_t& pid);

                // Find the base address of libil2cpp.so in the target process
                bool FindIl2cppBase(uintptr_t pid, uintptr_t& baseAddress);

                // Get cached values
                uintptr_t GetTargetPid() const { return m_targetPid; }
                uintptr_t GetIl2cppBase() const { return m_il2cppBase; }

            private:
                uintptr_t m_targetPid = 0;
                uintptr_t m_il2cppBase = 0;
                bool m_initialized = false;
            };

        } // namespace Memory
    } // namespace Daemon
} // namespace ZmInternal