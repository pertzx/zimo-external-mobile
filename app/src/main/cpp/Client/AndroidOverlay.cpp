#include "AndroidOverlay.hpp"
#include <android/native_window.h>
#include <android/native_window_jni.h>
#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <android/log.h>

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "StormOverlay", __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "StormOverlay", __VA_ARGS__)

static EGLDisplay eglDisplay = EGL_NO_DISPLAY;
static EGLSurface eglSurface = EGL_NO_SURFACE;
static EGLContext eglContext = EGL_NO_CONTEXT;
static ANativeWindow* nativeWindow = nullptr;
static bool bInitialized = false;

namespace Overlay {

    bool Setup(ANativeWindow* window) {
        if (!window) {
            LOGE("Setup: window nulo");
            return false;
        }
        nativeWindow = window;
        return true;
    }

    bool Initialize() {
        if (!nativeWindow) {
            LOGE("Initialize: nativeWindow nulo");
            return false;
        }

        // ==== EGL Display ====
        eglDisplay = eglGetDisplay(EGL_DEFAULT_DISPLAY);
        if (eglDisplay == EGL_NO_DISPLAY) {
            LOGE("eglGetDisplay falhou");
            return false;
        }

        EGLint major, minor;
        if (!eglInitialize(eglDisplay, &major, &minor)) {
            LOGE("eglInitialize falhou");
            return false;
        }
        LOGI("EGL %d.%d inicializado", major, minor);

        // ==== Configuração do surface (RGBA8888, sem depth/stencil) ====
        const EGLint attribs[] = {
            EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
            EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
            EGL_RED_SIZE, 8,
            EGL_GREEN_SIZE, 8,
            EGL_BLUE_SIZE, 8,
            EGL_ALPHA_SIZE, 8,
            EGL_DEPTH_SIZE, 0,
            EGL_STENCIL_SIZE, 0,
            EGL_NONE
        };

        EGLConfig config;
        EGLint numConfigs;
        if (!eglChooseConfig(eglDisplay, attribs, &config, 1, &numConfigs) || numConfigs < 1) {
            LOGE("eglChooseConfig falhou");
            return false;
        }

        // ==== Criar surface ====
        eglSurface = eglCreateWindowSurface(eglDisplay, config, nativeWindow, nullptr);
        if (eglSurface == EGL_NO_SURFACE) {
            LOGE("eglCreateWindowSurface falhou");
            return false;
        }

        // ==== Criar contexto OpenGL ES 3.0 ====
        const EGLint contextAttribs[] = {
            EGL_CONTEXT_CLIENT_VERSION, 3,
            EGL_NONE
        };
        eglContext = eglCreateContext(eglDisplay, config, EGL_NO_CONTEXT, contextAttribs);
        if (eglContext == EGL_NO_CONTEXT) {
            LOGE("eglCreateContext falhou");
            return false;
        }

        // ==== Tornar current ====
        if (!eglMakeCurrent(eglDisplay, eglSurface, eglSurface, eglContext)) {
            LOGE("eglMakeCurrent falhou");
            return false;
        }

        // ==== Configurar viewport ====
        EGLint width, height;
        eglQuerySurface(eglDisplay, eglSurface, EGL_WIDTH, &width);
        eglQuerySurface(eglDisplay, eglSurface, EGL_HEIGHT, &height);
        glViewport(0, 0, width, height);

        // ==== Estado OpenGL para overlay transparente ====
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_CULL_FACE);
        glClearColor(0.0f, 0.0f, 0.0f, 0.0f);

        bInitialized = true;
        LOGI("Overlay OpenGL ES inicializado: %dx%d", width, height);
        return true;
    }

    void ShutDown() {
        if (eglDisplay != EGL_NO_DISPLAY) {
            eglMakeCurrent(eglDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
            if (eglContext != EGL_NO_CONTEXT) {
                eglDestroyContext(eglDisplay, eglContext);
                eglContext = EGL_NO_CONTEXT;
            }
            if (eglSurface != EGL_NO_SURFACE) {
                eglDestroySurface(eglDisplay, eglSurface);
                eglSurface = EGL_NO_SURFACE;
            }
            eglTerminate(eglDisplay);
            eglDisplay = EGL_NO_DISPLAY;
        }
        if (nativeWindow) {
            ANativeWindow_release(nativeWindow);
            nativeWindow = nullptr;
        }
        bInitialized = false;
    }

    // Adicionar no final de AndroidOverlay.cpp:

void glRefresh() {
    if (eglDisplay != EGL_NO_DISPLAY && eglSurface != EGL_NO_SURFACE) {
        eglSwapBuffers(eglDisplay, eglSurface);
    }
}

void glClearTransparent() {
    glClear(GL_COLOR_BUFFER_BIT);
}

ImVec2 GetTargetWindowSize() {
    EGLint width = 0, height = 0;
    if (eglDisplay != EGL_NO_DISPLAY && eglSurface != EGL_NO_SURFACE) {
        eglQuerySurface(eglDisplay, eglSurface, EGL_WIDTH, &width);
        eglQuerySurface(eglDisplay, eglSurface, EGL_HEIGHT, &height);
    }
    return ImVec2((float)width, (float)height);
}

bool IsInitialized() {
    return bInitialized;
}
}