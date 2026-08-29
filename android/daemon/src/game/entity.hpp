#pragma once

#include <cstdint>
#include <chrono>

namespace ZmInternal {
    namespace Daemon {
        namespace Game {

            struct EntitySnapshot {
                uint64_t entityId;   // Unique entity ID
                uint32_t isActive;   // Whether entity is active/valid
                uint32_t teamId;     // Team ID (0 for enemy, 1+ for teammates)
                uint32_t health;     // Health points (0-100)
                uint32_t maxHealth;  // Maximum health
                float    posX;       // World position X
                float    posY;       // World position Y
                float    posZ;       // World position Z
                float    headX;      // Head position X (for aimbot)
                float    headY;      // Head position Y
                float    headZ;      // Head position Z
                uint32_t weaponId;   // Current weapon ID
                uint32_t isKnocked;  // Whether entity is knocked down
                uint32_t isLocalPlayer; // Whether this is the local player
                char     name[32];   // Player name (null-terminated)
                // Skeleton data (simplified - just key bones for ESP)
                float    neckX;      // Neck position X
                float    neckY;      // Neck position Y
                float    neckZ;      // Neck position Z
                float    hipX;       // Hip position X
                float    hipY;       // Hip position Y
                float    hipZ;       // Hip position Z
            };

        } // namespace Game
    } // namespace Daemon
}