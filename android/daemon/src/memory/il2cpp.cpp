#include "il2cpp.hpp"
#include "memory.hpp"
#include <android/log.h>

#define LOG_TAG "ZmInternal-IL2CPP"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

// Screen dimensions (would be set from client via IPC or JNI)
static int g_screenWidth = 0;
static int g_screenHeight = 0;

// Set screen dimensions (called from client)
void SetScreenDimensions(int width, int height) {
    g_screenWidth = width;
    g_screenHeight = height;
    LOGI("Screen dimensions set: %dx%d", width, height);
}

// Vector3 struct (simplified)
struct Vector3 {
    float x, y, z;

    Vector3() : x(0), y(0), z(0) {}
    Vector3(float xx, float yy, float zz) : x(xx), y(yy), z(zz) {}

    static Vector3 Zero() { return Vector3(0, 0, 0); }
};

// TMatrix struct (simplified)
struct TMatrix {
    struct { float x, y, z, w; } Position;
    struct { float x, y, z, w; } Rotation;
    struct { float x, y, z, w; } Scale;
};

// Transform class implementation
bool Transform::GetPosition(uintptr_t transformPtr, float& x, float& y, float& z) {
    if (!transformPtr) {
        return false;
    }

    // In a real implementation, we would determine N32/V31 from globals
    // For now, assuming 32-bit (N32=true) as Free Fire is 32-bit
    const bool N32 = true;

    auto ReadPtr = [N32](uintptr_t addr) -> uintptr_t {
        return N32 ? Memory::Read<uint32_t>(addr) : Memory::Read<uint64_t>(addr);
    };

    uintptr_t transObj = ReadPtr(transformPtr + 0x8); // Offsets::GetPosWorld::transObj
    uintptr_t matrix = ReadPtr(transObj + 0x20);     // Offsets::GetPosWorld::matrix
    uintptr_t index = ReadPtr(transObj + 0x24);      // Offsets::GetPosWorld::index
    uintptr_t matrix_list = ReadPtr(matrix + 0x18);  // Offsets::GetPosWorld::matrix_list
    uintptr_t matrix_indices = ReadPtr(matrix + 0x1C); // Offsets::GetPosWorld::matrix_indices

    if (!transObj || !matrix || !matrix_list || !matrix_indices) {
        return false;
    }

    Vector3 result = Memory::Read<Vector3>(matrix_list + sizeof(TMatrix) * index);
    int transformIndex = Memory::Read<int>(matrix_indices + sizeof(int) * index);

    int curIndex = 0;
    while (transformIndex >= 0 && curIndex++ < 60) {
        TMatrix tMatrix = Memory::Read<TMatrix>(matrix_list + sizeof(TMatrix) * transformIndex);

        float rotX = tMatrix.Rotation.x;
        float rotY = tMatrix.Rotation.y;
        float rotZ = tMatrix.Rotation.z;
        float rotW = tMatrix.Rotation.w;

        float scaleX = result.x * tMatrix.Scale.x;
        float scaleY = result.y * tMatrix.Scale.y;
        float scaleZ = result.z * tMatrix.Scale.z;

        result.x = tMatrix.Position.x + scaleX + (scaleX * ((rotY * rotY * -2.0f) - (rotZ * rotZ * 2.0f))) +
                   (scaleY * ((rotW * rotZ * -2.0f) - (rotY * rotX * -2.0f))) +
                   (scaleZ * ((rotZ * rotX * 2.0f) - (rotW * rotY * -2.0f)));
        result.y = tMatrix.Position.y + scaleY + (scaleX * ((rotX * rotY * 2.0f) - (rotW * rotZ * -2.0f))) +
                   (scaleY * ((rotZ * rotZ * -2.0f) - (rotX * rotX * 2.0f))) +
                   (scaleZ * ((rotW * rotX * -2.0f) - (rotZ * rotY * -2.0f)));
        result.z = tMatrix.Position.z + scaleZ + (scaleX * ((rotW * rotY * -2.0f) - (rotX * rotZ * -2.0f))) +
                   (scaleY * ((rotY * rotZ * 2.0f) - (rotW * rotX * -2.0f))) +
                   (scaleZ * ((rotX * rotX * -2.0f) - (rotY * rotY * 2.0f)));

        transformIndex = Memory::Read<int>(matrix_indices + sizeof(int) * transformIndex);
    }

    x = result.x;
    y = result.y;
    z = result.z;

    return true;
}

bool Transform::GetHeadPosition(uintptr_t transformPtr, float& x, float& y, float& z) {
    if (!transformPtr) {
        return false;
    }

    // In a real implementation, we would determine N32/V31 from globals
    // For now, assuming 32-bit (N32=true) as Free Fire is 32-bit
    const bool N32 = true;

    auto ReadPtr = [N32](uintptr_t addr) -> uintptr_t {
        return N32 ? Memory::Read<uint32_t>(addr) : Memory::Read<uint64_t>(addr);
    };

    bool isFemale = Memory::Read<bool>(transformPtr + 0x7D8); // Offsets::Player::IsFemale
    uintptr_t headCollider = isFemale ? 0x3C : 0x38; // Offsets::GetPosWorld::HeadColliderFemale/Male

    uintptr_t getTransform = ReadPtr(transformPtr + 0x760); // Offsets::Player::m_fireColliders
    if (getTransform != 0) {
        uintptr_t transform = ReadPtr(getTransform + 0x8); // Offsets::GetPosWorld::ColliderTransform
        if (transform != 0) {
            uintptr_t location = ReadPtr(transform + headCollider);
            if (location != 0) {
                uintptr_t h1 = ReadPtr(location + 0x8); // Offsets::GetPosWorld::ColliderTransform
                if (h1 != 0) {
                    uintptr_t h2 = ReadPtr(h1 + 0x28); // Offsets::GetPosWorld::BoundsCenter_1
                    if (h2 != 0) {
                        uintptr_t h3 = ReadPtr(h2 + 0x14); // Offsets::GetPosWorld::BoundsCenter_2
                        if (h3 != 0) {
                            Vector3 pos = Memory::Read<Vector3>(h3 + 0x60); // Offsets::GetPosWorld::BoundsCenter_3
                            x = pos.x;
                            y = pos.y;
                            z = pos.z;
                            return true;
                        }
                    }
                }
            }
        }
    }

    uintptr_t headTF = ReadPtr(transformPtr + 0x458); // Offsets::Player::HeadNode
    if (headTF != 0) {
        uintptr_t headTransform = ReadPtr(headTF + 0x8); // Offsets::PlayerTransformNode::Transform
        if (headTransform != 0) {
            return GetPosition(headTransform, x, y, z);
        }
    }

    return false;
}

bool Transform::World2Screen(uintptr_t transformPtr, float worldX, float worldY, float worldZ, float& screenX, float& screenY) {
    // In a real implementation, we would get the view matrix from the game
    // For now, we'll return false as we don't have access to the view matrix
    // This would need to be implemented properly in a real cheat
    return false;
}

// UTF8 class implementation
std::string UTF8::Read(uintptr_t stringPtr, int maxLength) {
    if (!stringPtr || maxLength <= 0) {
        return "";
    }

    return Memory::String(stringPtr, maxLength);
}