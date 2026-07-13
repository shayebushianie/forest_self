param(
    [string]$BuildDir = 'build',
    [string]$PortableDir = 'artifacts/forest_portable',
    [string]$StageDir = 'artifacts/installer-stage',
    [string]$NsisCompiler = '',
    [switch]$NoBuild,
    [switch]$StageOnly
)

$ErrorActionPreference = 'Stop'

$repoRoot = Split-Path -Parent $PSScriptRoot
$artifactsRoot = [System.IO.Path]::GetFullPath((Join-Path $repoRoot 'artifacts'))
$requiredFiles = @(
    'forest.exe', 'Qt6Sql.dll',
    'platforms\qwindows.dll', 'sqldrivers\qsqlite.dll'
)
$forbiddenPattern = '(^|\\)(forest\.portable|app_data)(\\|$)|\.(sqlite|dat|log|bak)$'

function Resolve-ArtifactPath([string]$path, [string]$description) {
    if ([System.IO.Path]::IsPathRooted($path)) {
        $resolvedPath = [System.IO.Path]::GetFullPath($path)
    } else {
        $fromRepoRoot = [System.IO.Path]::GetFullPath((Join-Path $repoRoot $path))
        $artifactPrefix = $artifactsRoot.TrimEnd('\') + '\'
        if ($fromRepoRoot.StartsWith($artifactPrefix, [System.StringComparison]::OrdinalIgnoreCase)) {
            $resolvedPath = $fromRepoRoot
        } else {
            $resolvedPath = [System.IO.Path]::GetFullPath((Join-Path $artifactsRoot $path))
        }
    }

    $artifactPrefix = $artifactsRoot.TrimEnd('\') + '\'
    if (-not $resolvedPath.StartsWith($artifactPrefix, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "$description must resolve below the repository artifacts directory: $path"
    }

    return $resolvedPath
}

function Assert-NoReparsePointsBelow([string]$path, [string]$description) {
    foreach ($item in Get-ChildItem -LiteralPath $path -Force) {
        if (($item.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
            throw "$description must not contain a reparse point: $($item.FullName)"
        }

        if ($item.PSIsContainer) {
            Assert-NoReparsePointsBelow $item.FullName $description
        }
    }
}

function Assert-NoReparsePoints([string]$path, [string]$description, [switch]$Recurse) {
    $currentPath = $artifactsRoot
    if (Test-Path -LiteralPath $currentPath) {
        $rootItem = Get-Item -LiteralPath $currentPath -Force
        if (($rootItem.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
            throw "Repository artifacts directory must not be a reparse point: $currentPath"
        }
    }

    $relativePath = $path.Substring($artifactsRoot.TrimEnd('\').Length).TrimStart('\')
    foreach ($component in $relativePath.Split('\', [System.StringSplitOptions]::RemoveEmptyEntries)) {
        $currentPath = Join-Path $currentPath $component
        if (-not (Test-Path -LiteralPath $currentPath)) {
            break
        }

        $item = Get-Item -LiteralPath $currentPath -Force
        if (($item.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
            throw "$description must not contain a reparse point: $currentPath"
        }
    }

    if ($Recurse -and (Test-Path -LiteralPath $path)) {
        Assert-NoReparsePointsBelow $path $description
    }
}

function Test-PathsOverlap([string]$firstPath, [string]$secondPath) {
    $firstPrefix = $firstPath.TrimEnd('\') + '\'
    $secondPrefix = $secondPath.TrimEnd('\') + '\'
    return $firstPath.Equals($secondPath, [System.StringComparison]::OrdinalIgnoreCase) -or
        $firstPath.StartsWith($secondPrefix, [System.StringComparison]::OrdinalIgnoreCase) -or
        $secondPath.StartsWith($firstPrefix, [System.StringComparison]::OrdinalIgnoreCase)
}

function Assert-InstallerStage([string]$stagePath) {
    foreach ($relative in $requiredFiles) {
        if (-not (Test-Path -LiteralPath (Join-Path $stagePath $relative))) {
            throw "Installer stage is missing required runtime: $relative"
        }
    }

    foreach ($file in Get-ChildItem -LiteralPath $stagePath -Recurse -Force -File) {
        $relative = $file.FullName.Substring($stagePath.Length).TrimStart('\')
        if ($relative -match $forbiddenPattern) {
            throw "Installer stage contains forbidden file: $relative"
        }
    }
}

function New-InstallerStage([string]$portablePath, [string]$stagePath) {
    if (-not (Test-Path -LiteralPath $portablePath)) {
        throw "Portable input directory does not exist: $portablePath"
    }

    if (Test-Path -LiteralPath $stagePath) {
        Remove-Item -LiteralPath $stagePath -Recurse -Force
    }
    New-Item -ItemType Directory -Path $stagePath | Out-Null

    foreach ($file in Get-ChildItem -LiteralPath $portablePath -Recurse -Force -File) {
        $relative = $file.FullName.Substring($portablePath.Length).TrimStart('\')
        if ($relative -notmatch $forbiddenPattern) {
            $destination = Join-Path $stagePath $relative
            New-Item -ItemType Directory -Force -Path (Split-Path -Parent $destination) | Out-Null
            Copy-Item -LiteralPath $file.FullName -Destination $destination -Force
        }
    }

    Assert-InstallerStage $stagePath
}

function Resolve-NsisCompiler {
    $candidates = @(
        $NsisCompiler,
        $env:NSIS_COMPILER,
        (Join-Path ${env:ProgramFiles(x86)} 'NSIS\makensis.exe'),
        (Join-Path $env:ProgramFiles 'NSIS\makensis.exe')
    ) | Where-Object { $_ } | Select-Object -Unique
    foreach ($candidate in $candidates) {
        if (Test-Path -LiteralPath $candidate) {
            return (Resolve-Path -LiteralPath $candidate).Path
        }
    }
    throw 'makensis.exe not found. Pass -NsisCompiler, set NSIS_COMPILER, or install NSIS.'
}

$buildPath = Resolve-ArtifactPath $BuildDir 'Build directory'
$portablePath = Resolve-ArtifactPath $PortableDir 'Portable input directory'
$stagePath = Resolve-ArtifactPath $StageDir 'Installer stage directory'
Assert-NoReparsePoints $buildPath 'Build directory'
Assert-NoReparsePoints $portablePath 'Portable input directory' -Recurse
Assert-NoReparsePoints $stagePath 'Installer stage directory' -Recurse

if (Test-PathsOverlap $portablePath $stagePath) {
    throw 'Portable input directory and installer stage directory must not overlap.'
}

if (-not $StageOnly) {
    $portableScript = Join-Path $PSScriptRoot 'package_portable.ps1'
    $portableArgs = @('-BuildDir', $BuildDir)
    if ($NoBuild) {
        $portableArgs += '-NoBuild'
    }
    & $portableScript @portableArgs
    if (-not $?) {
        throw 'Portable package creation failed.'
    }

    $portableArchive = Join-Path $repoRoot 'artifacts\forest_portable.zip'
    if (-not (Test-Path -LiteralPath $portableArchive)) {
        throw "Portable package did not create $portableArchive"
    }
    Get-FileHash -LiteralPath $portableArchive -Algorithm SHA256 |
        ForEach-Object { "{0} *forest_portable.zip" -f $_.Hash } |
        Set-Content -LiteralPath (Join-Path $repoRoot 'artifacts\forest_portable.sha256') -Encoding ascii
}

New-InstallerStage $portablePath $stagePath

if ($StageOnly) {
    Write-Host "Installer stage created: $stagePath"
} else {
    $releaseDir = Join-Path $repoRoot 'release'
    New-Item -ItemType Directory -Force -Path $releaseDir | Out-Null
    $nsis = Resolve-NsisCompiler
    $script = Join-Path $repoRoot 'installer\forest_installer.nsi'
    & $nsis "/DSTAGE_DIR=$stagePath" "/DOUTPUT_DIR=$releaseDir" $script
    if ($LASTEXITCODE -ne 0) {
        throw "NSIS build failed with exit code $LASTEXITCODE."
    }
    $installer = Join-Path $releaseDir 'ForestFocus_Setup.exe'
    if (-not (Test-Path -LiteralPath $installer)) {
        throw "NSIS did not create $installer"
    }
    Get-FileHash -LiteralPath $installer -Algorithm SHA256 |
        ForEach-Object { "{0} *ForestFocus_Setup.exe" -f $_.Hash } |
        Set-Content -LiteralPath (Join-Path $repoRoot 'artifacts\ForestFocus_Setup.sha256') -Encoding ascii
}
