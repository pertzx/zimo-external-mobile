$Root = (Get-Location).Path

$IgnoreNames = @(
    ".git",
    ".gradle",
    ".cxx",
    "build",
    ".idea",
    "node_modules"
)

$Extensions = @(
    ".cpp",
    ".hpp",
    ".h",
    ".c",
    ".java",
    ".kt",
    ".gradle",
    ".kts",
    ".xml",
    ".cmake",
    ".json",
    ".txt"
)

function Should-Ignore {
    param(
        [string]$Path
    )

    foreach ($name in $IgnoreNames) {
        $escaped = [regex]::Escape($name)

        if ($Path -match "(^|[\\/])$escaped([\\/]|$)") {
            return $true
        }
    }

    return $false
}

function Get-RelativePath {
    param(
        [string]$Path
    )

    if ($Path.StartsWith($Root, [System.StringComparison]::OrdinalIgnoreCase)) {
        return $Path.Substring($Root.Length).TrimStart('\', '/')
    }

    return $Path
}

function Print-Tree {
    param(
        [string]$Path,
        [string]$Prefix = ""
    )

    $items = @(
        Get-ChildItem -LiteralPath $Path -Force |
        Where-Object {
            -not (Should-Ignore $_.FullName)
        } |
        Sort-Object @{
            Expression = { if ($_.PSIsContainer) { 0 } else { 1 } }
        }, Name
    )

    for ($i = 0; $i -lt $items.Count; $i++) {

        $item = $items[$i]
        $isLast = ($i -eq ($items.Count - 1))

        if ($isLast) {
            $branch = "\-- "
            $nextPrefix = "$Prefix    "
        }
        else {
            $branch = "|-- "
            $nextPrefix = "$Prefix|   "
        }

        if ($item.PSIsContainer) {
            Write-Output "$Prefix$branch$($item.Name)/"

            Print-Tree `
                -Path $item.FullName `
                -Prefix $nextPrefix
        }
        else {

            $extension = [System.IO.Path]::GetExtension($item.Name).ToLower()

            $allowed =
                ($Extensions -contains $extension) -or
                ($item.Name -match "^(CMakeLists\.txt|Makefile|gradlew|gradlew\.bat)$")

            if ($allowed) {
                Write-Output "$Prefix$branch$($item.Name)"
            }
        }
    }
}

Write-Output "============================================================"
Write-Output "ZIMO-EXTERNAL-MOBILE - STRUCTURE"
Write-Output "============================================================"
Write-Output ""
Write-Output "ROOT: $Root"
Write-Output ""

Print-Tree -Path $Root


# ============================================================
# CPP TREE
# ============================================================

$CppPath = Join-Path $Root "app\src\main\cpp"

Write-Output ""
Write-Output "============================================================"
Write-Output "CPP STRUCTURE"
Write-Output "============================================================"
Write-Output ""

if (Test-Path $CppPath) {
    Print-Tree -Path $CppPath
}
else {
    Write-Output "[ERROR] app/src/main/cpp not found"
}


# ============================================================
# IMPORTANT FILES
# ============================================================

Write-Output ""
Write-Output "============================================================"
Write-Output "IMPORTANT FILES"
Write-Output "============================================================"
Write-Output ""

$ImportantFiles = @(
    "app\src\main\cpp\CMakeLists.txt",

    "app\src\main\cpp\Daemon\DaemonApp.cpp",
    "app\src\main\cpp\Daemon\DaemonApp.hpp",
    "app\src\main\cpp\Daemon\daemon_main.cpp",
    "app\src\main\cpp\Daemon\daemon_root_main.cpp",

    "app\src\main\cpp\Daemon\Memory\Memory.cpp",
    "app\src\main\cpp\Daemon\Memory\Memory.hpp",

    "app\src\main\cpp\Daemon\IPC\IPCServer.cpp",
    "app\src\main\cpp\Daemon\IPC\IPCServer.hpp",

    "app\src\main\cpp\Daemon\Data.cpp",
    "app\src\main\cpp\Daemon\Data.hpp",

    "app\src\main\cpp\Panel\IPC\IPCClient.cpp",
    "app\src\main\cpp\Panel\IPC\IPCClient.hpp",

    "app\src\main\cpp\Shared\IPC\IPCProtocol.hpp",

    "app\src\main\cpp\Shared\Globals.hpp",

    "app\src\main\cpp\Shared\Offsets\Offsets.cpp",
    "app\src\main\cpp\Shared\Offsets\Offsets.hpp"
)

foreach ($file in $ImportantFiles) {

    $fullPath = Join-Path $Root $file

    if (Test-Path $fullPath) {

        $lineCount = @(
            Get-Content -LiteralPath $fullPath -ErrorAction SilentlyContinue
        ).Count

        Write-Output ("[OK] {0} ({1} lines)" -f $file, $lineCount)
    }
    else {
        Write-Output ("[--] {0}" -f $file)
    }
}


# ============================================================
# FILE COUNT BY DIRECTORY
# ============================================================

Write-Output ""
Write-Output "============================================================"
Write-Output "FILE COUNT BY CPP DIRECTORY"
Write-Output "============================================================"
Write-Output ""

if (Test-Path $CppPath) {

    Get-ChildItem -LiteralPath $CppPath -Recurse -File -Force |
        Where-Object {
            -not (Should-Ignore $_.FullName)
        } |
        Group-Object {
            $relative = $_.DirectoryName.Substring($CppPath.Length).TrimStart('\', '/')

            if ([string]::IsNullOrWhiteSpace($relative)) {
                "."
            }
            else {
                $relative
            }
        } |
        Sort-Object Name |
        ForEach-Object {

            Write-Output (
                "{0,-55} {1,4} files" -f $_.Name, $_.Count
            )
        }
}


# ============================================================
# INCLUDE SCAN
# ============================================================

Write-Output ""
Write-Output "============================================================"
Write-Output "INCLUDE REFERENCES"
Write-Output "============================================================"

$Patterns = @(
    "Daemon/Draw",
    "Daemon\Draw",
    "Daemon/Unity",
    "Daemon\Unity",
    "Daemon/Memory",
    "Daemon\Memory",
    "Panel/IPC",
    "Shared/Offsets"
)

foreach ($pattern in $Patterns) {

    Write-Output ""
    Write-Output ">>> $pattern"

    $files = Get-ChildItem `
        -LiteralPath $CppPath `
        -Recurse `
        -File `
        -Force `
        -Include *.cpp,*.hpp,*.h,*.c |
        Where-Object {
            -not (Should-Ignore $_.FullName)
        }

    foreach ($file in $files) {

        try {

            $matches = Select-String `
                -LiteralPath $file.FullName `
                -Pattern $pattern `
                -SimpleMatch `
                -ErrorAction SilentlyContinue

            foreach ($match in $matches) {

                $relative = Get-RelativePath -Path $match.Path

                Write-Output (
                    "  {0}:{1} -> {2}" -f `
                    $relative, `
                    $match.LineNumber, `
                    $match.Line.Trim()
                )
            }

        }
        catch {
            # Ignora arquivos que nao puderem ser lidos
        }
    }
}


# ============================================================
# CMAKE TARGET REFERENCES
# ============================================================

Write-Output ""
Write-Output "============================================================"
Write-Output "CMAKE SOURCE REFERENCES"
Write-Output "============================================================"
Write-Output ""

$CMake = Join-Path $Root "app\src\main\cpp\CMakeLists.txt"

if (Test-Path $CMake) {

    $patterns = @(
        "DAEMON_CORE_SOURCES",
        "DAEMON_SOURCES",
        "PANEL_SOURCES",
        "SHARED_SOURCES",
        "Draw.cpp",
        "Skeleton.cpp",
        "NameGun.cpp",
        "Unity.cpp",
        "UTF8.cpp",
        "Data.cpp",
        "Memory.cpp",
        "Offsets.cpp"
    )

    foreach ($pattern in $patterns) {

        $found = Select-String `
            -LiteralPath $CMake `
            -Pattern $pattern `
            -SimpleMatch `
            -ErrorAction SilentlyContinue

        foreach ($match in $found) {
            Write-Output (
                "{0}:{1} -> {2}" -f `
                "CMakeLists.txt", `
                $match.LineNumber, `
                $match.Line.Trim()
            )
        }
    }
}


# ============================================================
# SUMMARY
# ============================================================

Write-Output ""
Write-Output "============================================================"
Write-Output "DONE"
Write-Output "============================================================"