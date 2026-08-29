#pragma once

#include <android/native_window.h>
#include <memory>

namespace ZmInternal {
    namespace Render {

        class Overlay {
        public:
            Overlay();
            ~Overlay();

            // Initialize overlay with native window
            bool Initialize(ANativeWindow* window);

            // Shutdown overlay
            void Shutdown();

            // Get the native window
            ANativeWindow* GetWindow() const { return m_window.get(); }

            // Set window dimensions (called when surface changes)
            void SetDimensions(int width, int height);

            // Get window dimensions
            int GetWidth() const { return m_width; }
            int GetHeight() const { return m_height; }

        private:
            // Custom deleter for ANativeWindow
            struct ANativeWindowDeleter {
                void operator()(ANativeWindow* window) const {
                    if (window) {
                        ANativeWindow_release(window);
                    }
                }
            };

            std::unique_ptr<ANativeWindow, ANativeWindowDeleter> m_window;
            int m_width = 0;
            int m_height = 0;
        };

    } // namespace Render
} // namespace ZmInternal