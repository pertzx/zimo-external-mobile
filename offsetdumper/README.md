# OffsetDumper Zygisk Module

Módulo Magisk Zygisk para dumpar offsets de classes IL2CPP do Free Fire (GameFacade_TypeInfo, GameVarDef_TypeInfo, AvatarWardrobeDataManager_TypeInfo).

## Estrutura

```
offsetdumper_module/
├── jni/
│   ├── Android.mk          # Configuracao build NDK
│   ├── Application.mk      # ABI + STL
│   ├── zygisk.hpp          # Header OFICIAL do Zygisk (topjohnwu)
│   ├── module.cpp          # Entry point do modulo
│   └── version_script.txt  # Esconde todos simbolos exceto zygisk_module_entry
├── module.prop             # Metadata do modulo Magisk
├── customize.sh            # Script de instalacao Magisk
├── build.bat               # Script de build (Windows)
└── zip_module.ps1          # Helper PowerShell pra zipar sem corromper .so
```

## Como usar

1. **Compilar** (no Windows com NDK instalado):
   ```
   .\build.bat
   ```
   Gera `offset_dumper-v1.0.0.zip`

2. **Instalar no celular**:
   ```
   adb push offset_dumper-v1.0.0.zip /sdcard/Download/
   ```
   Abra Magisk → Módulos → Instalar do armazenamento → escolha o zip → Reboot

3. **Verificar logs** (após reboot e abrir Free Fire):
   ```
   adb logcat -c
   adb logcat -v time OffsetDumper:* *:S
   ```

## Versão ATUAL (v1.0.0)

Esta é a versão **MÍNIMA** — só logs, sem scanner IL2CPP. Serve pra confirmar que:
- O Zygisk carrega o módulo sem crashar o Zygote
- O entry point `zygisk_module_entry` é encontrado e chamado
- O processo `com.dts.freefireth` é detectado corretamente

## Próximos passos

Depois que esta versão mínima funcionar (você ver `[ZYGISK] onLoad chamado` e `[ZYGISK] preAppSpecialize: processo='com.dts.freefireth'` no logcat), adicione de volta o scanner IL2CPP no `module.cpp`, dentro da thread de `postAppSpecialize`.

## Símbolos exportados (verificação)

Para confirmar que apenas `zygisk_module_entry` é exportado:
```
& "C:\Android\Sdk\ndk\30.0.14904198\toolchains\llvm\prebuilt\windows-x86_64\bin\llvm-nm.exe" -D .\libs\arm64-v8a\liboffsetdumper.so
```

Deve aparecer apenas:
```
... U __android_log_print
... U fopen
... U fclose
...
000000000000XXXX T zygisk_module_entry
```

Sem nenhum `std::...` exportado.
