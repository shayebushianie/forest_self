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
. (Join-Path $PSScriptRoot "PackageSafety.ps1")
. (Join-Path $PSScriptRoot "RuntimeSmoke.ps1")

$packagePaths = Assert-ForestPortableOutputSafe -RepoRoot $repoRoot -BuildDir $BuildDir -OutputDir $OutputDir
$buildPath = $packagePaths.BuildPath
$outputPath = $packagePaths.OutputDirectory
$zipPath = $packagePaths.ZipPath
$exeName = "forest.exe"
$builtExe = Join-Path $buildPath $exeName

function Require-File([string]$path, [string]$message) {
    if (-not (Test-Path -LiteralPath $path)) {
        throw "${message}: $path"
    }
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
    throw "Matching compiler not recorded in CMakeCache.txt; refusing to guess runtime DLLs from PATH."
}

if (-not $NoBuild) {
    cmake --build $buildPath --config $Configuration
    if ($LASTEXITCODE -ne 0) {
        throw "Build failed."
    }
}

Require-File $builtExe "Built executable not found"

$outputLeaf = Split-Path -Leaf $outputPath
$stageId = [Guid]::NewGuid().ToString("N")
$stagingRoot = Join-Path $packagePaths.ArtifactRoot ".$outputLeaf.stage-$stageId"
$stagingOutput = Join-Path $stagingRoot $outputLeaf
$stagedExe = Join-Path $stagingOutput $exeName
$stagedZip = Join-Path $packagePaths.ArtifactRoot ".$outputLeaf.stage-$stageId.zip"

foreach ($path in @($stagingRoot, $stagingOutput, $stagedZip)) {
    Assert-ForestNoReparsePoints -Path $path
    if (Test-Path -LiteralPath $path) {
        throw "Staging path already exists; refusing to overwrite it: $path"
    }
}
New-Item -ItemType Directory -Path $packagePaths.ArtifactRoot -Force | Out-Null
New-Item -ItemType Directory -Path $stagingRoot | Out-Null
New-Item -ItemType Directory -Path $stagingOutput | Out-Null
New-Item -ItemType Directory -Path (Join-Path $stagingOutput "app_data") | Out-Null
New-Item -ItemType File -Path (Join-Path $stagingOutput "forest.portable") | Out-Null
Copy-Item -LiteralPath $builtExe -Destination $stagedExe

& $windeployqt $stagedExe --no-compiler-runtime
if ($LASTEXITCODE -ne 0) {
    throw "windeployqt failed."
}

foreach ($dll in @("libgcc_s_seh-1.dll", "libstdc++-6.dll", "libwinpthread-1.dll")) {
    $source = Join-Path $runtimeBin $dll
    Require-File $source "Compiler runtime DLL not found"
    Copy-Item -LiteralPath $source -Destination (Join-Path $stagingOutput $dll)
}

Assert-ForestRuntimeComplete $stagedExe

if ($SmokeTest) {
    $smokeRoot = Join-Path ([IO.Path]::GetTempPath()) "forest-portable-smoke-$stageId"
    Assert-ForestNoReparsePoints -Path $smokeRoot
    if (Test-Path -LiteralPath $smokeRoot) {
        throw "Smoke-test fixture path already exists; refusing to overwrite it: $smokeRoot"
    }
    New-Item -ItemType Directory -Path $smokeRoot | Out-Null
    Copy-Item -LiteralPath $stagingOutput -Destination $smokeRoot -Recurse
    $smokeOutput = Join-Path $smokeRoot $outputLeaf
    $smokeExe = Join-Path $smokeOutput $exeName
    Invoke-ForestRuntimeSmoke $smokeExe | Out-Null
    Write-Host "Smoke-test fixture retained:"
    Write-Host "  $smokeRoot"
}

Compress-Archive -LiteralPath $stagingOutput -DestinationPath $stagedZip

$packagePaths = Assert-ForestPortableOutputSafe -RepoRoot $repoRoot -BuildDir $BuildDir -OutputDir $OutputDir
[IO.Directory]::Move($stagingOutput, $outputPath)
[IO.File]::Move($stagedZip, $zipPath)

$remainingStageItems = @(Get-ChildItem -Force -LiteralPath $stagingRoot)
if ($remainingStageItems.Count -eq 0) {
    Assert-ForestNoReparsePoints -Path $stagingRoot
    Remove-Item -LiteralPath $stagingRoot -Force
}

Write-Host "Portable package created:"
Write-Host "  $outputPath"
Write-Host "  $zipPath"
