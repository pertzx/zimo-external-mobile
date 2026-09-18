# zip_module.ps1 - empacota o modulo Magisk sem corromper .so
param(
    [Parameter(Mandatory=$true)][string]$OutDir,
    [Parameter(Mandatory=$true)][string]$ZipPath
)

# Carrega AMBAS as assemblies (ZipArchiveMode esta em Compression, ZipFile em FileSystem)
Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem

if (Test-Path $ZipPath) { Remove-Item $ZipPath -Force }

Write-Host "[INFO] Criando zip: $ZipPath"
$zip = [System.IO.Compression.ZipFile]::Open($ZipPath, [System.IO.Compression.ZipArchiveMode]::Create)

$entries = @(
    @{ file = Join-Path $OutDir "module.prop";                   entry = "module.prop" },
    @{ file = Join-Path $OutDir "customize.sh";                  entry = "customize.sh" },
    @{ file = Join-Path $OutDir "zygisk\armeabi-v7a.so";         entry = "zygisk/armeabi-v7a.so" },
    @{ file = Join-Path $OutDir "zygisk\arm64-v8a.so";          entry = "zygisk/arm64-v8a.so" }
)

foreach ($e in $entries) {
    if (-not (Test-Path $e.file)) {
        Write-Host "[ERRO] Arquivo nao encontrado: $($e.file)"
        exit 1
    }
    [System.IO.Compression.ZipFileExtensions]::CreateEntryFromFile($zip, $e.file, $e.entry)
    Write-Host "  Adicionado: $($e.entry)"
}

$zip.Dispose()

Write-Host "[INFO] Conteudo final do zip:"
$zip = [System.IO.Compression.ZipFile]::OpenRead($ZipPath)
foreach ($entry in $zip.Entries) {
    Write-Host ("    {0,-30} ({1} bytes)" -f $entry.FullName, $entry.Length)
}
$zip.Dispose()

Write-Host "[OK] Zip criado com sucesso"
