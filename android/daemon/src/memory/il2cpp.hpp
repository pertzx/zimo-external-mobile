#pragma once
#include <cstdint>
#include <string>
#include "memory.hpp"

// IL2CPP wrapper classes - ported from EspLines/Main/Unity/

class Transform {
public:
    // Get position of transform
    static bool GetPosition(uintptr_t transformPtr, float& x, float& y, float& z);

    // Get head position (with offset)
    static bool GetHeadPosition(uintptr_t transformPtr, float& x, float& y, float& z);

    // Convert world position to screen position
    static bool World2Screen(uintptr_t transformPtr, float worldX, float worldY, float worldZ, float& screenX, float& screenY);
};

class UTF8 {
public:
    // Read UTF-8 string from memory
    static std::string Read(uintptr_t stringPtr, int maxLength = 64);
};

// Note: Additional IL2CPP wrappers can be added as needed
// Based on EspLines/Main/Unity/Unity.cpp and related files