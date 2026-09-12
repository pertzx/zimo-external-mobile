#pragma once

#include "WindowsCompat.hpp"

#include <cstdint>
#include <cstddef>
#include <cstdlib>
#include <cstring>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

// Android
#include <android/log.h>
#include <android/keycodes.h>

// OpenGL ES
#ifdef __ANDROID__
#include <GLES3/gl3.h>
#include <GLES3/gl3ext.h>
#endif

// Logs
#ifndef LOGI
#define LOGI(...) \
    __android_log_print( \
        ANDROID_LOG_INFO, \
        "Storm", \
        __VA_ARGS__ \
    )
#endif

#ifndef LOGE
#define LOGE(...) \
    __android_log_print( \
        ANDROID_LOG_ERROR, \
        "Storm", \
        __VA_ARGS__ \
    )
#endif

// XOR
#include <XorStr.hpp>

#undef XorStr
#define XorStr(str) xorstr_(str)