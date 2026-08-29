# ZmInternal — Free Fire External Cheat

> **Contexto para LLM:** Este documento descreve o estado atual do projeto e a arquitetura-alvo do porte para Android nativo. Sempre que iniciar uma nova sessão, leia este README primeiro para entender o contexto completo sem precisar reescanear o repositório.

---

## 1. Visão Geral

**ZmInternal** é um painel/cheat externo para **Free Fire** (versão Android `v7a`, 32-bit) que hoje opera no **PC atacando um emulador BlueStacks**. O projeto está sendo portado para rodar como **app Android nativo** (sem emulador), com root, dividido em duas camadas: um **Daemon** (root, memória) e um **Client** (overlay + lógica + render).

### 1.1 Estado Atual (PC / Emulador)

- **Plataforma:** Windows x64
- **Formato:** DLL injetada no processo do emulador BlueStacks
- **Ponto de entrada:** `dllmain.cpp` → `DllMain` → `Cheat::Initialize()`
- **Build system:** Visual Studio (`.vcxproj` / `.vcxproj.filters`)
- **Backend gráfico:** OpenGL 3 + Win32 (overlay transparente)
- **UI:** ImGui com backends `imgui_impl_win32.cpp` + `imgui_impl_opengl3.cpp`
- **Anti-capture:** `WDA_EXCLUDEFROMCAPTURE` via `SetWindowDisplayAffinity`
- **Auth:** Migrada para driver kernel (shared memory); painel web local opcional

### 1.2 Objetivo do Porte (Android Nativo)

- **Plataforma:** Android (armeabi-v7a, root)
- **Arquitetura:** Client/Daemon separados via IPC (socket Unix abstract)
- **Overlay:** Janela Java transparente (`FLAG_NOT_TOUCH_MODAL`) + ImGui renderizado via NDK/OpenGL ES
- **Memória:** Daemon único com `ptrace` / `process_vm_readv` / `process_vm_writev`
- **IPC:** Protocolo binário definido em `android/shared/IpcProtocol.h` (criado, ainda não commitado)

---

## 2. Arquitetura Atual (PC — DLL Injetada)

### 2.1 Ponto de Entrada e Ciclo de Vida

```
dllmain.cpp
  └─ DllMain(DLL_PROCESS_ATTACH)
       ├─ g_hModule = hModule
       ├─ Parse argumento de injeção (address → g_Globals.General.Local)
       ├─ DynamicStub::Initialize()
       ├─ CreateEventW("...")  → g_MainFinishedEvent
       └─ DynamicStub::CreateThreadWithDynamicStub(MainThread)
            └─ Cheat::Initialize()
                 ├─ Utils::EnableDebugPrivilege()
                 ├─ VehCpuHook::Initialize()      // VEH + HWBP
                 ├─ FullScreenFixHooks()          // PAGE_GUARD hooks
                 ├─ Overlay::Setup()              // Encontra janela do emulador
                 ├─ Overlay::Initialize()         // Cria janela overlay OpenGL
                 ├─ new Interface()               // ImGui context + estilo
                 ├─ Overlay::SetupWindowProcHook()  // Input forwarding
                 └─ Loop principal (PeekMessage + ImGui frame)
                      ├─ ProcessDriverCommands()   // Shared memory com driver
                      ├─ ApplyWebFeatures()        // Mirror do painel web
                      ├─ Chams::Enable/Disable()
                      ├─ Silent::Start/Stop()
                      ├─ WebPanel::Start/Stop()
                      ├─ Data::Draw()              // ESP + aimbot + exploits
                      └─ ImGui render + SwapBuffers
```

### 2.2 Módulos Principais

#### A. Memória — `EspLines/Main/Memory/`

| Arquivo | Função |
|---------|--------|
| `Memory.cpp` / `Memory.hpp` | Leitura/escrita de memória do guest Linux via exports da `BstkVMM.dll` |
| `EmulatorEnvironment.cpp` | Detecção de versão do emulador, ABI (x86/x64), offsets de kernel |
| `MemoryExternal.h` | Template wrappers `Read<T>` / `Write<T>` |

**Técnica de leitura (PC):**
- Resolve exports da `BstkVMM.dll`: `PGMR3PhysReadExternal`, `PGMR3PhysWriteExternal`, `GCPhys2CCPtr`, `GCPhys2CCPtrRO`, `PGMPhysGCPtr2GCPhys`, `VMMGetCpuById`, `PGMR3PhysTlbGCPhys2Ptr`, `PGMPhysReleasePageMappingLock`
- Faz **page-table walk manual** (32-bit e 64-bit) com CR3 capturado
- CR3 é obtido caminhando `task_struct` do kernel Linux guest via `init_task` → `mm` → `pgd`
- **SoftTLB por thread** (`TlsAlloc`) para cachear traduções VA→PA
- **Anti-tamper:** `VALIDATE()` verifica prólogos das funções VMM; em mismatch, loga e segue em modo degradado (nunca crasha o jogo)
- **Revalidação periódica:** `RefreshCR3()` a cada ~64 iterações do ReadLoop

#### B. Leitura de Dados do Jogo — `EspLines/Main/Draw/Draw.cpp`

| Arquivo | Função |
|---------|--------|
| `Draw.cpp` / `Draw.hpp` | Loop de leitura (`ReadLoop`) + loop de render (`Draw`) |
| `Skeleton.cpp` / `Skeleton.hpp` | Desenho de skeleton dos players |
| `Silent.cpp` / `Silent.hpp` | Silent aim (thread separada) |
| `Weapon/NameGun.cpp` | Lookup de nomes/ícones de armas |

**ReadLoop:**
- Thread dedicada (`DynamicStub::CreateThreadWithDynamicStub`)
- Templates instanciados para 4 combinações: `<N32,V31>` × `{true,false}²`
- Lê cadeia IL2CPP: `GameFacade` → `AccessClass` → `MatchGame` → `Match` → `EntityList`
- Extrai: posição, saúde, arma, nome, time, knocked, skeleton (UMAData)
- **Snapshot congelado:** mantém último estado bom por até 3s (anti-flicker)
- **Carry-over de entidades:** entidades que passaram nos checks fundamentais mas falharam na leitura transitória herdam o último estado bom
- **Watchdog:** se snapshot não fica fresco por >1.5s, aciona `RefreshCR3`; se >4s, `RestartAsync()`
- **Empty-clear:** só limpa snapshot após 25s de frames vazios sustentados
- **Lobby-clear:** só limpa snapshot após 30s sem `Match` válido

**Draw (render loop):**
- Roda no thread principal (overlay OpenGL)
- Reprojeta posições de mundo com view matrix ao vivo (relida a cada frame)
- Se view matrix inválida, usa snapshot congelado (posições de tela)
- **Isolamento por entidade:** exceção no desenho de uma entidade não aborta o frame
- **Exceções capturadas:** tanto `ReadLoop` quanto `Draw` têm `try/catch(...)` para nunca morrerem

#### C. ESP / Visuals

| Feature | Descrição |
|---------|-----------|
| Box | Full, Corner, Filled |
| HealthBar | Left, Right, Top, Bottom, Text |
| SnapLines | Top, Bottom, Center |
| Skeleton | Via UMAData + transform chain |
| Weapon | Texto e/ou ícone (FontAwesome) |
| Distance | Em metros |
| Name | Nick do player (ou "BOT") |
| Watermark | "STORM CHEATS" |
| Enemy Counter | Contador de inimigos visíveis |

#### D. Aimbot

| Modo | Descrição |
|------|-----------|
| BoneSwap (`aimtype==0`) | Troca ponteiros de bone (neck ↔ hip) no transform do alvo |
| Rage (`aimtype==1`) | Thread dedicada que escreve `m_AimRotation` a cada 1µs |
| Magnet (`aimmagnect`) | Thread TIME_CRITICAL que sobrescreve posição do bone root |
| Ghost | Congela posição do player local (`m_WaitForForceSync`) |

**Rage Aimbot:**
- Delay configurável (PeitosIndex: 0-5)
- Modifica `m_EAimAssit` temporariamente
- Re-lê HP do alvo a cada iteração (sai se morrer)
- `PraCima`: após kill, dá pitch up controlado via quaternion
- FloodReset RAII garante que `s_AimFloodRunning` nunca fique preso em `true`

#### E. Silent Aim

- Thread separada (`Silent::Start/Stop`)
- Alvo independente do aimbot (próprio FOV + MaxDistance)
- Sem VisibleCheck / IgnoreKnocked / IgnoreBots do aimbot

#### F. Exploits de Player

| Exploit | Mecanismo |
|---------|-----------|
| NoRecoil | Sobrescreve `tangentTheta` no `FireComponent` |
| FireDelay | `PrefireDelay = 0.0f` |
| MoreDamage | `FullDamageDistance = 400.0f` |
| Aimlock | `m_FireDuration = -3.0f` |
| AimLock2x | Zera `m_fAimAssistCurrentLerpTime` ao sighting |
| AimbotAwm | Troca `IntWeaponType` de sniper para rifle (auto-aim) |
| FastMedkit | Reduz `m_EatSpeedScale` |
| TelaParada | Manipula `UserControlHandler` (dash por joystick) |
| AtributarArma | Reduz `m_FireIntervalScale` (4 níveis) |
| BugarPixel | `ShootTraceAdjustmentDistanceThreshold = 0.0f` |
| Precision | Sensibilidades extremas no `GameVarDef` |
| BackJump | Desabilita `EnableAccelerationOnFalling` |
| SocoLonge | `WeaponParams.Range = 3.1f` |
| SpinBot | Rotaciona quaternion do root transform + parent chain |

#### G. Chams

- `Chams.cpp` / `Chams.hpp`
- Hook via **hardware breakpoints (DR registers)** em funções GL do emulador
- 2 slots DR usados; gerenciado por `VehCpuHook`
- Modo agressivo toggleável

#### H. Anti-Tamper / Bypass

| Componente | Técnica |
|------------|---------|
| `VehCpuHook` | VEH + HWBP (DR0-DR3) para hooks de execução |
| `Vehpageguardhook` | VEH + PAGE_GUARD para hooks de acesso |
| `closehandle.cpp` | Fecha handles suspeitos |
| `StealthyOpenProcess.cpp` | Abertura stealth de processos |
| `DynamicStub.cpp` | Criação de threads com stub dinâmico (anti-detecção) |
| `FullScreenFix.cpp` | Hooks PAGE_GUARD para corrigir fullscreen do emulador |

#### I. Overlay / Render

| Arquivo | Função |
|---------|--------|
| `Overlay.cpp` / `Overlay.hpp` | Janela Win32 transparente + OpenGL context |
| `Render.cpp` / `Render.hpp` | Helpers de lookup de janela |
| `Interface.cpp` / `Interface.hpp` | Menu ImGui completo + input handling |
| `Fonts.cpp` / `Fonts.hpp` | Fontes embeddadas (Verdana, Inter, GFF, IconWeapon, FontAwesome) |

**Overlay:**
- Janela `WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE | WS_EX_LAYERED`
- OpenGL com alpha blending, sem depth test
- `DwmExtendFrameIntoClientArea` para vidro total
- Input forwarding via `WindowProc` hook
- `SetWindowDisplayAffinity(..., WDA_EXCLUDEFROMCAPTURE)` para anti-capture

#### J. Driver / Shared Memory

| Arquivo | Função |
|---------|--------|
| `SharedMemory.h` | Estruturas de shared memory com driver kernel |
| `Cheat.cpp` | `ProcessDriverCommands()`, `ApplyWebFeatures()`, `SetStreamMode()` |

**Estruturas:**
- `SHARED_COMMAND_BUFFER` (Magic=`CMD_MAGIC=0x444D435A`): comandos driver→DLL (STREAM, UNLOAD)
- `SHARED_WEB_STATE` (Magic=`WEB_MAGIC=0x42455742`): mirror de `g_Globals` + auth
- Auth 100% kernel; DLL apenas lê `Authenticated` e aplica features

#### K. WebPanel

- Servidor HTTP local (porta configurável, hoje desativado por padrão)
- `WebPanel::Start/Stop()` idempotentes
- Ações one-shot: save/load config, restart

#### L. Loader

- Diretório `Loader/` (estrutura não detalhada no repo atual)
- Injeta DLL criptografada no processo do emulador
- Comunica com driver para validação de licença

---

## 3. Estrutura de Diretórios Atual

```
zimo-external-mobile/
├── dllmain.cpp                          # Ponto de entrada DLL (Windows)
├── logo.c                               # Logo embeddado
├── ZmInternal.vcxproj                   # Projeto Visual Studio
├── ZmInternal.vcxproj.filters           # Filtros do VS
├── .gitignore
├── README.md                            # Este arquivo
│
├── src/
│   ├── Includes.hpp                     # Includes globais
│   ├── Cheat/
│   │   ├── Cheat.cpp / Cheat.hpp        # Inicialização e loop principal
│   │   ├── Globals.hpp                  # Estrutura g_Globals (configurações)
│   │   ├── saveconfig.cpp               # Save/load de config JSON
│   │   ├── SharedMemory.h               # Shared memory com driver
│   │   ├── FullScreenFix.cpp / .hpp     # Hooks de fullscreen
│   │   └── WebPanel.hpp                 # (referenciado, não no filters)
│   ├── Render/
│   │   ├── Fonts/
│   │   │   ├── Fonts.cpp / Fonts.hpp
│   │   │   └── Bytes/
│   │   │       ├── IconsFontAwesome6.h
│   │   │       └── IconsFontAwesome6Brands.h
│   │   ├── Interface/
│   │   │   ├── Interface.cpp / Interface.hpp
│   │   │   └── unpacker.h
│   │   └── Overlay/
│   │       ├── Overlay.cpp / Overlay.hpp
│   │       └── Render/
│   │           ├── Render.cpp / Render.hpp
│   ├── Utils/
│   │   └── Utils.cpp / Utils.hpp
│   └── DynamicStub/
│       ├── DynamicStub.cpp / DynamicStub.hpp
│
├── EspLines/                            # Lógica do jogo (IL2CPP)
│   ├── Main/
│   │   ├── Unity/
│   │   │   ├── Unity.cpp / Unity.hpp     # Wrappers IL2CPP (Transform, etc.)
│   │   │   └── UTF/
│   │   │       └── UTF8.cpp / UTF8.hpp   # Leitura de strings UTF-8/16
│   │   ├── Memory/
│   │   │   ├── Memory.cpp / Memory.hpp   # Engine de memória VMM
│   │   │   ├── MemoryExternal.h          # Template wrappers
│   │   │   └── EmulatorEnvironment.cpp / .hpp
│   │   ├── Draw/
│   │   │   ├── Draw.cpp / Draw.hpp       # ESP + aimbot + exploits
│   │   │   ├── Silent.cpp / Silent.hpp   # Silent aim
│   │   │   ├── Skeleton.cpp / Skeleton.hpp
│   │   │   └── Weapon/
│   │   │       └── NameGun.cpp / NameGun.h
│   │   └── Offsets/
│   │       ├── Offsets.cpp / Offsets.hpp # Offsets IL2CPP + GameConfig
│   ├── Math/
│   │   ├── Math.cpp / Math.hpp
│   │   ├── MathUtils.hpp
│   │   ├── Vectors/
│   │   │   ├── Vector2.cpp / Vector2.hpp
│   │   │   ├── Vector3.cpp / Vector3.hpp
│   │   │   └── Vector4.cpp / Vector4.hpp
│   │   └── Quaternion/
│   │       └── Quaternion.cpp / Quaternion.hpp
│
├── bypass/
│   ├── closehandle.cpp
│   └── StealthyOpenProcess.cpp / .h
│
├── Chams/
│   └── Chams.cpp / Chams.hpp
│
├── ext/
│   ├── Dependencies/
│   │   ├── ImGui/                         # ImGui core + backends Win32/OpenGL3
│   │   │   ├── imgui.cpp, imgui_draw.cpp, imgui_tables.cpp, imgui_widgets.cpp
│   │   │   ├── imgui_impl_win32.cpp / .h
│   │   │   ├── imgui_impl_opengl3.cpp / .h
│   │   │   ├── imgui_freetype.cpp / .h
│   │   │   ├── Custom.cpp / Custom.hpp
│   │   │   ├── imspinner.h
│   │   │   └── libs/GLFW/
│   │   ├── Notify/
│   │   │   └── Notify.cpp / Notify.hpp
│   │   ├── VehCpuHook/
│   │   │   └── VehCpuHook.cpp / VehCpuHook.hpp
│   │   ├── VehPGDHook/
│   │   │   └── Vehpageguardhook.cpp / Vehpageguardhook.hpp
│   │   ├── GL/
│   │   │   ├── glext.h
│   │   │   └── khrplatform.h
│   │   ├── XorStr.hpp
│   │   └── Lazyimporter.hpp
│   └── Auth/
│       └── LicenseValidator.h             # (legado, auth migrada pro driver)
│
├── Loader/                                # Loader externo (DLL criptografada)
│
└── dist/                                  # Artefatos de release
    └── HardwareMonitor-Release-x64.zip
```

---

## 4. Arquitetura-Alvo (Android Nativo)

### 4.1 Visão de Alto Nível

```
┌─────────────────────────────────────────────────────────────┐
│                      Android OS (root)                       │
│  ┌─────────────────┐    ┌─────────────────────────────────┐  │
│  │   Daemon (root) │    │         Client (app)            │  │
│  │   C++ nativo    │◄──►│  Java overlay + NDK/OpenGL ES  │  │
│  │                 │IPC │                                 │  │
│  │  • ptrace       │    │  • ImGui render                │  │
│  │  • process_vm_* │    │  • Input touch dispatch        │  │
│  │  • /proc/pid/mem│    │  • Lógica ESP/aimbot           │  │
│  │  • Leitura IL2CPP│   │  • Recebe dados via IPC        │  │
│  └─────────────────┘    └─────────────────────────────────┘  │
│           ▲                              ▲                   │
│           │                              │                   │
│           └────────── Free Fire ─────────┘                   │
│              (com.dts.freefireth / freefiremax)              │
└─────────────────────────────────────────────────────────────┘
```

### 4.2 Separação de Responsabilidades

| Camada | Responsabilidade | NÃO faz |
|--------|------------------|---------|
| **Daemon** | Único que lê/escreve memória do jogo; resolve offsets; faz cache de entidades; expõe dados via IPC | Não renderiza; não tem UI; não processa input |
| **Client** | Recebe snapshot de entidades via IPC; faz lógica de ESP/aimbot; renderiza ImGui; processa input touch | Nunca lê/escreve memória do jogo diretamente |

### 4.3 IPC — Protocolo Binário

**Status:** `android/shared/IpcProtocol.h` foi criado nesta sessão, mas ainda **não está commitado** no repositório.

O protocolo deve ser:
- **Socket Unix abstract** (`\0zm_internal_ipc`) — não requer permissões de filesystem
- **Binário, little-endian** — mínimo overhead
- **Mensagens tipadas** com magic + version + checksum
- **Tipos principais:**
  - `IPC_MSG_HEARTBEAT` — keepalive
  - `IPC_MSG_SNAPSHOT` — snapshot de entidades (pos, hp, arma, nome, etc.)
  - `IPC_MSG_CONFIG` — configurações do client → daemon (FOV, distância, etc.)
  - `IPC_MSG_COMMAND` — comandos one-shot (restart, save, etc.)
  - `IPC_MSG_ACK` — confirmação

**Requisitos de performance:**
- Snapshot deve caber em ~64KB (≤200 entidades)
- Frequência: 60-240 Hz (configurável, igual ao `ThreadDelay` atual)
- Latência alvo: <1ms round-trip

### 4.4 Estrutura de Diretórios Alvo

```
zimo-external-mobile/
│
├── README.md                              # Este documento
├── .gitignore
│
# ─── CÓDIGO LEGADO (PC) — mantido para referência ───
├── legacy/
│   ├── dllmain.cpp
│   ├── ZmInternal.vcxproj
│   ├── src/
│   ├── EspLines/
│   ├── bypass/
│   ├── Chams/
│   ├── ext/
│   ├── Loader/
│   └── dist/
│
# ─── ANDROID (novo) ───
├── android/
│   ├── CMakeLists.txt                     # Build NDK unificado
│   ├── build.gradle                       # (opcional, se usar gradle)
│   │
│   ├── shared/
│   │   └── IpcProtocol.h                  # Protocolo binário IPC
│   │                                      # (criado, PENDENTE DE COMMIT)
│   │
│   ├── daemon/
│   │   ├── src/
│   │   │   ├── main.cpp                   # Entry point do daemon
│   │   │   ├── daemon.cpp / daemon.hpp    # Loop principal + IPC server
│   │   │   ├── memory/
│   │   │   │   ├── memory.cpp / memory.hpp     # ptrace / process_vm_readv
│   │   │   │   ├── offsets.cpp / offsets.hpp   # Offsets IL2CPP (portado de EspLines/Main/Offsets)
│   │   │   │   └── il2cpp.cpp / il2cpp.hpp     # Wrappers IL2CPP (portado de EspLines/Main/Unity)
│   │   │   ├── game/
│   │   │   │   ├── entity.cpp / entity.hpp     # Estrutura Entity + snapshot builder
│   │   │   │   ├── readloop.cpp / readloop.hpp # Loop de leitura (portado de Draw.cpp)
│   │   │   │   └── resolver.cpp / resolver.hpp # Resolve base addresses, CR3 equivalente
│   │   │   └── ipc/
│   │   │       ├── server.cpp / server.hpp     # Unix abstract socket server
│   │   │       └── protocol.cpp / protocol.hpp # Serialização/deserialização
│   │   └── jni/
│   │       └── Android.mk                 # (alternativa ao CMake)
│   │
│   └── client/
│       ├── src/
│       │   ├── main.cpp                   # Entry point NDK
│       │   ├── client.cpp / client.hpp    # Loop principal + IPC client
│       │   ├── render/
│       │   │   ├── renderer.cpp / renderer.hpp   # OpenGL ES + ImGui
│       │   │   ├── overlay.cpp / overlay.hpp     # SurfaceFlinger overlay / Activity transparente
│       │   │   └── fonts.cpp / fonts.hpp         # Fontes embeddadas (portado)
│       │   ├── logic/
│       │   │   ├── esp.cpp / esp.hpp           # Lógica ESP (reprojeta snapshot)
│       │   │   ├── aimbot.cpp / aimbot.hpp     # Seleção de alvo + aimbot
│       │   │   ├── silent.cpp / silent.hpp     # Silent aim
│       │   │   └── exploits.cpp / exploits.hpp # Exploits de player
│       │   ├── ui/
│       │   │   ├── interface.cpp / interface.hpp  # Menu ImGui (portado de src/Render/Interface)
│       │   │   └── input.cpp / input.hpp            # Touch input + dispatch
│       │   └── ipc/
│       │       ├── client_ipc.cpp / client_ipc.hpp  # Unix abstract socket client
│       │       └── protocol.cpp / protocol.hpp      # (compartilhado com daemon)
│       │
│       └── java/
│           └── com/zminternal/
│               └── OverlayActivity.java     # Activity transparente FLAG_NOT_TOUCH_MODAL
│
# ─── DEPENDÊNCIAS ───
├── third_party/
│   ├── imgui/                             # ImGui (backend OpenGL ES a ser adicionado)
│   ├── freetype/                          # (opcional, para fontes)
│   └── json/                              # nlohmann/json ou similar (config)
│
# ─── FERRAMENTAS ───
├── tools/
│   ├── offset_dumper/                     # Script para extrair offsets do APK
│   └── build_scripts/
│       ├── build_daemon.sh
│       └── build_client.sh
│
└── docs/
    ├── IPC_SPEC.md                        # Especificação detalhada do protocolo
    ├── OFFSETS.md                         # Tabela de offsets por versão do FF
    └── PORTING_GUIDE.md                   # Guia de porte de código legado
```

### 4.5 Mapeamento de Código Legado → Novo

| Código Legado (PC) | Destino Android | Observações |
|--------------------|-----------------|-------------|
| `EspLines/Main/Memory/Memory.cpp` | `android/daemon/src/memory/memory.cpp` | Substituir VMM por `ptrace`/`process_vm_readv` |
| `EspLines/Main/Memory/EmulatorEnvironment.cpp` | `android/daemon/src/memory/resolver.cpp` | Detectar versão do FF via `/proc/pid/maps` |
| `EspLines/Main/Offsets/Offsets.cpp` | `android/daemon/src/memory/offsets.cpp` | Mesma lógica, paths de memória nativa |
| `EspLines/Main/Unity/Unity.cpp` | `android/daemon/src/memory/il2cpp.cpp` | Wrappers de leitura IL2CPP |
| `EspLines/Main/Draw/Draw.cpp` (ReadLoop) | `android/daemon/src/game/readloop.cpp` | Extrair apenas a leitura; remover render |
| `EspLines/Main/Draw/Draw.cpp` (Draw) | `android/client/src/logic/esp.cpp` | Reprojeta snapshot recebido via IPC |
| `src/Render/Interface/Interface.cpp` | `android/client/src/ui/interface.cpp` | Portar ImGui para OpenGL ES |
| `src/Render/Overlay/Overlay.cpp` | `android/client/src/render/overlay.cpp` | Substituir Win32 por SurfaceFlinger/Activity |
| `src/Render/Fonts/Fonts.cpp` | `android/client/src/render/fonts.cpp` | Mesmas fontes, backend diferente |
| `src/Cheat/Cheat.cpp` | Dividido entre daemon + client | Init do daemon ≠ init do client |
| `src/Cheat/Globals.hpp` | `android/shared/globals.hpp` | Estrutura compartilhada (subset) |
| `src/Cheat/SharedMemory.h` | **Remover** | Substituído por IPC socket |
| `bypass/` | `android/daemon/src/anti_detect/` | Adaptar técnicas para Linux/Android |
| `Chams/` | `android/client/src/logic/chams.cpp` | HWBP não funciona no Android; precisa de alternativa |
| `ext/Dependencies/ImGui/` | `third_party/imgui/` | Adicionar backend `imgui_impl_android.cpp` |
| `ext/Dependencies/VehCpuHook/` | **Remover** | VEH é Windows-specific |
| `ext/Dependencies/VehPGDHook/` | **Remover** | PAGE_GUARD VEH é Windows-specific |
| `DynamicStub/` | **Remover** | Stub dinâmico não necessário no Android |
| `Loader/` | `android/client/java/` | Substituir por app Android com overlay |

### 4.6 Considerações Técnicas do Porte

#### A. Leitura de Memória no Android

```cpp
// Em vez de VMM exports, usar process_vm_readv (não requer ptrace attach)
#include <sys/uio.h>

bool ReadProcessMemory(pid_t pid, uintptr_t addr, void* buf, size_t size) {
    struct iovec local = { buf, size };
    struct iovec remote = { (void*)addr, size };
    ssize_t n = process_vm_readv(pid, &local, 1, &remote, 1, 0);
    return n == (ssize_t)size;
}
```

- Fallback para `ptrace(PTRACE_PEEKDATA)` se `process_vm_readv` falhar
- Para escrita: `process_vm_writev` ou `ptrace(PTRACE_POKEDATA)`
- **CR3/page-table walk não é necessário** — estamos no mesmo espaço de endereçamento do processo (via kernel), não em um emulador aninhado

#### B. Overlay no Android

**Opção 1: Activity Transparente (recomendada)**
- Activity com tema `@android:style/Theme.Translucent.NoTitleBar`
- `WindowManager.LayoutParams.FLAG_NOT_TOUCH_MODAL`
- `WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE`
- Dispatch condicional de touch: se dentro do painel ImGui → consome; senão → passa

**Opção 2: SurfaceFlinger Overlay (mais stealth)**
- Criar Surface com `SurfaceComposerClient`
- Desenhar diretamente no framebuffer
- Mais complexo, mas menos detectável

**Opção 3: Native Activity + OpenGL ES**
- `ANativeActivity` com `EGLContext`
- Renderização full-screen transparente
- Input via `AInputQueue`

#### C. ImGui no Android

- Usar backend `imgui_impl_android.cpp` (existe na dock do ImGui)
- Ou implementar próprio baseado em `AInputEvent` para touch
- OpenGL ES 2.0/3.0 em vez de OpenGL 3.3
- Shaders precisam ser convertidos para GLSL ES

#### D. Input Touch

```cpp
// Pseudo-código para dispatch condicional
bool HandleTouchEvent(AInputEvent* event) {
    float x = AMotionEvent_getX(event, 0);
    float y = AMotionEvent_getY(event, 0);

    ImGuiIO& io = ImGui::GetIO();
    io.MousePos = ImVec2(x, y);

    if (IsInsideImGuiWindow(x, y)) {
        io.MouseDown[0] = true;
        return true; // Consumido
    }
    return false; // Passa para o jogo
}
```

#### E. Anti-Detecção no Android

- **Daemon:** nome de processo camuflado, esconde de `/proc` (magiskhide/zygisk)
- **Client:** app com nome benigno, sem ícone suspeito
- **IPC:** socket abstract com nome aleatório (gerado em runtime)
- **Memória:** evitar `ptrace` attach direto quando possível; preferir `process_vm_readv`
- **SELinux:** daemon pode precisar de contexto permissivo ou magisk module

---

## 5. Variáveis Globais Importantes

### 5.1 g_Globals (PC)

Definido em `src/Cheat/Globals.hpp`. Estrutura central de configuração:

```cpp
struct Globals {
    struct General {
        bool EnableFuncs;        // Master switch
        bool ShutDown;
        bool N32;               // true = 32-bit guest
        bool V31;               // true = FF v31
        int ThreadDelay;        // Target FPS (30-240)
        bool CaptureBypass;     // WDA_EXCLUDEFROMCAPTURE
        bool WebRemote;         // Painel web local
        char* Local;            // Path injetado pelo loader
    } General;

    struct Visuals {
        struct ESP { ... } ESP;
        struct Chams { bool Enabled, AggressiveMode; } Chams;
    } Visuals;

    struct AimBot { ... } AimBot;
    struct Silent { ... } Silent;

    struct Misc {
        struct Screen { ... } Screen;
        struct Exploits {
            struct LocalPlayer { ... } LocalPlayer;
        } Exploits;
    } Misc;
};
```

### 5.2 Offsets Principais (IL2CPP)

Definidos em `EspLines/Main/Offsets/Offsets.hpp`. Os mais críticos:

```cpp
namespace Offsets {
    extern uintptr_t LibIl2Cpp;           // Base do libil2cpp.so
    extern std::vector<uintptr_t> LibIl2CppCandidates;

    namespace GameFacade { ... }
    namespace MatchGame { ... }
    namespace Match { ... }
    namespace Player { ... }
    namespace PlayerNetwork { ... }
    namespace ReplicationEntity { ... }
    namespace Camera { ... }
    namespace CameraControllerManager { ... }
    namespace AvatarManager { ... }
    namespace UMAAvatarBase { ... }
    namespace UMAData { ... }
    namespace ShadowState { ... }
    namespace GameVarDef { ... }
    namespace PlayerAttributes { ... }
    namespace Weapon { ... }
    namespace WeaponParams { ... }
    namespace InventoryManager { ... }
    namespace AimAssistAutoLock { ... }
    namespace UserControlHandler { ... }
    namespace GetPosWorld { ... }
    namespace PlayerTransformNode { ... }
}
```

### 5.3 GameConfig

`Offsets::GameConfig()` é chamado após localizar `libil2cpp.so`. Faz:
1. Pattern scan / dlsym para encontrar `GameFacade_TypeInfo`
2. Preenche todos os offsets de classes IL2CPP
3. Diferencia versões do FF (v24 vs v31, 32 vs 64 bit)

---

## 6. Checklist de Porte

### Fase 1: Infraestrutura Android
- [ ] Criar estrutura de diretórios `android/`
- [ ] Setup CMakeLists.txt / build.gradle para compilar com NDK
- [ ] Portar ImGui para OpenGL ES (backend Android)
- [ ] Criar `OverlayActivity.java` transparente
- [ ] Implementar input touch dispatch condicional

### Fase 2: Daemon
- [ ] Implementar `memory.cpp` com `process_vm_readv`/`process_vm_writev`
- [ ] Portar `Offsets.cpp` para Android (paths de memória nativa)
- [ ] Portar `Unity.cpp` (wrappers IL2CPP)
- [ ] Implementar `readloop.cpp` (somente leitura, sem render)
- [ ] Implementar `server.cpp` (Unix abstract socket)
- [ ] Definir e implementar `IpcProtocol.h` completo

### Fase 3: Client
- [ ] Implementar `client_ipc.cpp` (conecta no daemon)
- [ ] Implementar `renderer.cpp` (OpenGL ES + ImGui)
- [ ] Portar `interface.cpp` (menu ImGui)
- [ ] Implementar `esp.cpp` (reprojeta snapshot recebido)
- [ ] Portar lógica de aimbot/silent/exploits (usando dados do IPC)

### Fase 4: Integração e Testes
- [ ] Testar IPC com stress (200 entidades, 240Hz)
- [ ] Testar overlay em diferentes dispositivos/resoluções
- [ ] Testar input touch com jogo rodando
- [ ] Otimizar latência do IPC
- [ ] Implementar anti-detecção básica

### Fase 5: Features Avançadas
- [ ] Portar Chams (alternativa a HWBP no Android)
- [ ] Auth/licença (substituir shared memory driver por outro mecanismo)
- [ ] WebPanel (HTTP server no client ou daemon)
- [ ] Save/load config no Android

---

## 7. Notas para o LLM

### 7.1 Contexto Sempre Relevante

- O projeto é um **cheat externo** para Free Fire. Nunca injeta código no jogo; apenas lê/escreve memória.
- A versão PC usa **VMM do BlueStacks** para ler memória do guest Linux. A versão Android lerá diretamente do processo nativo.
- O código é **extremamente defensivo**: try/catch em todo lugar, snapshot congelado, watchdog, auto-recovery. Isso é intencional e deve ser preservado.
- **N32** (32-bit) e **V31** (versão do jogo) são flags que controlam offsets e tamanhos de ponteiros. Sempre respeitar.
- O **IpcProtocol.h** já foi concebido mas ainda não está no repo. Quando o usuário mencionar, assumir que é o protocolo binário do IPC entre daemon e client.

### 7.2 Decisões de Design já Tomadas

1. **Daemon único para memória:** o client NUNCA lê/escreve memória do jogo. Tudo passa pelo daemon.
2. **IPC via socket Unix abstract:** mais rápido que binder, não requer permissões especiais além de root.
3. **Snapshot-based:** o daemon envia um snapshot completo de entidades a cada frame. O client não faz leituras adicionais.
4. **Overlay como Activity transparente:** mais simples que SurfaceFlinger, suficiente para o caso de uso.
5. **OpenGL ES 2.0/3.0:** compatibilidade máxima; shaders simples do ImGui funcionam sem modificação.

### 7.3 Armadilhas Comuns

- **Não confundir ponteiros 32-bit e 64-bit:** Free Fire v7a é 32-bit (`N32=true`), mas o sistema Android pode ser 64-bit. O daemon roda como 64-bit lendo processo 32-bit.
- **Offsets de IL2CPP mudam a cada update do jogo:** `Offsets.cpp` precisa de mecanismo de auto-update ou pattern scan.
- **process_vm_readv requer CAP_SYS_PTRACE:** no Android root, isso geralmente é garantido, mas verificar.
- **SELinux pode bloquear socket abstract:** testar com `setenforce 0` primeiro; depois criar regras adequadas.
- **ImGui no Android não tem mouse nativo:** implementar touch como mouse (touch down = mouse down, drag = mouse move).

---

## 8. Licença e Aviso

Este projeto é fornecido **exclusivamente para fins educacionais e de pesquisa em segurança de jogos**. O uso em ambientes de produção ou para violar termos de serviço de jogos online é **estritamente desencorajado** e pode resultar em banimentos permanentes ou ações legais.

---

*Última atualização: 2026-08-29*
*Autor: pertzx*
*Sessão de criação deste README: contexto completo do projeto ZmInternal para porte Android*
