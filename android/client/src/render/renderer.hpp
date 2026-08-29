#pragma once

#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <android/native_window.h>

namespace ZmInternal {
    namespace Render {

        class Renderer {
        public:
            Renderer();
            ~Renderer();

            // Initialize EGL and OpenGL ES
            bool Initialize(ANativeWindow* window);

            // Shutdown renderer
            void Shutdown();

            // Begin frame
            void BeginFrame();

            // End frame (swap buffers)
            void EndFrame();

            // Get EGL display
            EGLDisplay GetDisplay() const { return m_display; }

            // Get EGL surface
            EGLSurface GetSurface() const { return m_surface; }

            // Get EGL context
            EGLContext GetContext() const { return m_context; }

        private:
            EGLDisplay m_display = EGL_NO_DISPLAY;
            EGLSurface m_surface = EGL_NO_SURFACE;
            EGLContext m_context = EGL_NO_CONTEXT;
            ANativeWindow* m_window = nullptr;
            int m_width = 0;
            int m_height = 0;
        };

    } // namespace Render
} // namespace ZmInternal