ARQUIVOS CORRIGIDOS - ZIMO EXTERNAL MOBILE

Aplicar estes arquivos no projeto existente.

1) app/src/main/cpp/CMakeLists.txt
   - somente dois targets SHARED: client e daemon
   - elimina stormdaemon/executavel
   - remove Data.cpp/Data.hpp/daemon_root_main.cpp
   - usa Client como raiz dos sources movidos

2) app/src/main/cpp/Daemon/DaemonApp.cpp
   - remove dependencias de Data e Memory
   - daemon fica apenas como backend/ponte IPC

3) app/src/main/cpp/Daemon/IPC/IPCServer.cpp
   - remove include quebrado de Daemon/Unity
   - usa Globals diretamente
   - preserva a ponte IPC de configuracao

4) app/src/main/cpp/Daemon/daemon_main.cpp
   - remove ../Shared/Globals.hpp
   - usa Globals.hpp pelo include path

5) app/src/main/java/com/stormcheats/OverlayService.java
   - troca System.loadLibrary("panel") por System.loadLibrary("client")

6) build.ps1
   - limpa o native build e gera o APK Debug
   - verifica libclient.so e libdaemon.so

Observacao:
O CMake usa GLOB_RECURSE para o lado Client porque os arquivos foram fisicamente movidos
entre Panel/Shared/Daemon -> Client. Isso evita manter uma lista de caminhos antigos no build.
