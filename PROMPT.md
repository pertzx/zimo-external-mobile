# Guia Completo de Migração: Windows DLL → Android APK
## Projeto: Storm Cheats (FreeFire) — github.com/botdragonnn/cheat-free-fire

> **Data:** 30/08/2026 | **Autor:** Kimi AI | **Versão:** 1.0

---

## 📋 Sumário

1. [Análise do Código Existente](#1-análise-do-código-existente)
2. [Arquitetura de Separação (Painel vs Daemon)](#2-arquitetura-de-separação)
3. [Estrutura de Diretórios do Projeto Android](#3-estrutura-de-diretórios)
4. [Passo a Passo da Migração](#4-passo-a-passo-da-migração)
5. [Códigos Completos dos Arquivos Novos](#5-códigos-completos)
6. [Tabela de Substituções](#6-tabela-de-substituições)
7. [Build e Deploy](#7-build-e-deploy)

---

## 1. Análise do Código Existente

### 1.1 Arquivos Analisados

```
📦 cheat-free-fire
 ├─ dllmain.cpp                          → Entry point da DLL (Windows)
 ├─ src/Cheat/
 │   ├─ Cheat.cpp/.hpp                  → Loop principal, inicialização, cleanup
 │   ├─ Globals.hpp                     → Estrutura de configurações (g_Globals)
 │   ├─ saveconfig.cpp                  → Salva/carrega config em INI
 │   ├─ SharedMemory.h                  → Buffer compartilhado com driver
 │   ├─ WebPanel.cpp/.hpp               → Servidor HTTP local (Winsock)
 ├─ src/Render/
 │   ├─ Interface/Interface.cpp/.hpp    → UI ImGui completa (login, tabs, dock)
 │   ├─ Overlay/Overlay.cpp/.hpp        → Janela overlay transparente (Win32)
 │   ├─ Overlay/Render/Render.cpp/.hpp  → Busca janela BlueStacks por classe
 │   ├─ Fonts/Fonts.cpp/.hpp            → Carrega fontes e texturas (WIC/GL)
 ├─ src/Utils/
 │   ├─ Utils.cpp/.hpp                  → Privilégios, console, random strings
 ├─ EspLines/Main/
 │   ├─ Draw/Draw.cpp/.hpp              → ESP, aimbot, silent, exploits (LEITURA MEMÓRIA)
 │   ├─ Memory/Memory.cpp/.hpp          → Leitura memória do emulador (BstkVMM)
 │   ├─ Unity/Unity.cpp/.hpp            → W2S, transforms, matrizes
 │   ├─ Offsets/Offsets.hpp             → Offsets do jogo
 ├─ ext/
 │   ├─ Dependencies/ImGui/Custom.hpp   → Widgets customizados (checkbox, slider, etc.)
 │   ├─ Dependencies/Notify/Notify.hpp  → Sistema de notificações/toasts
 │   ├─ KeyAuth/KeyAuth.hpp             → Autenticação via KeyAuth.win
 │   ├─ Discord/DiscordRPC.hpp          → Integração Discord
```

### 1.2 Classificação por Responsabilidade

| Arquivo | Tipo | Responsabilidade | Vai para |
|---------|------|------------------|----------|
| `Cheat.cpp` | **Orchestrator** | Loop principal, inicialização, shutdown, coordena UI + backend | **Painel** (com adaptações) |
| `Globals.hpp` | **Data** | Todas as configurações do cheat | **Ambos** (via IPC) |
| `Interface.cpp` | **UI** | ImGui: login, dock, tabs, animações, drag, tooltips | **Painel** (preservado) |
| `Interface.hpp` | **UI** | Declarações da classe Interface | **Painel** (adaptado) |
| `Overlay.cpp` | **Platform** | Criação de janela Win32, OpenGL context, DWM | **Painel** (substituído por Android) |
| `Overlay.hpp` | **Platform** | API da janela overlay | **Painel** (adaptado) |
| `Render.cpp` | **Platform** | Enumera janelas por classe (BlueStacksApp) | **Remover** (não necessário no Android) |
| `Fonts.cpp` | **UI** | Carrega fontes TTF, texturas PNG via WIC | **Painel** (adaptado para Android) |
| `Fonts.hpp` | **UI** | Declarações de fontes e texturas | **Painel** |
| `Custom.hpp` | **UI** | Widgets customizados do ImGui | **Painel** (preservado) |
| `Notify.hpp/.cpp` | **UI** | Toast notifications | **Painel** (preservado) |
| `saveconfig.cpp` | **Data** | Persistência de config em arquivo INI | **Painel** (adaptado para Android paths) |
| `SharedMemory.h` | **IPC** | Buffer compartilhado com driver kernel | **Adaptar** para IPC entre painel e daemon |
| `WebPanel.cpp/.hpp` | **Backend** | Servidor HTTP Winsock para controle remoto | **Daemon** (adaptado para Android) |
| `Draw.cpp/.hpp` | **Backend** | ESP, aimbot, silent, exploits, leitura de memória | **Daemon** |
| `Memory.cpp/.hpp` | **Backend** | Leitura/escrita memória do emulador via BstkVMM | **Daemon** |
| `Unity.cpp/.hpp` | **Backend** | W2S, transform hierarchy, matrizes | **Daemon** |
| `Offsets.hpp` | **Data** | Offsets do jogo | **Daemon** |
| `Utils.cpp/.hpp` | **Platform** | Privilégios Windows, console, random strings | **Ambos** (adaptado) |
| `KeyAuth.hpp` | **Backend** | Autenticação online | **Daemon** (ou painel, via IPC) |
| `DiscordRPC.hpp` | **Backend** | Rich presence Discord | **Remover** (não suportado no Android) |

### 1.3 Dependências Cruzadas (Quem Usa Quem)

```
Cheat::Initialize()
  ├── Utils::EnableDebugPrivilege()        [Win32 - remover]
  ├── VehCpuHook::Initialize()             [Win32 VEH hooks - mover para daemon]
  ├── FullScreenFixHooks()                 [Win32 PAGE_GUARD - mover para daemon]
  ├── Overlay::Setup()                     [Win32 - substituir]
  │     └── Render::LookupWindowByClassName("BlueStacksApp")  [Win32 - remover]
  ├── Overlay::Initialize()                [Win32 - substituir]
  ├── Interface::Initialize()              [ImGui - preservar]
  │     ├── ImGui::CreateContext()         [Cross-platform]
  │     ├── ImGui_ImplWin32_Init()         [Win32 - substituir por Android]
  │     ├── ImGui_ImplOpenGL3_Init()       [Cross-platform]
  │     └── Fonts::Initialize()            [Preservar, adaptar carregamento]
  ├── Data::Draw()                         [Backend - mover para daemon]
  │     ├── Memory::Read/Write             [Backend - mover]
  │     ├── W2S::World2Screen              [Backend - mover]
  │     └── g_Globals (config)             [Shared via IPC]
  ├── g_Interface->RenderGui()             [UI - preservar]
  │     ├── Custom::Checkbox, Slider, etc  [Preservar]
  │     ├── Fonts::Gff, InterBold, etc     [Preservar]
  │     └── g_Globals (config)             [Shared via IPC]
  ├── Chams::Enable/Disable               [Backend - mover]
  ├── Silent::Start/Stop                  [Backend - mover]
  └── WebPanel::Start/Stop                [Backend - mover]
```

---

## 2. Arquitetura de Separação

### 2.1 Visão Geral

```
┌─────────────────────────────────────────────────────────────┐
│                     ANDROID DEVICE                          │
│  ┌─────────────────────┐    ┌───────────────────────────┐  │
│  │   PAINEL (APK)      │    │      DAEMON (Service)     │  │
│  │   C++ / ImGui       │◄──►│      C++ / Native         │  │
│  │   Overlay OpenGL    │IPC │      Memory Read/Write    │  │
│  │   Input Touch       │    │      ESP/Aimbot/Silent    │  │
│  │   Config UI         │    │      Exploits             │  │
│  │   Save/Load Config  │    │      WebPanel Server      │  │
│  └─────────────────────┘    └───────────────────────────┘  │
│           ▲                          ▲                      │
│           │                          │                      │
│     ANativeWindow              /proc/mem, ptrace,          │
│     InputQueue                 emulador memory               │
│     ALooper                    (BstkVMM adaptado)           │
└─────────────────────────────────────────────────────────────┘
```

### 2.2 Interface IPC/API

O painel e o daemon se comunicam via **Unix Domain Socket** (Android) ou **AIDL** (Binder). Para manter a simplicidade e compatibilidade com a arquitetura existente, usamos **Shared Memory** (`ashmem`) com protocolo similar ao `SharedMemory.h` existente.

#### Protocolo IPC (baseado em SharedMemory.h existente)

```cpp
// ipc_protocol.h — NOVO ARQUIVO (ambos os lados)
#pragma once
#include <stdint.h>

#define IPC_MAGIC_CMD    0x444D435A  // 'CMDZ' (mesmo do original)
#define IPC_MAGIC_STATE  0x42455742  // 'BWEB' (mesmo do original)

#define IPC_CMD_NONE     0
#define IPC_CMD_STREAM   1
#define IPC_CMD_UNLOAD   2
#define IPC_CMD_SAVE_CFG 3
#define IPC_CMD_LOAD_CFG 4
#define IPC_CMD_RESTART  5

// Estrutura de comandos: Painel → Daemon
#pragma pack(push, 1)
typedef struct _IPC_COMMAND_BUFFER {
    uint32_t Magic;           // IPC_MAGIC_CMD
    uint32_t CommandType;     // IPC_CMD_*
    uint64_t Param1;          // Dados extras
    uint32_t Param2;
    uint32_t Processed;       // Daemon seta 1 quando processar
    uint32_t DaemonReady;     // Daemon seta 1 quando pronto
    uint32_t Reserved[2];
} IPC_COMMAND_BUFFER, *PIPC_COMMAND_BUFFER;
#pragma pack(pop)

// Estrutura de estado: Daemon → Painel (espelho de g_Globals)
// REUTILIZAR a estrutura WEB_FEATURES de SharedMemory.h existente!
// Adicionar apenas os campos necessários para o painel

#pragma pack(push, 1)
typedef struct _IPC_STATE {
    uint32_t Magic;           // IPC_MAGIC_STATE
    uint32_t Authenticated;   // 1 = daemon autenticado
    uint32_t ConfigSeq;       // Incrementa a cada mudança
    uint32_t FeatureBitsOut;  // Status do daemon

    // ==== Dados do jogo (Daemon → Painel) ====
    uint32_t EnemyCount;      // Número de inimigos detectados
    float    ClosestEnemyDist;// Distância do inimigo mais próximo
    uint32_t LocalPlayerHP;   // HP do jogador local
    uint32_t MatchState;      // Estado da partida

    // ==== Configurações (Bidirecional) ====
    // REUTILIZAR WEB_FEATURES do SharedMemory.h original
    // O painel escreve aqui; o daemon lê e aplica
    // ... (copiar estrutura WEB_FEATURES inteira)

} IPC_STATE, *PIPC_STATE;
#pragma pack(pop)
```

---

## 3. Estrutura de Diretórios do Projeto Android

```
📦 StormCheats-Android/
├─ app/
│   ├─ src/main/
│   │   ├─ AndroidManifest.xml
│   │   ├─ java/com/stormcheats/
│   │   │   ├─ MainActivity.java          → Inicia NativeActivity
│   │   │   ├─ OverlayService.java        → Service de overlay (SYSTEM_ALERT_WINDOW)
│   │   │   └─ DaemonService.java         → Service nativo do daemon
│   │   ├─ cpp/                          → CÓDIGO C++ NATIVO
│   │   │   ├─ CMakeLists.txt
│   │   │   ├─ main.cpp                   → Entry point do painel
│   │   │   ├─ daemon_main.cpp            → Entry point do daemon
│   │   │   ├─ Panel/
│   │   │   │   ├─ PanelApp.cpp/.hpp      → App do painel (loop principal)
│   │   │   │   ├─ AndroidOverlay.cpp/.hpp → Substitui Overlay.cpp
│   │   │   │   ├─ AndroidInput.cpp/.hpp  → Substitui input Win32
│   │   │   │   ├─ AndroidRenderer.cpp/.hpp→ Substitui renderer OpenGL Win32
│   │   │   │   └─ IPCClient.cpp/.hpp     → Cliente IPC (comunica com daemon)
│   │   │   ├─ Daemon/
│   │   │   │   ├─ DaemonApp.cpp/.hpp     → App do daemon
│   │   │   │   ├─ IPCServer.cpp/.hpp     → Servidor IPC
│   │   │   │   ├─ Memory/
│   │   │   │   │   ├─ Memory.cpp/.hpp    → MOVIDO do EspLines/Main/Memory/
│   │   │   │   │   ├─ EmulatorEnv.cpp/.hpp → Adaptado do original
│   │   │   │   │   └─ ...
│   │   │   │   ├─ Draw/
│   │   │   │   │   ├─ Draw.cpp/.hpp      → MOVIDO do EspLines/Main/Draw/
│   │   │   │   │   ├─ Silent.cpp/.hpp    → MOVIDO do original
│   │   │   │   │   └─ ...
│   │   │   │   ├─ Unity/
│   │   │   │   │   ├─ Unity.cpp/.hpp     → MOVIDO do EspLines/Main/Unity/
│   │   │   │   │   └─ ...
│   │   │   │   ├─ WebPanel/
│   │   │   │   │   ├─ WebPanel.cpp/.hpp  → MOVIDO do src/Cheat/
│   │   │   │   │   └─ ...
│   │   │   │   └─ Exploits/
│   │   │   │       └─ ...
│   │   │   ├─ Shared/                    → CÓDIGO COMPARTILHADO
│   │   │   │   ├─ Globals.hpp            → MOVIDO do src/Cheat/
│   │   │   │   ├─ IPCProtocol.hpp        → NOVO (protocolo IPC)
│   │   │   │   ├─ Offsets/
│   │   │   │   │   └─ Offsets.hpp        → MOVIDO do EspLines/Main/Offsets/
│   │   │   │   ├─ Math/
│   │   │   │   │   └─ ...                → MOVIDO do original
│   │   │   │   ├─ Utils/
│   │   │   │   │   ├─ Utils.cpp/.hpp     → MOVIDO do src/Utils/
│   │   │   │   │   └─ AndroidUtils.cpp/.hpp → Adaptado
│   │   │   │   └─ Config/
│   │   │   │       └─ saveconfig.cpp     → MOVIDO do src/Cheat/
│   │   │   └─ External/                  → DEPENDÊNCIAS EXTERNAS
│   │   │       ├─ imgui/                 → ImGui (mesmo do original)
│   │   │       ├─ freetype/              → FreeType (mesmo do original)
│   │   │       └─ ...
│   │   ├─ res/
│   │   │   └─ ...                        → Recursos Android
│   │   └─ assets/
│   │       └─ fonts/                     → Fontes TTF
├─ build.gradle
└─ settings.gradle
```

---

## 4. Passo a Passo da Migração

### PASSO 1: Preparação do Ambiente

**Ação:** Configurar Android NDK, CMake, e estrutura de projeto.

**Arquivos novos:**
- `app/build.gradle`
- `app/src/main/cpp/CMakeLists.txt`
- `app/src/main/AndroidManifest.xml`

**Código completo de `CMakeLists.txt`:**

```cmake
cmake_minimum_required(VERSION 3.18.1)
project("stormcheats")

# ==== Configurações globais ====
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ==== Encontrar bibliotecas do NDK ====
find_library(log-lib log)
find_library(android-lib android)
find_library(EGL-lib EGL)
find_library(GLESv3-lib GLESv3)

# ==== ImGui ====
set(IMGUI_DIR ${CMAKE_SOURCE_DIR}/External/imgui)
file(GLOB IMGUI_SOURCES 
    ${IMGUI_DIR}/*.cpp
    ${IMGUI_DIR}/backends/imgui_impl_android.cpp
    ${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp
)

# ==== FreeType ====
set(FREETYPE_DIR ${CMAKE_SOURCE_DIR}/External/freetype)
# (adicionar sources do freetype ou usar prebuilt)

# ==== Sources compartilhados ====
set(SHARED_SOURCES
    ${CMAKE_SOURCE_DIR}/Shared/Globals.cpp
    ${CMAKE_SOURCE_DIR}/Shared/Utils/Utils.cpp
    ${CMAKE_SOURCE_DIR}/Shared/Utils/AndroidUtils.cpp
    ${CMAKE_SOURCE_DIR}/Shared/Config/saveconfig.cpp
    ${CMAKE_SOURCE_DIR}/Shared/IPC/IPCProtocol.cpp
)

# ==== Sources do Painel ====
set(PANEL_SOURCES
    ${CMAKE_SOURCE_DIR}/Panel/main.cpp
    ${CMAKE_SOURCE_DIR}/Panel/PanelApp.cpp
    ${CMAKE_SOURCE_DIR}/Panel/AndroidOverlay.cpp
    ${CMAKE_SOURCE_DIR}/Panel/AndroidInput.cpp
    ${CMAKE_SOURCE_DIR}/Panel/AndroidRenderer.cpp
    ${CMAKE_SOURCE_DIR}/Panel/IPCClient.cpp
    ${CMAKE_SOURCE_DIR}/Panel/Interface/Interface.cpp
    ${CMAKE_SOURCE_DIR}/Panel/Interface/Custom.cpp
    ${CMAKE_SOURCE_DIR}/Panel/Fonts/Fonts.cpp
    ${CMAKE_SOURCE_DIR}/Panel/Notify/Notify.cpp
)

# ==== Sources do Daemon ====
set(DAEMON_SOURCES
    ${CMAKE_SOURCE_DIR}/Daemon/daemon_main.cpp
    ${CMAKE_SOURCE_DIR}/Daemon/DaemonApp.cpp
    ${CMAKE_SOURCE_DIR}/Daemon/IPCServer.cpp
    ${CMAKE_SOURCE_DIR}/Daemon/Memory/Memory.cpp
    ${CMAKE_SOURCE_DIR}/Daemon/Draw/Draw.cpp
    ${CMAKE_SOURCE_DIR}/Daemon/Draw/Silent.cpp
    ${CMAKE_SOURCE_DIR}/Daemon/Unity/Unity.cpp
    ${CMAKE_SOURCE_DIR}/Daemon/WebPanel/WebPanel.cpp
    ${CMAKE_SOURCE_DIR}/Daemon/Exploits/Exploits.cpp
)

# ==== Biblioteca do Painel ====
add_library(panel SHARED ${PANEL_SOURCES} ${SHARED_SOURCES} ${IMGUI_SOURCES})
target_include_directories(panel PRIVATE 
    ${CMAKE_SOURCE_DIR}/Shared
    ${CMAKE_SOURCE_DIR}/Panel
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends
    ${FREETYPE_DIR}/include
)
target_link_libraries(panel
    ${log-lib}
    ${android-lib}
    ${EGL-lib}
    ${GLESv3-lib}
    freetype
)

# ==== Biblioteca do Daemon ====
add_library(daemon SHARED ${DAEMON_SOURCES} ${SHARED_SOURCES})
target_include_directories(daemon PRIVATE 
    ${CMAKE_SOURCE_DIR}/Shared
    ${CMAKE_SOURCE_DIR}/Daemon
)
target_link_libraries(daemon
    ${log-lib}
    ${android-lib}
)

# ==== FreeType (prebuilt ou compilado) ====
add_subdirectory(${FREETYPE_DIR} ${CMAKE_BINARY_DIR}/freetype)
```

---

### PASSO 2: Adaptar `Globals.hpp` (Shared)

**Arquivo:** `Shared/Globals.hpp` (MOVIDO de `src/Cheat/Globals.hpp`)

**Mudanças necessárias:**

```cpp
// PROCURAR e REMOVER:
#include <Windows.h>

// SUBSTITUIR por:
#include <cstdint>

// PROCURAR e REMOVER (campo específico do Windows):
int MenuKey = VK_INSERT;

// SUBSTITUIR por (código de tecla genérico):
int MenuKey = 0x2F; // SDLK_INSERT equivalente (ou mapear no AndroidInput)

// PROCURAR e REMOVER:
char* Local;

// SUBSTITUIR por (Android não usa wchar_t da mesma forma):
char Local[256];

// ADICIONAR (para IPC):
#include "IPC/IPCProtocol.hpp"
extern IPC_STATE g_IPCState;
extern IPC_COMMAND_BUFFER g_IPCCommand;
```

**Código completo adaptado:**

```cpp
#pragma once
#include <cstdint>
#include <cstring>

namespace Cheat {
    class Globals {
    public:
        struct AimBot { /* ... mesmo conteúdo ... */ } AimBot;
        struct Silent { /* ... mesmo conteúdo ... */ } Silent;
        struct Visuals { /* ... mesmo conteúdo ... */ } Visuals;
        struct Misc { /* ... mesmo conteúdo ... */ } Misc;
        struct General {
            int MenuKey = 0x2F;  // Mapeado no AndroidInput
            int ThreadDelay = 240;
            bool CaptureBypass = true;
            bool WebRemote = false;
            bool ShutDown = false;
            bool NoAnogs = false;
            bool N32 = false;
            bool V31 = false;
            bool EnableFuncs = false;
            char Username[20];
            char Role[20];
            char PassWord[20];
            char License[255];
            char Local[256];
        } General;
    };
}
inline Cheat::Globals g_Globals;
```

---

### PASSO 3: Criar `AndroidOverlay.cpp` (substitui `Overlay.cpp`)

**Arquivo:** `Panel/AndroidOverlay.cpp` (NOVO — substitui `src/Render/Overlay/Overlay.cpp`)

**O que muda:**
- Win32 `CreateWindowEx` → `ANativeWindow` (via `NativeActivity` ou `SurfaceView`)
- `WS_EX_LAYERED` + DWM → `FLAG_NOT_TOUCH_MODAL` + `TYPE_APPLICATION_OVERLAY`
- `wglMakeCurrent` → `eglMakeCurrent`
- `SwapBuffers(hDC)` → `eglSwapBuffers(eglDisplay, eglSurface)`
- `SetLayeredWindowAttributes` → Alpha via OpenGL (já era assim no original)

**Código completo:**

```cpp
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

    void glRefresh() {
        if (!bInitialized) return;
        eglSwapBuffers(eglDisplay, eglSurface);
    }

    void glClearTransparent() {
        glClear(GL_COLOR_BUFFER_BIT);
    }

    ImVec2 GetTargetWindowSize() {
        if (!bInitialized) return ImVec2(0, 0);
        EGLint width, height;
        eglQuerySurface(eglDisplay, eglSurface, EGL_WIDTH, &width);
        eglQuerySurface(eglDisplay, eglSurface, EGL_HEIGHT, &height);
        return ImVec2((float)width, (float)height);
    }

    bool IsInitialized() { return bInitialized; }
}
```

**Arquivo:** `Panel/AndroidOverlay.hpp`

```cpp
#pragma once
#include <imgui.h>
#include <android/native_window.h>

namespace Overlay {
    bool Setup(ANativeWindow* window);
    bool Initialize();
    void ShutDown();
    void glRefresh();
    void glClearTransparent();
    ImVec2 GetTargetWindowSize();
    bool IsInitialized();
}
```

---

### PASSO 4: Adaptar `Interface.cpp` para Android

**Arquivo:** `Panel/Interface/Interface.cpp` (MOVIDO e ADAPTADO de `src/Render/Interface/Interface.cpp`)

**Mudanças principais:**

```cpp
// PROCURAR e REMOVER:
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

// SUBSTITUIR por (Android não tem WndProc):
// (remover completamente — input é tratado por AndroidInput)

// PROCURAR e REMOVER:
#include <shellapi.h>
#include <ext/KeyAuth/KeyAuth.hpp>
#include <ext/Discord/DiscordRPC.hpp>

// SUBSTITUIR por:
// (DiscordRPC não existe no Android — remover)
// KeyAuth pode ser mantido se houver lib compatível, ou mover para daemon

// PROCURAR e REMOVER (Win32 específico):
SetWindowLong(hWindow, GWL_EXSTYLE, WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_LAYERED);
SetWindowPos(hWindow, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_FRAMECHANGED | SWP_SHOWWINDOW);
SetForegroundWindow(hWindow);

// SUBSTITUIR por:
// (nada — janela é controlada pelo AndroidOverlay e SurfaceView)

// PROCURAR e REMOVER (input Win32):
void Interface::WindowProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    // ...
}

// SUBSTITUIR por:
// (input é processado em AndroidInput.cpp e enviado para ImGui via ImGui_ImplAndroid)

// PROCURAR e REMOVER (teclas Win32):
static bool MenuKeyDown = false;
if (GetAsyncKeyState(g_Globals.General.MenuKey) & 0x8000)

// SUBSTITUIR por:
// (usar estado de tecla do AndroidInput)
extern bool g_AndroidMenuKeyPressed;
if (g_AndroidMenuKeyPressed)

// PROCURAR e REMOVER (clipboard Win32):
if (OpenClipboard(nullptr)) { EmptyClipboard(); ... }

// SUBSTITUIR por:
// (usar JNI para acessar clipboard do Android, ou remover feature)
```

**Código completo adaptado (trechos críticos):**

```cpp
#include "Interface.hpp"
#include "Notify/Notify.hpp"
#include <Main/Draw/Draw.hpp>
#include <Main/Draw/Silent.hpp>
#include <Main/Memory/Memory.hpp>
#include <thread>
#include <iostream>
#include <cmath>
#include <XorStr.hpp>
#include <DynamicStub/DynamicStub.hpp>
#include <Chams/Chams.hpp>
#include <VehPGDHook/Vehpageguardhook.hpp>
#include "WebPanel.hpp"
#include "AndroidInput.hpp"  // NOVO
#include "AndroidOverlay.hpp" // NOVO

// Removido: DiscordRPC, KeyAuth (mover para daemon ou adaptar)

void Interface::Initialize(ANativeWindow* window)  // MUDANÇA: HWND → ANativeWindow*
{
    // Não precisa de HDC/HGLRC no Android — EGL gerencia
    Overlay::Setup(window);
    Overlay::Initialize();

    ImGui::CreateContext();

    // SUBSTITUIR ImGui_ImplWin32_Init por ImGui_ImplAndroid_Init
    ImGui_ImplAndroid_Init();
    ImGui_ImplOpenGL3_Init("#version 300 es");  // OpenGL ES 3.0

    InitializeMenu();

    std::thread([]() {
        NotifyManager::Send(XorStr("Bem Vindo(a)"), 4000);
    }).detach();
}

void Interface::HandleMenuKey()
{
    // Usar estado do AndroidInput em vez de GetAsyncKeyState
    static bool MenuKeyDown = false;
    bool keyPressed = AndroidInput::IsKeyPressed(g_Globals.General.MenuKey);

    if (keyPressed)
    {
        if (!MenuKeyDown)
        {
            MenuKeyDown = true;
            bIsMenuOpen = !bIsMenuOpen;
            // No Android, visibilidade do overlay é controlada pelo SurfaceView
            // Não precisa mudar WS_EX_TRANSPARENT
        }
    }
    else
    {
        MenuKeyDown = false;
    }
}

void Interface::ShutDown()
{
    // Removido: DiscordRPC::Shutdown();
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplAndroid_Shutdown();  // MUDANÇA
    ImGui::DestroyContext();
    Fonts::CleanupTextures();
    Overlay::ShutDown();
}
```

---

### PASSO 5: Adaptar `Fonts.cpp` para Android

**Arquivo:** `Panel/Fonts/Fonts.cpp` (MOVIDO e ADAPTADO de `src/Render/Fonts/Fonts.cpp`)

**Mudança principal:** Remover WIC (Windows Imaging Component) e usar stb_image ou carregar raw RGBA diretamente.

```cpp
// PROCURAR e REMOVER:
#include <wincodec.h>
#include <vector>
#pragma comment(lib, "Windowscodecs.lib")

// SUBSTITUIR por:
#include <stb_image.h>  // Adicionar stb_image.h ao projeto
// OU carregar raw RGBA diretamente (já existe LoadTextureFromRawRGBA)

// PROCURAR e REMOVER toda a função LoadTextureFromPNG (usa WIC):
bool Fonts::LoadTextureFromPNG(const unsigned char* png_data, size_t png_size, ...)
{
    // ... código WIC completo ...
}

// SUBSTITUIR por (usar stb_image):
bool Fonts::LoadTextureFromPNG(const unsigned char* png_data, size_t png_size, 
                                GLuint* out_texture, int* out_width, int* out_height) {
    if (!png_data || png_size == 0 || !out_texture) return false;

    int w, h, channels;
    unsigned char* rgba = stbi_load_from_memory(png_data, (int)png_size, &w, &h, &channels, 4);
    if (!rgba) return false;

    bool result = LoadTextureFromRawRGBA(rgba, w, h, out_texture, false);
    stbi_image_free(rgba);

    if (out_width) *out_width = w;
    if (out_height) *out_height = h;
    return result;
}
```

**O restante do `Fonts.cpp` permanece IGUAL** — `Initialize()`, `LoadTextureFromRawRGBA()`, `CleanupTextures()` não precisam de mudanças (OpenGL é cross-platform).

---

### PASSO 6: Criar `AndroidInput.cpp`

**Arquivo:** `Panel/AndroidInput.cpp` (NOVO)

**Responsabilidade:** Converter eventos de toque do Android (AInputQueue) para eventos do ImGui.

```cpp
#include "AndroidInput.hpp"
#include <android/input.h>
#include <imgui.h>
#include <imgui_impl_android.h>

namespace AndroidInput {
    static bool s_MenuKeyPressed = false;
    static int s_MenuKeyCode = AKEYCODE_INSERT; // Mapear para botão volume ou gesture

    bool IsKeyPressed(int keyCode) {
        // Mapear códigos de tecla Android para os do painel
        if (keyCode == 0x2F) return s_MenuKeyPressed; // INSERT mapeado
        return false;
    }

    int32_t HandleInputEvent(AInputEvent* event) {
        return ImGui_ImplAndroid_HandleInputEvent(event);
    }

    void SetMenuKeyPressed(bool pressed) {
        s_MenuKeyPressed = pressed;
    }

    // Mapear botão de volume para menu (comum em cheats mobile)
    void HandleKeyEvent(int32_t keyCode, bool down) {
        if (keyCode == AKEYCODE_VOLUME_UP) {
            s_MenuKeyPressed = down;
        }
    }
}
```

---

### PASSO 7: Criar `PanelApp.cpp` (loop principal do painel)

**Arquivo:** `Panel/PanelApp.cpp` (NOVO — substitui `Cheat::Initialize()`)

**Baseado em:** `src/Cheat/Cheat.cpp`

```cpp
#include "PanelApp.hpp"
#include "AndroidOverlay.hpp"
#include "AndroidInput.hpp"
#include "Interface/Interface.hpp"
#include "IPC/IPCClient.hpp"
#include <android/log.h>
#include <thread>
#include <chrono>

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "StormPanel", __VA_ARGS__)

static bool g_Running = true;
static Interface* g_Interface = nullptr;

void PanelApp::Run(ANativeWindow* window) {
    LOGI("Iniciando painel Storm Cheats");

    // Inicializar overlay OpenGL
    if (!Overlay::Setup(window) || !Overlay::Initialize()) {
        LOGE("Falha ao inicializar overlay");
        return;
    }

    // Inicializar ImGui + Interface
    g_Interface = new Interface();
    g_Interface->Initialize(window);

    // Conectar ao daemon via IPC
    IPCClient::Connect("/data/local/tmp/storm_daemon.sock");

    auto lastFrame = std::chrono::high_resolution_clock::now();

    while (g_Running) {
        // Processar input Android
        AndroidInput::ProcessEvents();

        // Verificar shutdown
        if (g_Globals.General.ShutDown) {
            break;
        }

        // Sincronizar config com daemon
        IPCClient::SyncConfigToDaemon();
        IPCClient::SyncStateFromDaemon();

        // Atualizar tamanho da janela
        ImVec2 displaySize = Overlay::GetTargetWindowSize();
        if (g_Interface->ResizeWidth != 0 || g_Interface->ResizeHeight != 0) {
            g_Interface->ResizeWidth = g_Interface->ResizeHeight = 0;
        }

        // Handle menu key
        g_Interface->HandleMenuKey();

        // Novo frame ImGui
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplAndroid_NewFrame();  // MUDANÇA: Win32 → Android
        ImGui::NewFrame();

        {
            // Render ESP recebido do daemon (via IPC)
            // O daemon envia lista de jogadores; o painel desenha
            IPCClient::RenderESP();

            // Render menu ImGui
            g_Interface->RenderGui();
            NotifyManager::Render();

            // FOV Circles
            if (g_Globals.Misc.Screen.ShowAimbotFov) {
                // ... mesmo código do original ...
            }
            if (g_Globals.Misc.Screen.ShowSilentFov) {
                // ... mesmo código do original ...
            }
        }

        ImGui::EndFrame();
        ImGui::Render();

        // Render OpenGL
        Overlay::glClearTransparent();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        Overlay::glRefresh();

        // Frame limiter
        const double targetFPS = (g_Globals.General.ThreadDelay > 0) 
            ? static_cast<double>(g_Globals.General.ThreadDelay) : 60.0;
        const double frameDuration = 1000.0 / targetFPS;

        auto now = std::chrono::high_resolution_clock::now();
        double elapsed = std::chrono::duration<double, std::milli>(now - lastFrame).count();

        if (elapsed < frameDuration) {
            std::this_thread::sleep_for(
                std::chrono::milliseconds(static_cast<long long>(frameDuration - elapsed)));
        }
        lastFrame = std::chrono::high_resolution_clock::now();
    }

    // Cleanup
    if (g_Interface) {
        delete g_Interface;
        g_Interface = nullptr;
    }
    Overlay::ShutDown();
    IPCClient::Disconnect();
}

void PanelApp::RequestShutdown() {
    g_Running = false;
}
```

---

### PASSO 8: Criar `IPCClient.cpp` (comunicação Painel ↔ Daemon)

**Arquivo:** `Panel/IPC/IPCClient.cpp` e `Daemon/IPC/IPCServer.cpp`

**Protocolo:** Unix Domain Socket com estruturas binárias (mesmo formato do SharedMemory.h original).

```cpp
// Panel/IPC/IPCClient.cpp
#include "IPCClient.hpp"
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <android/log.h>

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "StormIPC", __VA_ARGS__)

static int g_Socket = -1;

bool IPCClient::Connect(const char* socketPath) {
    g_Socket = socket(AF_UNIX, SOCK_STREAM, 0);
    if (g_Socket < 0) return false;

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, socketPath, sizeof(addr.sun_path) - 1);

    if (connect(g_Socket, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        close(g_Socket);
        g_Socket = -1;
        return false;
    }

    LOGI("Conectado ao daemon IPC");
    return true;
}

void IPCClient::SyncConfigToDaemon() {
    if (g_Socket < 0) return;

    // Enviar g_Globals como IPC_STATE
    IPC_STATE state;
    memset(&state, 0, sizeof(state));
    state.Magic = IPC_MAGIC_STATE;
    state.ConfigSeq = 1; // incrementar quando mudar

    // Copiar configurações relevantes de g_Globals para state
    // ... (mapear todos os campos de g_Globals para WEB_FEATURES)

    send(g_Socket, &state, sizeof(state), 0);
}

void IPCClient::SyncStateFromDaemon() {
    if (g_Socket < 0) return;

    IPC_STATE state;
    int n = recv(g_Socket, &state, sizeof(state), MSG_DONTWAIT);
    if (n == sizeof(state) && state.Magic == IPC_MAGIC_STATE) {
        // Atualizar estado do jogo no painel
        // EnemyCount, ClosestEnemyDist, etc.
    }
}

void IPCClient::Disconnect() {
    if (g_Socket >= 0) {
        close(g_Socket);
        g_Socket = -1;
    }
}
```

---

### PASSO 9: Mover e Adaptar o Daemon

**Arquivos movidos para `Daemon/`:**
- `EspLines/Main/Draw/Draw.cpp/.hpp` → `Daemon/Draw/`
- `EspLines/Main/Memory/Memory.cpp/.hpp` → `Daemon/Memory/`
- `EspLines/Main/Unity/Unity.cpp/.hpp` → `Daemon/Unity/`
- `EspLines/Main/Offsets/Offsets.hpp` → `Daemon/Offsets/`
- `src/Cheat/WebPanel.cpp/.hpp` → `Daemon/WebPanel/`
- `src/Cheat/saveconfig.cpp` → `Daemon/Config/` (ou Shared)

**Mudanças no `Memory.cpp`:**

```cpp
// PROCURAR e REMOVER (Win32 específico):
#include <Windows.h>
#include <TlHelp32.h>
#include <Lazyimporter.hpp>

// SUBSTITUIR por (Android):
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>

// PROCURAR e REMOVER (BstkVMM.dll — não existe no Android):
HMODULE bstkVMM = GetModuleHandleA("BstkVMM.dll");
PGMR3PhysRead = GetProcAddress(bstkVMM, "PGMR3PhysReadExternal");
// ... etc

// SUBSTITUIR por (Android memory access):
// No Android, o daemon precisa de root para acessar /proc/<pid>/mem
// ou usar ptrace, ou ser um kernel module
// Adaptação para acesso via /proc/mem (requer root):

bool Memory::ReadBuffer(uintptr_t address, void* buffer, size_t size) {
    if (memFd < 0) return false;
    lseek64(memFd, address, SEEK_SET);
    return read(memFd, buffer, size) == (ssize_t)size;
}

bool Memory::WriteBuffer(uintptr_t address, const void* buffer, size_t size) {
    if (memFd < 0) return false;
    lseek64(memFd, address, SEEK_SET);
    return write(memFd, buffer, size) == (ssize_t)size;
}
```

**Nota:** O acesso à memória do FreeFire no Android é DIFERENTE do Windows. No Windows, o cheat lê a memória do emulador Android (BlueStacks) via VMM. No Android nativo, o jogo roda nativamente e o daemon precisa de:
1. **Root + ptrace** (`process_vm_readv` / `process_vm_writev`)
2. **Kernel module** (mais seguro, mas complexo)
3. **MemFD** (`/proc/<pid>/mem` com permissões adequadas)

**Mudanças no `Draw.cpp`:**

```cpp
// PROCURAR e REMOVER (Win32 input):
bool keyPressed = (GetAsyncKeyState(AimCfg.KeyBind) & 0x8000);

// SUBSTITUIR por (receber do painel via IPC):
// O daemon lê o estado das teclas do IPC_STATE enviado pelo painel
extern IPC_STATE g_DaemonIPCState;
bool keyPressed = g_DaemonIPCState.AimKeyPressed;

// PROCURAR e REMOVER (Win32 threads):
HANDLE h = CreateThread(nullptr, 0, ReadLoopWrapper, nullptr, 0, nullptr);

// SUBSTITUIR por (pthread):
#include <pthread.h>
pthread_t thread;
pthread_create(&thread, nullptr, ReadLoopWrapper, nullptr);

// PROCURAR e REMOVER (Win32 mutex):
std::mutex m_Mutex;  // std::mutex é cross-platform — MANTER

// PROCURAR e REMOVER (GetTickCount64):
LONGLONG now = GetTickCount64();

// SUBSTITUIR por:
#include <time.h>
LONGLONG now = (LONGLONG)(clock_gettime_ns() / 1000000);
```

---

### PASSO 10: Adaptar `saveconfig.cpp` para Android

**Arquivo:** `Shared/Config/saveconfig.cpp` (MOVIDO de `src/Cheat/saveconfig.cpp`)

```cpp
// PROCURAR e REMOVER (Win32 paths):
#include <ShlObj.h>
#include <filesystem>
#pragma comment(lib, "Shell32.lib")

// SUBSTITUIR por (Android paths):
#include <sys/stat.h>
#include <unistd.h>

// PROCURAR e REMOVER:
static std::wstring DefaultPath() {
    std::wstring base = knownFolder(FOLDERID_LocalAppData);
    // ...
}

// SUBSTITUIR por:
static std::string DefaultPath() {
    // Android: /data/data/<package>/files/config.dat
    return "/data/data/com.stormcheats/files/config.dat";
}

// PROCURAR e REMOVER (wstring):
static bool SaveAs(const std::wstring& path)

// SUBSTITUIR por:
static bool SaveAs(const std::string& path)

// PROCURAR e REMOVER (knownFolder):
static std::wstring knownFolder(REFKNOWNFOLDERID id)

// SUBSTITUIR por:
static void ensureParent(const std::string& file) {
    size_t pos = file.find_last_of('/');
    if (pos != std::string::npos) {
        std::string dir = file.substr(0, pos);
        mkdir(dir.c_str(), 0755);
    }
}
```

---

### PASSO 11: Criar `AndroidManifest.xml`

```xml
<?xml version="1.0" encoding="utf-8"?>
<manifest xmlns:android="http://schemas.android.com/apk/res/android"
    package="com.stormcheats"
    android:versionCode="1"
    android:versionName="1.0">

    <uses-permission android:name="android.permission.SYSTEM_ALERT_WINDOW" />
    <uses-permission android:name="android.permission.FOREGROUND_SERVICE" />
    <uses-permission android:name="android.permission.INTERNET" />
    <uses-permission android:name="android.permission.ACCESS_NETWORK_STATE" />

    <application
        android:label="Storm Cheats"
        android:theme="@android:style/Theme.NoDisplay">

        <!-- Activity principal (invisível, inicia o overlay) -->
        <activity
            android:name=".MainActivity"
            android:theme="@android:style/Theme.Translucent.NoTitleBar"
            android:launchMode="singleInstance"
            android:excludeFromRecents="true"
            android:exported="true">
            <intent-filter>
                <action android:name="android.intent.action.MAIN" />
                <category android:name="android.intent.category.LAUNCHER" />
            </intent-filter>
        </activity>

        <!-- Service do overlay (painel ImGui) -->
        <service
            android:name=".OverlayService"
            android:permission="android.permission.SYSTEM_ALERT_WINDOW"
            android:foregroundServiceType="mediaProjection" />

        <!-- Service do daemon (backend) -->
        <service
            android:name=".DaemonService"
            android:process=":daemon"
            android:foregroundServiceType="dataSync" />

    </application>
</manifest>
```

---

### PASSO 12: Criar `MainActivity.java`

```java
package com.stormcheats;

import android.app.Activity;
import android.content.Intent;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.provider.Settings;
import android.widget.Toast;

public class MainActivity extends Activity {
    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        // Solicitar permissão de overlay
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M) {
            if (!Settings.canDrawOverlays(this)) {
                Intent intent = new Intent(Settings.ACTION_MANAGE_OVERLAY_PERMISSION,
                    Uri.parse("package:" + getPackageName()));
                startActivityForResult(intent, 1);
                return;
            }
        }

        startServices();
        finish();
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        if (requestCode == 1) {
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M) {
                if (Settings.canDrawOverlays(this)) {
                    startServices();
                } else {
                    Toast.makeText(this, "Permissão de overlay necessária", Toast.LENGTH_LONG).show();
                }
            }
        }
        finish();
    }

    private void startServices() {
        // Iniciar daemon
        startService(new Intent(this, DaemonService.class));

        // Iniciar overlay (painel)
        startService(new Intent(this, OverlayService.class));
    }
}
```

---

### PASSO 13: Criar `OverlayService.java`

```java
package com.stormcheats;

import android.app.Service;
import android.content.Intent;
import android.graphics.PixelFormat;
import android.os.IBinder;
import android.view.SurfaceView;
import android.view.View;
import android.view.WindowManager;
import android.view.WindowManager.LayoutParams;

public class OverlayService extends Service {
    private WindowManager windowManager;
    private SurfaceView surfaceView;

    @Override
    public void onCreate() {
        super.onCreate();

        windowManager = (WindowManager) getSystemService(WINDOW_SERVICE);

        // Criar SurfaceView transparente para renderização OpenGL
        surfaceView = new SurfaceView(this);
        surfaceView.setZOrderOnTop(true);
        surfaceView.getHolder().setFormat(PixelFormat.TRANSLUCENT);

        // Layout params: overlay transparente, full screen
        LayoutParams params = new LayoutParams(
            LayoutParams.MATCH_PARENT,
            LayoutParams.MATCH_PARENT,
            LayoutParams.TYPE_APPLICATION_OVERLAY,
            LayoutParams.FLAG_NOT_FOCUSABLE 
                | LayoutParams.FLAG_NOT_TOUCH_MODAL
                | LayoutParams.FLAG_LAYOUT_NO_LIMITS,
            PixelFormat.TRANSLUCENT
        );

        windowManager.addView(surfaceView, params);

        // Iniciar native panel (C++)
        surfaceView.getHolder().addCallback(new android.view.SurfaceHolder.Callback() {
            @Override
            public void surfaceCreated(android.view.SurfaceHolder holder) {
                nativeStartPanel(holder.getSurface());
            }
            @Override
            public void surfaceChanged(android.view.SurfaceHolder holder, int format, int w, int h) {
                nativeResize(w, h);
            }
            @Override
            public void surfaceDestroyed(android.view.SurfaceHolder holder) {
                nativeStopPanel();
            }
        });
    }

    @Override
    public void onDestroy() {
        nativeStopPanel();
        if (surfaceView != null) {
            windowManager.removeView(surfaceView);
        }
        super.onDestroy();
    }

    @Override
    public IBinder onBind(Intent intent) { return null; }

    // Native methods
    private native void nativeStartPanel(android.view.Surface surface);
    private native void nativeResize(int width, int height);
    private native void nativeStopPanel();

    static {
        System.loadLibrary("panel");
    }
}
```

---

## 5. Códigos Completos dos Arquivos Novos

### 5.1 `Panel/main.cpp` (Entry Point do Painel)

```cpp
#include <jni.h>
#include <android/native_window.h>
#include <android/native_window_jni.h>
#include "PanelApp.hpp"
#include <android/log.h>

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "StormNative", __VA_ARGS__)

extern "C" {
    JNIEXPORT void JNICALL
    Java_com_stormcheats_OverlayService_nativeStartPanel(JNIEnv* env, jobject thiz, jobject surface) {
        ANativeWindow* window = ANativeWindow_fromSurface(env, surface);
        if (window) {
            LOGI("Iniciando painel nativo");
            PanelApp::Run(window);
            ANativeWindow_release(window);
        }
    }

    JNIEXPORT void JNICALL
    Java_com_stormcheats_OverlayService_nativeResize(JNIEnv* env, jobject thiz, jint width, jint height) {
        PanelApp::OnResize(width, height);
    }

    JNIEXPORT void JNICALL
    Java_com_stormcheats_OverlayService_nativeStopPanel(JNIEnv* env, jobject thiz) {
        PanelApp::RequestShutdown();
    }
}
```

### 5.2 `Daemon/daemon_main.cpp` (Entry Point do Daemon)

```cpp
#include "DaemonApp.hpp"
#include <android/log.h>
#include <signal.h>

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "StormDaemon", __VA_ARGS__)

int main(int argc, char** argv) {
    LOGI("Daemon Storm Cheats iniciado");

    // Ignorar SIGPIPE (quebra de socket)
    signal(SIGPIPE, SIG_IGN);

    DaemonApp::Run();
    return 0;
}
```

### 5.3 `Daemon/DaemonApp.cpp`

```cpp
#include "DaemonApp.hpp"
#include "IPC/IPCServer.hpp"
#include "Memory/Memory.hpp"
#include "Draw/Draw.hpp"
#include <android/log.h>
#include <thread>

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "StormDaemon", __VA_ARGS__)

void DaemonApp::Run() {
    // Inicializar memória
    if (!Memory::Initialize()) {
        LOGE("Falha ao inicializar memória");
        return;
    }

    // Iniciar servidor IPC
    IPCServer::Start("/data/local/tmp/storm_daemon.sock");

    // Iniciar thread de leitura (mesmo padrão do original)
    Data::StartReadThread();

    // Loop principal do daemon
    while (!g_Globals.General.ShutDown) {
        // Processar comandos do painel
        IPCServer::ProcessCommands();

        // Sincronizar estado para o painel
        IPCServer::SyncState();

        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    // Cleanup
    Data::StopReadThread();
    IPCServer::Stop();
    Memory::Shutdown();
}
```

---

## 6. Tabela de Substituições

### 6.1 Windows → Android (Platform)

| Windows (Original) | Android (Novo) | Arquivo |
|-------------------|----------------|---------|
| `HWND` | `ANativeWindow*` | AndroidOverlay.hpp |
| `HDC` | `EGLDisplay` | AndroidOverlay.cpp |
| `HGLRC` | `EGLContext` | AndroidOverlay.cpp |
| `CreateWindowEx()` | `ANativeWindow_fromSurface()` | AndroidOverlay.cpp |
| `wglCreateContext()` | `eglCreateContext()` | AndroidOverlay.cpp |
| `wglMakeCurrent()` | `eglMakeCurrent()` | AndroidOverlay.cpp |
| `SwapBuffers()` | `eglSwapBuffers()` | AndroidOverlay.cpp |
| `SetLayeredWindowAttributes()` | Alpha via OpenGL (já existia) | — |
| `DwmExtendFrameIntoClientArea()` | Não necessário | — |
| `GetAsyncKeyState()` | `AInputQueue` / `AndroidInput` | AndroidInput.cpp |
| `PeekMessage()` | `ALooper_pollAll()` | PanelApp.cpp |
| `GetClientRect()` | `eglQuerySurface(EGL_WIDTH/HEIGHT)` | AndroidOverlay.cpp |
| `std::wstring` | `std::string` (UTF-8) | saveconfig.cpp |
| `FOLDERID_LocalAppData` | `/data/data/<pkg>/files/` | saveconfig.cpp |
| `CreateThread()` | `pthread_create()` | Draw.cpp |
| `WaitForSingleObject()` | `pthread_join()` | Draw.cpp |
| `InterlockedCompareExchange()` | `__atomic_compare_exchange_n()` | Vários |
| `GetTickCount64()` | `clock_gettime(CLOCK_MONOTONIC)` | Vários |
| `Sleep()` | `usleep()` / `nanosleep()` | Vários |
| `AllocConsole()` | `__android_log_print()` | Utils.cpp |
| `GetModuleHandleA()` | `dlopen()` / `dlsym()` | Memory.cpp |

### 6.2 Win32 → Android (ImGui Backends)

| Original | Substituição | Arquivo |
|----------|-------------|---------|
| `ImGui_ImplWin32_Init()` | `ImGui_ImplAndroid_Init()` | Interface.cpp |
| `ImGui_ImplWin32_NewFrame()` | `ImGui_ImplAndroid_NewFrame()` | PanelApp.cpp |
| `ImGui_ImplWin32_Shutdown()` | `ImGui_ImplAndroid_Shutdown()` | Interface.cpp |
| `ImGui_ImplWin32_WndProcHandler()` | `ImGui_ImplAndroid_HandleInputEvent()` | AndroidInput.cpp |

### 6.3 Memory Access (Windows → Android)

| Windows (BstkVMM) | Android (Root) | Arquivo |
|-------------------|----------------|---------|
| `PGMR3PhysReadExternal()` | `process_vm_readv()` | Memory.cpp |
| `PGMR3PhysWriteExternal()` | `process_vm_writev()` | Memory.cpp |
| `PGMPhysGCPtr2GCPhys()` | `/proc/<pid>/maps` parsing | Memory.cpp |
| `VMMGetCpuById()` | Não necessário | — |
| `TlsAlloc()` | `pthread_key_create()` | Memory.cpp |
| `TlsGetValue()` | `pthread_getspecific()` | Memory.cpp |
| `HeapAlloc()` | `malloc()` | Memory.cpp |

### 6.4 Network (Win32 → Android)

| Windows | Android | Arquivo |
|---------|---------|---------|
| `WSAStartup()` | Não necessário (Berkeley sockets) | WebPanel.cpp |
| `socket(AF_INET, ...)` | `socket(AF_INET, ...)` (mesmo) | WebPanel.cpp |
| `bind()` / `listen()` / `accept()` | Mesmas funções | WebPanel.cpp |
| `gethostname()` | `gethostname()` (mesmo) | WebPanel.cpp |
| `inet_ntop()` | `inet_ntop()` (mesmo) | WebPanel.cpp |

---

## 7. Build e Deploy

### 7.1 Build

```bash
# 1. Configurar NDK
export ANDROID_NDK_HOME=/path/to/android-ndk

# 2. Build do projeto
./gradlew assembleDebug

# 3. O APK estará em:
# app/build/outputs/apk/debug/app-debug.apk
```

### 7.2 Deploy

```bash
# Instalar APK
adb install app-debug.apk

# Conceder permissão de overlay (manual ou via adb)
adb shell appops set com.stormcheats SYSTEM_ALERT_WINDOW allow

# Iniciar
adb shell am start -n com.stormcheats/.MainActivity

# Para daemon com root (se necessário para memória):
adb shell su -c "/data/data/com.stormcheats/lib/libdaemon.so"
```

### 7.3 Estrutura do APK Final

```
app-debug.apk
├── lib/
│   ├── arm64-v8a/
│   │   ├── libpanel.so      → Painel C++/ImGui
│   │   └── libdaemon.so     → Daemon C++ (backend)
│   └── armeabi-v7a/
│       ├── libpanel.so
│       └── libdaemon.so
├── classes.dex              → Java (MainActivity, Services)
├── AndroidManifest.xml
└── res/
```

---

## 🎯 Resumo das Ações

| # | Ação | Prioridade | Complexidade |
|---|------|------------|--------------|
| 1 | Criar projeto Android com NDK + CMake | 🔴 Alta | Média |
| 2 | Mover `Globals.hpp` para `Shared/` | 🟡 Média | Baixa |
| 3 | Criar `AndroidOverlay.cpp` (EGL) | 🔴 Alta | Alta |
| 4 | Criar `AndroidInput.cpp` (touch) | 🔴 Alta | Média |
| 5 | Adaptar `Interface.cpp` (ImGui Android) | 🔴 Alta | Média |
| 6 | Adaptar `Fonts.cpp` (remover WIC) | 🟡 Média | Baixa |
| 7 | Criar protocolo IPC (`IPCProtocol.hpp`) | 🔴 Alta | Média |
| 8 | Criar `IPCClient.cpp` (painel) | 🔴 Alta | Média |
| 9 | Criar `IPCServer.cpp` (daemon) | 🔴 Alta | Média |
| 10 | Mover `Draw.cpp`, `Memory.cpp`, `Unity.cpp` para `Daemon/` | 🟡 Média | Baixa |
| 11 | Adaptar `Memory.cpp` para Android (root/ptrace) | 🔴 Alta | Alta |
| 12 | Adaptar `saveconfig.cpp` para Android paths | 🟢 Baixa | Baixa |
| 13 | Criar `MainActivity.java` + `OverlayService.java` | 🔴 Alta | Média |
| 14 | Criar `AndroidManifest.xml` | 🟡 Média | Baixa |
| 15 | Build e teste | 🔴 Alta | Alta |

---

## ⚠️ Notas Importantes

1. **Acesso à memória no Android** requer root ou um kernel module. O daemon deve ser executado com `su` ou como um serviço de sistema.

2. **Overlay no Android** usa `TYPE_APPLICATION_OVERLAY` (API 26+) ou `TYPE_SYSTEM_OVERLAY` (legacy). Requer permissão `SYSTEM_ALERT_WINDOW`.

3. **OpenGL ES vs OpenGL**: O painel usa OpenGL ES 3.0 no Android. Os shaders do ImGui (`#version 300 es`) precisam de `precision mediump float;`.

4. **Input touch**: O ImGui para Android (`imgui_impl_android.cpp`) já existe no repositório oficial do ImGui. Usar a implementação oficial.

5. **Fontes**: As fontes embutidas (bytes arrays) do original funcionam no Android sem mudanças. Apenas o carregamento de PNGs precisa de `stb_image` no lugar de WIC.

6. **WebPanel**: O servidor HTTP usa sockets Berkeley que são cross-platform. Poucas ou nenhuma mudança necessária no `WebPanel.cpp`.

7. **DiscordRPC**: Remover completamente. Não há suporte no Android.

8. **KeyAuth**: Pode ser mantido se a biblioteca suportar Android (curl/openssl). Caso contrário, mover autenticação para o daemon.

---

*Fim do guia de migração.*
