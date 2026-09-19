#pragma once
#include <cstdint>
#include <src/Globals.hpp>
#include <EspLines/Memory/Memory.hpp>
#include <EspLines/Player.h>

namespace AimLockFunction {
    class AimLock {
    public:
        // Método estático simples
        static void Run();

    private:
        // Membros estáticos
        static Player* lastTarget;
    };
}