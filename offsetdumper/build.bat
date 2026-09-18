@echo off
setlocal enabledelayedexpansion
cd /d "%~dp0"

set "NDK_PATH=C:\Android\Sdk\ndk\30.0.14904198"
set "MODULE_NAME=offset_dumper"
set "OUT_DIR=build_output"
set "ZIP_NAME=%MODULE_NAME%-v1.0.0.zip"

echo [1/4] Limpando...
if exist "libs" rmdir /s /q "libs"
if exist "obj"  rmdir /s /q "obj"
if exist "%OUT_DIR%" rmdir /s /q "%OUT_DIR%"
if exist "%ZIP_NAME%" del /F "%ZIP_NAME%"
mkdir "%OUT_DIR%\zygisk"

echo [2/4] Compilando...
call "%NDK_PATH%\ndk-build.cmd" -j8
if %errorlevel% neq 0 (
    echo.
    echo ERRO NA COMPILACAO!
    pause
    exit /b 1
)

echo [3/4] Empacotando modulo Magisk...
copy /Y "module.prop"    "%OUT_DIR%\module.prop"    >nul
copy /Y "customize.sh"   "%OUT_DIR%\customize.sh"   >nul
copy /Y /B "libs\armeabi-v7a\liboffsetdumper.so" "%OUT_DIR%\zygisk\armeabi-v7a.so" >nul
copy /Y /B "libs\arm64-v8a\liboffsetdumper.so"   "%OUT_DIR%\zygisk\arm64-v8a.so"  >nul

for %%A in ("libs\arm64-v8a\liboffsetdumper.so") do set "SO_SIZE_SRC=%%~zA"
for %%A in ("%OUT_DIR%\zygisk\arm64-v8a.so") do set "SO_SIZE_DST=%%~zA"
echo [INFO] .so source size: !SO_SIZE_SRC! bytes
echo [INFO] .so dest   size: !SO_SIZE_DST! bytes
if not "!SO_SIZE_SRC!"=="!SO_SIZE_DST!" (
    echo [ERRO] Tamanhos diferentes - copy falhou
    pause
    exit /b 1
)

if exist "%ZIP_NAME%" del /F "%ZIP_NAME%"

powershell -NoProfile -ExecutionPolicy Bypass -File "%CD%\zip_module.ps1" -OutDir "%CD%\%OUT_DIR%" -ZipPath "%CD%\%ZIP_NAME%"
if %errorlevel% neq 0 (
    echo [ERRO] Falha ao criar o zip
    pause
    exit /b 1
)

if not exist "%ZIP_NAME%" (
    echo [ERRO] Zip nao foi criado
    pause
    exit /b 1
)

echo.
echo ==========================================
echo BUILD CONCLUIDO
echo ==========================================
echo Modulo: %ZIP_NAME%
echo ==========================================

echo [4/4] Verificando device ADB...
adb devices > adb_devices_tmp.txt 2>nul
set "HAS_DEVICE=0"
for /f "skip=1 tokens=1,2 delims=	" %%a in (adb_devices_tmp.txt) do (
    if /i "%%b"=="device" set "HAS_DEVICE=1"
)
del /f adb_devices_tmp.txt 2>nul

if "!HAS_DEVICE!"=="0" (
    echo [INFO] Nenhum device conectado.
    echo [INFO] ZIP gerado em: %CD%\%ZIP_NAME%
    pause
    exit /b 0
)

echo [INFO] Device conectado. Enviando modulo...
adb push "%ZIP_NAME%" "/sdcard/Download/%ZIP_NAME%"
echo [OK] Modulo enviado para /sdcard/Download/%ZIP_NAME%
echo.
echo Para instalar:
echo   1. Abra Magisk
echo   2. Modulos ^> Instalar do armazenamento
echo   3. Escolha %ZIP_NAME% na pasta Download
echo   4. Reboot
echo.
echo Para ver o log:
echo   adb logcat -v time OffsetDumper:* *:S
echo.
pause
