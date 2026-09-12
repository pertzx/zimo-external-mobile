$ErrorActionPreference = 'Stop'

$Root = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location $Root

Write-Host "[1/4] Limpando CMake/Gradle nativos..." -ForegroundColor Cyan
& .\gradlew.bat :app:externalNativeBuildCleanDebug

Write-Host "[2/4] Montando os dois .so..." -ForegroundColor Cyan
& .\gradlew.bat :app:assembleDebug

Write-Host "[3/4] Procurando artefatos..." -ForegroundColor Cyan
$libs = Get-ChildItem -Path .\app\build -Recurse -Filter *.so -ErrorAction SilentlyContinue |
    Where-Object { $_.Name -in @('libclient.so', 'libdaemon.so') }

if (-not $libs) {
    throw "Nenhum libclient.so/libdaemon.so foi encontrado."
}

Write-Host "[4/4] Resultado:" -ForegroundColor Green
$libs | ForEach-Object {
    Write-Host (" - {0} | {1}" -f $_.FullName, $_.Length)
}

Write-Host "" 
Write-Host "ABIs configuradas: armeabi-v7a e arm64-v8a" -ForegroundColor Green
