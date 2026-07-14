param()

$ErrorActionPreference = 'Stop'

$repoRoot = Split-Path -Parent $PSScriptRoot
$artifactsRoot = [System.IO.Path]::GetFullPath((Join-Path $repoRoot 'artifacts'))

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

function Resolve-SafeArtifactPath([string]$path, [string]$description, [switch]$Recurse) {
    $normalizedPath = [System.IO.Path]::GetFullPath($path).TrimEnd('\')
    $normalizedArtifactsRoot = $artifactsRoot.TrimEnd('\')
    if (-not $normalizedPath.StartsWith($normalizedArtifactsRoot + '\', [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "$description must be created below artifacts: $normalizedPath"
    }

    $currentPath = $normalizedArtifactsRoot
    if (Test-Path -LiteralPath $currentPath) {
        $rootItem = Get-Item -LiteralPath $currentPath -Force
        if (($rootItem.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
            throw "Repository artifacts directory must not be a reparse point: $currentPath"
        }
    }

    $relativePath = $normalizedPath.Substring($normalizedArtifactsRoot.Length).TrimStart('\')
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

    if ($Recurse -and (Test-Path -LiteralPath $normalizedPath)) {
        Assert-NoReparsePointsBelow $normalizedPath $description
    }

    return $normalizedPath
}

$fixtureRoot = Resolve-SafeArtifactPath (Join-Path $artifactsRoot ("installer-stage-selftest-" + [guid]::NewGuid())) 'Self-test fixture'
$portableRoot = Join-Path $fixtureRoot 'portable'
$stageRoot = Join-Path $fixtureRoot 'stage'
$packageScript = Join-Path $PSScriptRoot 'package_installer.ps1'

$required = @(
    'forest.exe', 'Qt6Sql.dll',
    'platforms\qwindows.dll', 'sqldrivers\qsqlite.dll'
)
$forbidden = @(
    'forest.portable', 'app_data\sessions.sqlite',
    'session.dat', 'app.log', 'backup.bak',
    'sessions.sqlite-wal', 'sessions.sqlite-shm',
    'preferences.ini', 'restore_request.txt',
    'backup\archive.bin', 'snapshots\state.bin',
    'ForestRestoreSnapshots\snapshot.bin'
)

function Invoke-PackageExpectingReparseRejection([string]$name, [string]$portablePath, [string]$stagePath) {
    Invoke-PackageCommand $portablePath $stagePath -StageOnly
    if ($LASTEXITCODE -eq 0) {
        throw "$name reparse point was accepted."
    }
}

function Remove-ReparseFixture([string]$path) {
    if (-not (Test-Path -LiteralPath $path)) {
        return
    }

    $item = Get-Item -LiteralPath $path -Force
    if (($item.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
        cmd.exe /d /s /c "rmdir `"$($item.FullName)`"" | Out-Null
        if (Test-Path -LiteralPath $item.FullName) {
            throw "Unable to remove reparse-point fixture: $($item.FullName)"
        }
        return
    }

    Remove-Item -LiteralPath $item.FullName -Recurse -Force
}

function Invoke-PackageCommand(
    [string]$portablePath,
    [string]$stagePath,
    [switch]$StageOnly,
    [switch]$NoBuild
) {
    $escapedScript = $packageScript.Replace("'", "''")
    $escapedPortablePath = $portablePath.Replace("'", "''")
    $escapedStagePath = $stagePath.Replace("'", "''")
    $arguments = @()
    if ($StageOnly) {
        $arguments += '-StageOnly'
    }
    if ($NoBuild) {
        $arguments += '-NoBuild'
    }
    $command = "& { `$ErrorActionPreference = 'Stop'; try { & '$escapedScript' -PortableDir '$escapedPortablePath' -StageDir '$escapedStagePath' $($arguments -join ' '); exit 0 } catch { Write-Output `$_.Exception.Message; exit 1 } }"
    & powershell -NoProfile -ExecutionPolicy Bypass -Command $command
}

function Test-PackageReparseGuards {
    $portableTarget = Join-Path $fixtureRoot 'portable-reparse-target'
    $portableLink = Join-Path $portableRoot 'linked-runtime'
    $stageWithLink = Join-Path $fixtureRoot 'stage-with-reparse'
    $stageTarget = Join-Path $fixtureRoot 'stage-reparse-target'
    $stageLink = Join-Path $stageWithLink 'linked-runtime'

    try {
        New-Item -ItemType Directory -Force -Path $portableTarget | Out-Null
        New-Item -ItemType Junction -Path $portableLink -Target $portableTarget | Out-Null
    } catch {
        Write-Host "Reparse-point self-test skipped: junction creation is unavailable."
        return
    }

    try {
        Invoke-PackageExpectingReparseRejection 'Portable input descendant' $portableRoot (Join-Path $fixtureRoot 'stage-from-portable-reparse')
    } finally {
        Remove-ReparseFixture $portableLink
    }

    try {
        New-Item -ItemType Directory -Force -Path $stageWithLink, $stageTarget | Out-Null
        $stageTargetSentinel = Join-Path $stageTarget 'keep.txt'
        Set-Content -LiteralPath $stageTargetSentinel -Value 'must survive stage cleanup rejection' -Encoding ascii
        New-Item -ItemType Junction -Path $stageLink -Target $stageTarget | Out-Null
        Invoke-PackageExpectingReparseRejection 'Stage descendant' $portableRoot $stageWithLink
        if (-not (Test-Path -LiteralPath $stageTargetSentinel -PathType Leaf)) {
            throw 'Stage cleanup followed a reparse point outside the stage directory.'
        }
        if (-not (Test-Path -LiteralPath $stageLink)) {
            throw 'Stage reparse point was removed instead of rejecting cleanup.'
        }
    } finally {
        Remove-ReparseFixture $stageLink
    }
}

try {
    foreach ($relative in $required + $forbidden) {
        $path = Join-Path $portableRoot $relative
        New-Item -ItemType Directory -Force -Path (Split-Path -Parent $path) | Out-Null
        New-Item -ItemType File -Force -Path $path | Out-Null
    }

    $LASTEXITCODE = 0
    & $packageScript -PortableDir $portableRoot -StageDir $stageRoot -StageOnly
    if ($LASTEXITCODE -ne 0) {
        throw 'Stage-only package command failed.'
    }

    foreach ($relative in $required) {
        if (-not (Test-Path -LiteralPath (Join-Path $stageRoot $relative))) {
            throw "Required staged file is missing: $relative"
        }
    }

    foreach ($relative in $forbidden) {
        if (Test-Path -LiteralPath (Join-Path $stageRoot $relative)) {
            throw "Forbidden staged file exists: $relative"
        }
    }

    Test-PackageReparseGuards

    Remove-Item -LiteralPath (Join-Path $portableRoot 'sqldrivers\qsqlite.dll') -Force
    Invoke-PackageCommand $portableRoot $stageRoot -StageOnly 2>$null
    if ($LASTEXITCODE -eq 0) {
        throw 'Stage-only command accepted input without qsqlite.dll.'
    }

    $nonDefaultPortable = Join-Path $fixtureRoot 'different-portable'
    $nonDefaultOutput = Invoke-PackageCommand $nonDefaultPortable $stageRoot -NoBuild 2>&1 | Out-String
    if ($LASTEXITCODE -eq 0) {
        throw 'Non-stage package command accepted a non-default portable directory.'
    }
    if ($nonDefaultOutput -notmatch [regex]::Escape('Non-stage packaging requires PortableDir artifacts/forest_portable.')) {
        throw 'Non-stage package command did not reject the non-default portable directory before portable packaging.'
    }
} finally {
    Resolve-SafeArtifactPath $fixtureRoot 'Self-test fixture' -Recurse | Out-Null
    if (Test-Path -LiteralPath $fixtureRoot) {
        Remove-Item -LiteralPath $fixtureRoot -Recurse -Force
    }
}
