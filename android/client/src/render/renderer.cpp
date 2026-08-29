#include "renderer.hpp"
#include <android/log.h>
#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <android/native_window.h>

#define LOG_TAG "ZmInternal-Renderer"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

namespace ZmInternal {
    namespace Render {

        Renderer::Renderer() {
            // Constructor
        }

        Renderer::~Renderer() {
            Shutdown();
        }

        bool Renderer::Initialize(ANativeWindow* window) {
            m_window = window;

            // Get EGL display
            m_display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
            if (m_display == EGL_NO_DISPLAY) {
                LOGE("eglGetDisplay failed");
                return false;
            }

            // Initialize EGL
            EGLint majorVersion, minorVersion;
            if (!eglInitialize(m_display, &majorVersion, &minorVersion)) {
                LOGE("eglInitialize failed");
                return false;
            }

            // Configure EGL
            EGLint configAttrs[] = {
                EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
                EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
                EGL_BLUE_SIZE, 8,
                EGL_GREEN_SIZE, 8,
                EGL_RED_SIZE, 8,
                EGL_DEPTH_SIZE, 16,
                EGL_NONE
            };
            EGLint numConfigs;
            EGLConfig config;
            if (!eglChooseConfig(m_display, configAttrs, &config, 1, &numConfigs) || numConfigs == 0) {
                LOGE("eglChooseConfig failed");
                return false;
            }

            // Create surface
            m_surface = eglCreateWindowSurface(m_display, config, m_window, nullptr);
            if (m_surface == EGL_NO_SURFACE) {
                LOGE("eglCreateWindowSurface failed");
                return false;
            }

            // Create context
            EGLint contextAttrs[] = {
                EGL_CONTEXT_CLIENT_VERSION, 2,
                EGL_NONE
            };
            m_context = eglCreateContext(m_display, config, EGL_NO_CONTEXT, contextAttrs);
            if (m_context == EGL_NO_CONTEXT) {
                LOGE("eglCreateContext failed");
                return false;
            }

            // Make context current
            if (!eglMakeCurrent(m_display, m_surface, m_surface, m_context)) {
                LOGE("eglMakeCurrent failed");
                return false;
            }

            // Query surface dimensions
            eglQuerySurface(m_display, m_surface, EGL_WIDTH, &m_width);
            eglQuerySurface(m_display, m_surface, EGL_HEIGHT, &m_height);

            // Set up OpenGL ES state
            glClearColor(0.0f, 0.0f, 0.0f, 0.0f); // Transparent black
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

            LOGI("Renderer initialized: %dx%d", m_width, m_height);
            return true;
        }

        void Renderer::Shutdown() {
            if (m_display != EGL_NO_DISPLAY) {
                eglMakeCurrent(m_display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
                if (m_context != EGL_NO_CONTEXT) {
                    eglDestroyContext(m_display, m_context);
                }
                if (m_surface != EGL_NO_SURFACE) {
                    eglDestroySurface(m_display, m_surface);
                }
                eglTerminate(m_display);
            }

            m_display = EGL_NO_DISPLAY;
            m_context = EGL_NO_CONTEXT;
            m_surface = EGL_NO_SURFACE;
            m_window = nullptr;
            m_width = 0;
            m_height = 0;
        }

        void Renderer::BeginFrame() {
            glClear(GL_COLOR_BUFFER_BIT);
        }

        void Renderer::EndFrame() {
            eglSwapBuffers(m_display, m_surface);
        }

    } // namespace Render
} // namespace ZmInternal