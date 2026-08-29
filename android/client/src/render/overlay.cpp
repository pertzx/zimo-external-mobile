#include "overlay.hpp"
#include <android/log.h>

#define LOG_TAG "ZmInternal-Overlay"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

namespace ZmInternal {
    namespace Render {

        Overlay::Overlay() {
            // Constructor
        }

        Overlay::~Overlay() {
            Shutdown();
        }

        bool Overlay::Initialize(ANativeWindow* window) {
            m_window.reset(window); // Take ownership
            LOGI("Overlay initialized");
            return true;
        }

        void Overlay::Shutdown() {
            m_window.reset(); // Release the window
            LOGI("Overlay shut down");
        }

        void Overlay::SetDimensions(int width, int height) {
            m_width = width;
            m_height = height;
            LOGI("Overlay dimensions set: %dx%d", width, height);
        }

    } // namespace Render
} // namespace ZmInternal