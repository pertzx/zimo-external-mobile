#include "AndroidUtils.hpp"
#include <android/log.h>

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "StormUtils", __VA_ARGS__)

namespace AndroidUtils {
    void LogInfo(const char* msg) {
        LOGI("%s", msg);
    }
}