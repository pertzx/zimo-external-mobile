#pragma once

// Compatibilidade Windows -> Android
#include "WindowsCompat.hpp"

#include <functional>
#include <iostream>
#include <string>
#include <cstdio>
#include <algorithm>
#include <chrono>
#include <thread>
#include <vector>
#include <map>
#include <cstring>
#include <cmath>

// GL (OpenGL ES no Android)
#include <GLES3/gl3.h>
#include <GLES3/gl3ext.h>

// ImGui
#include <imgui.h>
#include <imgui_impl_android.h>
#include <imgui_impl_opengl3.h>
#include <imgui_internal.h>
#include <imspinner.h>

// XOR string
#include <XorStr.hpp>
#undef XorStr
#define XorStr(str) xorstr_(str)

// Android log
#include <android/log.h>

// stb_image para load de texturas
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

// Android keycodes mapeados para uso no painel
#include <android/keycodes.h>

// Macros de log Android
#ifndef LOGI
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "Storm", __VA_ARGS__)
#endif
#ifndef LOGE
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "Storm", __VA_ARGS__)
#endif
