param(
    [string]$Configuration = "Release",
    [string]$BuildDir = "build",
    [string]$OutputDir = "artifacts/forest_portable",
    [string]$QtDir = "",
    [switch]$NoBuild,
    [switch]$SmokeTest
)

$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
$buildPath = Join-Path $repoRoot $BuildDir
$outputPath = Join-Path $repoRoot $OutputDir
$exeName = "forest.exe"
$builtExe = Join-Path $buildPath $exeName
$portableExe = Join-Path $outputPath $exeName

function Require-File([string]$path, [string]$message) {
    if (-not (Test-Path -LiteralPath $path)) {
        throw "${message}: $path"
    }
}

function Test-BinaryContainsText([string]$path, [string]$needle) {
    $bytes = [System.IO.File]::ReadAllBytes($path)
    $text = [System.Text.Encoding]::ASCII.GetString($bytes)
    return $text.Contains($needle)
}

function Resolve-QtBin {
    $candidates = @()
    if ($QtDir) {
        $candidates += (Join-Path $QtDir "bin")
    }

    $cmakeCache = Join-Path $buildPath "CMakeCache.txt"
    if (Test-Path -LiteralPath $cmakeCache) {
        $qt6DirLine = Get-Content -LiteralPath $cmakeCache |
            Where-Object { $_ -match "^Qt6_DIR(:[^=]+)?=(.+)$" } |
            Select-Object -First 1
        if ($qt6DirLine -and ($qt6DirLine -match "^Qt6_DIR(:[^=]+)?=(.+)$")) {
            $qt6Dir = $Matches[2]
            $prefix = Split-Path (Split-Path (Split-Path $qt6Dir -Parent) -Parent) -Parent
            $candidates += (Join-Path $prefix "bin")
        }
    }

    if ($env:CMAKE_PREFIX_PATH) {
        foreach ($prefix in ($env:CMAKE_PREFIX_PATH -split ';')) {
            if ($prefix) {
                $candidates += (Join-Path $prefix "bin")
            }
        }
    }

    foreach ($candidate in ($candidates | Where-Object { $_ } | Select-Object -Unique)) {
        if (Test-Path -LiteralPath (Join-Path $candidate "windeployqt.exe")) {
            return $candidate
        }
    }

    throw "windeployqt not found. Pass -QtDir with the Qt mingw_64 directory."
}

$qtBin = Resolve-QtBin
$windeployqt = Join-Path $qtBin "windeployqt.exe"
Require-File $windeployqt "windeployqt not found"

$runtimeBin = $null
$cmakeCache = Join-Path $buildPath "CMakeCache.txt"
if (Test-Path -LiteralPath $cmakeCache) {
    $compilerLine = Get-Content -LiteralPath $cmakeCache |
        Where-Object { $_ -match "^CMAKE_CXX_COMPILER(:[^=]+)?=(.+)$" } |
        Select-Object -First 1
    if ($compilerLine -and ($compilerLine -match "^CMAKE_CXX_COMPILER(:[^=]+)?=(.+)$")) {
        $runtimeBin = Split-Path $Matches[2] -Parent
    }
}
if (-not $runtimeBin) {
    $compiler = (Get-Command c++.exe -ErrorAction SilentlyContinue).Source
    if (-not $compiler) {
        $compiler = (Get-Command g++.exe -ErrorAction SilentlyContinue).Source
    }
    if ($compiler) {
        $runtimeBin = Split-Path $compiler -Parent
    }
}
$runtimeSearchDirs = @($runtimeBin, $qtBin) | Where-Object { $_ } | Select-Object -Unique

if (-not $NoBuild) {
    cmake --build $buildPath --config $Configuration
    if ($LASTEXITCODE -ne 0) {
        throw "Build failed."
    }
}

Require-File $builtExe "Built executable not found"

if (Test-Path -LiteralPath $outputPath) {
    Remove-Item -LiteralPath $outputPath -Recurse -Force
}
New-Item -ItemType Directory -Path $outputPath | Out-Null
New-Item -ItemType Directory -Path (Join-Path $outputPath "app_data") | Out-Null
New-Item -ItemType File -Path (Join-Path $outputPath "forest.portable") -Force | Out-Null
Copy-Item -LiteralPath $builtExe -Destination $portableExe

& $windeployqt $portableExe --no-compiler-runtime
if ($LASTEXITCODE -ne 0) {
    throw "windeployqt failed."
}

foreach ($dll in @("libgcc_s_seh-1.dll", "libstdc++-6.dll", "libwinpthread-1.dll")) {
    $source = $null
    foreach ($dir in $runtimeSearchDirs) {
        $candidate = Join-Path $dir $dll
        if (Test-Path -LiteralPath $candidate) {
            $source = $candidate
            break
        }
    }
    Require-File $source "Compiler runtime DLL not found"
    Copy-Item -LiteralPath $source -Destination (Join-Path $outputPath $dll) -Force
}

$pthread = Join-Path $outputPath "libwinpthread-1.dll"
if (-not (Test-BinaryContainsText $pthread "clock_gettime64")) {
    throw "Packaged libwinpthread-1.dll does not contain clock_gettime64."
}

if ($SmokeTest) {
    $process = Start-Process -FilePath $portableExe -WorkingDirectory $outputPath -PassThru -WindowStyle Hidden
    Start-Sleep -Seconds 3
    if ($process.HasExited) {
        throw "Portable executable exited during smoke test."
    }
    Stop-Process -Id $process.Id -Force
    Wait-Process -Id $process.Id -Timeout 5 -ErrorAction SilentlyContinue
}

$appDataPath = Join-Path $outputPath "app_data"
if (Test-Path -LiteralPath $appDataPath) {
    for ($attempt = 1; $attempt -le 5; $attempt++) {
        try {
            Remove-Item -LiteralPath $appDataPath -Recurse -Force
            break
        } catch {
            if ($attempt -eq 5) {
                throw
            }
            Start-Sleep -Milliseconds 300
        }
    }
}
New-Item -ItemType Directory -Path $appDataPath | Out-Null
New-Item -ItemType File -Path (Join-Path $outputPath "forest.portable") -Force | Out-Null

$zipPath = Join-Path $repoRoot "$OutputDir.zip"
if (Test-Path -LiteralPath $zipPath) {
    Remove-Item -LiteralPath $zipPath -Force
}
Compress-Archive -LiteralPath $outputPath -DestinationPath $zipPath

Write-Host "Portable package created:"
Write-Host "  $outputPath"
Write-Host "  $zipPath"
