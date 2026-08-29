#pragma once
#include <cstdint>
#include <string>
#include <vector>

// Magic number and version for IPC protocol
#define IPC_MAGIC 0x4950434D  // "IPCM" in ASCII
#define IPC_VERSION 1

// Socket path for Unix abstract socket
#define IPC_SOCKET_PATH "\\0zm_internal_ipc"

// Message types
enum IPC_MSG_TYPE : uint8_t {
    IPC_MSG_HEARTBEAT = 0,
    IPC_MSG_SNAPSHOT = 1,
    IPC_MSG_CONFIG = 2,
    IPC_MSG_COMMAND = 3,
    IPC_MSG_ACK = 4
};

// Header for all IPC messages
#pragma pack(push, 1)
struct IpcHeader {
    uint32_t magic;      // IPC_MAGIC
    uint8_t  version;    // IPC_VERSION
    uint8_t  type;       // IPC_MSG_TYPE
    uint32_t size;       // Size of payload (excluding header)
    uint32_t seq;        // Sequence number for matching requests/responses
    uint32_t checksum;   // Simple XOR checksum of payload
};

// Heartbeat message (keepalive)
#pragma pack(push, 1)
struct IpcMsgHeartbeat {
    uint64_t timestamp;  // Timestamp in milliseconds
};

// Entity snapshot data
#pragma pack(push, 1)
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

// Snapshot message (contains array of entities)
#pragma pack(push, 1)
struct IpcMsgSnapshot {
    uint32_t count;              // Number of entities in snapshot
    EntitySnapshot entities[1];  // Variable length array (actual size determined by count in header)
};

// Configuration message (client -> daemon)
#pragma pack(push, 1)
struct IpcMsgConfig {
    bool    enableFuncs;         // Master switch
    bool    aimbotEnabled;       // Aimbot toggle
    bool    silentAimEnabled;    // Silent aim toggle
    bool    espEnabled;          // ESP toggle
    bool    chamsEnabled;        // Chams toggle
    float   fov;                 // Field of view for aimbot
    float   smooth;              // Smoothing factor for aimbot
    float   maxDistance;         // Maximum distance for target selection
    bool    visibleCheck;        // Only target visible entities
    bool    ignoreKnocked;       // Ignore knocked enemies
    bool    ignoreBots;          // Ignore AI bots
    int     aimbotBone;          // Bone to aim for (0=head, 1=neck, etc.)
    int     silentAimBone;       // Bone for silent aim
    uint32_t exploits;           // Bitmask for exploit toggles
};

// Command message (one-shot commands)
#pragma pack(push, 1)
struct IpcMsgCommand {
    uint8_t commandId;           // Command identifier
    // Command-specific data would follow in a real implementation
};

// Acknowledgement message
#pragma pack(push, 1)
struct IpcMsgAck {
    uint32_t seq;                // Sequence number of message being acknowledged
    bool     success;            // Whether the command succeeded
};

#pragma pack(pop)

// Utility function to calculate simple XOR checksum
inline uint32_t calculateChecksum(const void* data, size_t size) {
    const uint8_t* bytes = static_cast<const uint8_t*>(data);
    uint32_t checksum = 0;
    for (size_t i = 0; i < size; ++i) {
        checksum ^= bytes[i];
    }
    return checksum;
}

// Utility function to validate IPC header
inline bool validateIpcHeader(const IpcHeader* header) {
    return header->magic == IPC_MAGIC &&
           header->version == IPC_VERSION &&
           header->size < 64 * 1024;  // Reasonable upper limit (64KB)
}