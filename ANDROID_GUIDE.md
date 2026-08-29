# Guia Passo a Passo — Iniciar App Android ZmInternal

> Checklist completo do zero até o app rodando. Não detalha implementação, apenas lista o que fazer em ordem.

---

## Fase 0 — Preparação do Repositório

- [ ] Criar branch `android-port` ou trabalhar em `main` com backup do legado
- [ ] Criar pasta `legacy/` na raiz
- [ ] Mover todo código PC para `legacy/`:
  - `dllmain.cpp`, `.vcxproj`, `src/`, `EspLines/`, `bypass/`, `Chams/`, `ext/`, `Loader/`, `dist/`
- [ ] Criar pasta `android/` na raiz
- [ ] Criar `third_party/` na raiz
- [ ] Atualizar `.gitignore` para ignorar `build/`, `.gradle/`, `*.so`, `*.apk`

---

## Fase 1 — Estrutura de Pastas Android

- [ ] Criar `android/shared/`
- [ ] Criar `android/daemon/src/` com subpastas:
  - `memory/`, `game/`, `ipc/`
- [ ] Criar `android/client/src/` com subpastas:
  - `render/`, `logic/`, `ui/`, `ipc/`
- [ ] Criar `android/client/java/com/zminternal/`
- [ ] Criar `android/daemon/jni/` (fallback Android.mk)
- [ ] Criar `third_party/imgui/`
- [ ] Criar `third_party/json/`
- [ ] Criar `tools/build_scripts/`

---

## Fase 2 — Dependências

- [ ] Copiar código-fonte do ImGui para `third_party/imgui/`
- [ ] Adicionar `imgui_impl_android.cpp/.h` e `imgui_impl_opengl3.cpp/.h` (backends ES)
- [ ] Adicionar nlohmann/json em `third_party/json/`
- [ ] Verificar se FreeType é necessário; se sim, adicionar em `third_party/freetype/`

---

## Fase 3 — Build System

- [ ] Criar `android/CMakeLists.txt` (build unificado daemon + client)
- [ ] Criar `android/build.gradle` (wrapper Gradle, opcional)
- [ ] Criar `android/daemon/CMakeLists.txt`
- [ ] Criar `android/client/CMakeLists.txt`
- [ ] Criar `android/AndroidManifest.xml` para o client
- [ ] Criar `tools/build_scripts/build_daemon.sh`
- [ ] Criar `tools/build_scripts/build_client.sh`
- [ ] Testar compilação vazia (hello world) de daemon e client separadamente

---

## Fase 4 — IPC Protocol

- [ ] Criar `android/shared/IpcProtocol.h`
- [ ] Definir magic number, version, checksum
- [ ] Definir structs:
  - `IpcHeader` (magic, type, size, seq)
  - `IpcMsgHeartbeat`
  - `IpcMsgSnapshot` (array de entidades)
  - `IpcMsgConfig` (FOV, distância, toggles)
  - `IpcMsgCommand` (restart, save, etc.)
  - `IpcMsgAck`
- [ ] Definir `IPC_MSG_*` enum
- [ ] Definir socket path: `\0zm_internal_ipc` (abstract)
- [ ] Criar `android/shared/globals.hpp` (subset de `g_Globals` compartilhado)

---

## Fase 5 — Daemon (Servidor IPC)

### 5.1 Entry Point
- [ ] Criar `android/daemon/src/main.cpp`
- [ ] Parse args: `--target-pid`, `--socket-name`
- [ ] Loop principal: signal handler (SIGTERM, SIGINT)

### 5.2 Memória
- [ ] Criar `android/daemon/src/memory/memory.hpp`
- [ ] Criar `android/daemon/src/memory/memory.cpp`
  - Implementar `ReadProcessMemory()` via `process_vm_readv`
  - Implementar `WriteProcessMemory()` via `process_vm_writev`
  - Fallback `ptrace(PTRACE_PEEKDATA/POKEDATA)`
  - Template wrappers `Read<T>` / `Write<T>`
- [ ] Criar `android/daemon/src/memory/offsets.hpp`
- [ ] Criar `android/daemon/src/memory/offsets.cpp`
  - Portar offsets de `EspLines/Main/Offsets/Offsets.cpp`
  - Adaptar para paths de memória nativa Android
- [ ] Criar `android/daemon/src/memory/il2cpp.hpp`
- [ ] Criar `android/daemon/src/memory/il2cpp.cpp`
  - Portar wrappers de `EspLines/Main/Unity/Unity.cpp`
  - `Transform::GetPosition`, `GetHeadPosition`, `World2Screen`, etc.
- [ ] Criar `android/daemon/src/memory/resolver.hpp`
- [ ] Criar `android/daemon/src/memory/resolver.cpp`
  - Detectar PID do FF via `/proc/*/cmdline`
  - Encontrar base do `libil2cpp.so` via `/proc/pid/maps`
  - Detectar versão do jogo (pattern scan ou string check)

### 5.3 Game Logic (Leitura)
- [ ] Criar `android/daemon/src/game/entity.hpp`
  - Struct `EntitySnapshot` (pos, hp, arma, nome, time, knocked, skeleton bones)
- [ ] Criar `android/daemon/src/game/readloop.hpp`
- [ ] Criar `android/daemon/src/game/readloop.cpp`
  - Portar lógica do `ReadLoop` de `Draw.cpp`
  - Remover TODO código de render/desenho
  - Manter snapshot congelado, carry-over, watchdog
  - Adaptar `GetTickCount64()` para `clock_gettime(CLOCK_MONOTONIC)`
  - Adaptar `TlsAlloc` para `thread_local`
  - Adaptar `Interlocked*` para `std::atomic`
  - Thread dedicada para leitura

### 5.4 IPC Server
- [ ] Criar `android/daemon/src/ipc/server.hpp`
- [ ] Criar `android/daemon/src/ipc/server.cpp`
  - Criar socket Unix abstract (`socket(AF_UNIX, SOCK_SEQPACKET, 0)`)
  - `bind()` com `sun_path[0] = '\0'`
  - `listen()` + `accept()`
  - Thread para aceitar conexões
- [ ] Criar `android/daemon/src/ipc/protocol.hpp`
- [ ] Criar `android/daemon/src/ipc/protocol.cpp`
  - Serialização/deserialização binária (memcpy-friendly)
  - `SerializeSnapshot()`, `DeserializeConfig()`

### 5.5 Daemon Main Loop
- [ ] Criar `android/daemon/src/daemon.hpp`
- [ ] Criar `android/daemon/src/daemon.cpp`
  - Inicializar memória (encontrar PID, base, offsets)
  - Inicializar IPC server
  - Inicializar ReadLoop
  - Loop: recebe config do client → aplica → envia snapshot
  - Heartbeat a cada 1s

---

## Fase 6 — Client (App Android)

### 6.1 Java Overlay
- [ ] Criar `android/client/java/com/zminternal/OverlayActivity.java`
  - Estender `Activity`
  - Tema: `@android:style/Theme.Translucent.NoTitleBar`
  - Flags: `FLAG_NOT_TOUCH_MODAL`, `FLAG_NOT_FOCUSABLE`, `FLAG_LAYOUT_NO_LIMITS`
  - `onCreate()`: carregar biblioteca nativa `System.loadLibrary("zmclient")`
  - `onTouchEvent()`: forward para native `onTouch()`
  - `dispatchTouchEvent()`: decide consumir ou passar
- [ ] Criar `android/client/java/com/zminternal/ZmApplication.java`
  - `Application` custom para inicialização
- [ ] Criar `android/client/res/layout/activity_overlay.xml` (vazio ou `SurfaceView`)
- [ ] Criar `android/client/res/values/styles.xml`
  - Tema transparente
- [ ] Criar `android/client/AndroidManifest.xml`
  - Permissões: `SYSTEM_ALERT_WINDOW`, `FOREGROUND_SERVICE`
  - Activity `OverlayActivity` com `launchMode="singleInstance"`

### 6.2 NDK Entry Point
- [ ] Criar `android/client/src/main.cpp`
  - `ANativeActivity_onCreate()` ou `JNI_OnLoad()`
  - Inicializar EGL + OpenGL ES
  - Inicializar ImGui
  - Registrar callbacks de input

### 6.3 Renderer
- [ ] Criar `android/client/src/render/renderer.hpp`
- [ ] Criar `android/client/src/render/renderer.cpp`
  - `EGLDisplay`, `EGLSurface`, `EGLContext`
  - `glClearColor(0,0,0,0)` + `GL_BLEND`
  - Swap buffers
  - Frame timing (target FPS = `ThreadDelay`)
- [ ] Criar `android/client/src/render/overlay.hpp`
- [ ] Criar `android/client/src/render/overlay.cpp`
  - Gerenciar `SurfaceView` ou `NativeActivity` surface
  - Sincronizar dimensões com jogo (via IPC ou JNI)
- [ ] Criar `android/client/src/render/fonts.hpp`
- [ ] Criar `android/client/src/render/fonts.cpp`
  - Portar fontes embeddadas de `src/Render/Fonts/`
  - Adaptar para ImGui Android (bytes hex ou carregar do assets)

### 6.4 UI / ImGui
- [ ] Criar `android/client/src/ui/interface.hpp`
- [ ] Criar `android/client/src/ui/interface.cpp`
  - Portar menu de `src/Render/Interface/Interface.cpp`
  - Adaptar para touch (sem hover, click = tap)
  - Remover dependências Win32 (`GetAsyncKeyState`, `VK_*`)
  - Substituir keybinds por botões touch ou gesture
- [ ] Criar `android/client/src/ui/input.hpp`
- [ ] Criar `android/client/src/ui/input.cpp`
  - `AInputEvent` → ImGuiIO
  - Touch down = mouse down
  - Touch move = mouse move
  - Touch up = mouse up
  - Back button = toggle menu
  - Dispatch condicional: se dentro do menu → consumir; senão → passar para jogo

### 6.5 IPC Client
- [ ] Criar `android/client/src/ipc/client_ipc.hpp`
- [ ] Criar `android/client/src/ipc/client_ipc.cpp`
  - Conectar ao socket abstract do daemon
  - Thread para receber snapshot (bloqueante)
  - Thread para enviar config (não-bloqueante)
  - Reconnect automático se daemon reiniciar
- [ ] Reutilizar `android/shared/IpcProtocol.h` e `protocol.cpp`

### 6.6 Lógica do Cheat (Client-side)
- [ ] Criar `android/client/src/logic/esp.hpp`
- [ ] Criar `android/client/src/logic/esp.cpp`
  - Receber `IpcMsgSnapshot`
  - Reprojetar posições de mundo para tela (W2S)
  - Desenhar: box, healthbar, snaplines, skeleton, weapon, distance, name, watermark, enemy counter
  - Usar `ImGui::GetForegroundDrawList()`
- [ ] Criar `android/client/src/logic/aimbot.hpp`
- [ ] Criar `android/client/src/logic/aimbot.cpp`
  - Selecionar alvo do snapshot recebido
  - Visible check (ler do snapshot, não da memória)
  - BoneSwap: enviar comando para daemon aplicar (ou aplicar no client se for write simples)
  - Rage: enviar comando para daemon (ou thread no daemon)
  - Magnet: enviar comando para daemon
  - Ghost: toggle via IPC
- [ ] Criar `android/client/src/logic/silent.hpp`
- [ ] Criar `android/client/src/logic/silent.cpp`
  - Alvo independente do aimbot
  - Enviar comando para daemon
- [ ] Criar `android/client/src/logic/exploits.hpp`
- [ ] Criar `android/client/src/logic/exploits.cpp`
  - Mapear toggles da UI para `IpcMsgConfig`
  - NoRecoil, FireDelay, MoreDamage, Aimlock, etc.
  - FastMedkit, TelaParada, AtributarArma, BugarPixel, Precision, BackJump, SocoLonge, SpinBot
  - AimLock2x, AimbotAwm

---

## Fase 7 — Porte de Código Específico

### 7.1 Math / Utils
- [ ] Copiar `EspLines/Math/` para `android/shared/math/` ou `client/src/math/`
- [ ] Adaptar `Vector2/3/4`, `Quaternion`, `Matrix4x4` (já são cross-platform)
- [ ] Adaptar `W2S::World2Screen()` para dimensões do celular
- [ ] Remover `DirectXMath` (usado só no rage quaternion); substituir por implementação própria ou GLM

### 7.2 Skeleton
- [ ] Copiar `EspLines/Main/Draw/Skeleton.cpp` para `client/src/logic/skeleton.cpp`
- [ ] Adaptar para receber dados de skeleton via snapshot (não ler memória ao vivo)
- [ ] Ou: daemon resolve skeleton e manda bones projetados no snapshot

### 7.3 Chams
- [ ] Criar `android/client/src/logic/chams.cpp`
- [ ] HWBP não funciona no Android; pesquisar alternativa:
  - Hook via `PLT/GOT` no processo do jogo (requer injeção — fora do escopo externo)
  - Ou: desativar Chams no Android v1
- [ ] Marcar como `TODO / experimental`

### 7.4 Anti-Detecção (Daemon)
- [ ] Criar `android/daemon/src/anti_detect/hide.cpp`
- [ ] Renomear processo do daemon (argv[0])
- [ ] Esconder de `/proc` se possível (magiskhide/zygisk)
- [ ] Socket abstract com nome aleatório gerado em runtime
- [ ] Evitar `ptrace` attach direto; preferir `process_vm_readv`

### 7.5 Config / Save-Load
- [ ] Criar `android/client/src/ui/config.cpp`
- [ ] Salvar em `/sdcard/Android/data/com.zminternal/files/config.json`
- [ ] Usar nlohmann/json
- [ ] Load no startup

---

## Fase 8 — Integração e Testes

- [ ] Compilar daemon: `aarch64-linux-android-clang++` ou `armv7a-linux-androideabi-clang++`
- [ ] Compilar client: via Gradle/CMake ou ndk-build
- [ ] Push daemon para `/data/local/tmp/` e executar com `su`
- [ ] Instalar client APK
- [ ] Testar IPC: daemon envia heartbeat, client recebe
- [ ] Testar snapshot: 1 entidade → client desenha box
- [ ] Testar snapshot: 50 entidades → performance OK?
- [ ] Testar snapshot: 200 entidades → latência < 16ms?
- [ ] Testar input: toque no menu consome, toque fora passa
- [ ] Testar aimbot: seleciona alvo, envia comando
- [ ] Testar exploits: toggle no menu, daemon aplica
- [ ] Testar reconexão: matar daemon, client reconecta automaticamente
- [ ] Testar recovery: matar FF, daemon detecta, reinicia leitura

---

## Fase 9 — Otimizações

- [ ] Comprimir snapshot (delta encoding entre frames)
- [ ] Reduzir frequência de snapshot para entidades distantes
- [ ] Usar `SOCK_SEQPACKET` ou `SOCK_DGRAM` em vez de `SOCK_STREAM` se houver framing issues
- [ ] Buffer triplo no renderer para evitar tearing
- [ ] Lazy loading de fontes (só carrega quando menu abre)
- [ ] Compilar com `-O3 -flto` para daemon
- [ ] Strip símbolos do daemon (`strip zm_daemon`)

---

## Fase 10 — Polimento

- [ ] Criar ícone do app (não suspeito)
- [ ] Criar nome do app benigno (ex: "System Service")
- [ ] Adicionar splash screen falsa
- [ ] Criar `docs/IPC_SPEC.md` detalhado
- [ ] Criar `docs/OFFSETS.md` com tabela por versão
- [ ] Criar `docs/PORTING_GUIDE.md` explicando o que foi portado de onde
- [ ] Atualizar `README.md` raiz com instruções de build Android
- [ ] Criar `CHANGELOG.md`
- [ ] Tag de release: `v2.0-android-alpha`

---

## Checklist Rápido (Resumo)

```
□ Mover legado para legacy/
□ Criar estrutura android/{shared,daemon,client}/
□ Copiar ImGui + json para third_party/
□ Criar CMakeLists.txt e scripts de build
□ Criar IpcProtocol.h
□ Implementar daemon: memória + readloop + IPC server
□ Implementar client: overlay Java + EGL + ImGui + IPC client
□ Portar ESP/aimbot/silent/exploits para client (dados via IPC)
□ Testar IPC → snapshot → render
□ Testar input touch dispatch
□ Otimizar e polir
```

---

*Criado em: 2026-08-29*
*Próximo passo: escolher um item do checklist e executar.*
