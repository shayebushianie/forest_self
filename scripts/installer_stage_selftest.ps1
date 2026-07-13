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
    'session.dat', 'app.log', 'backup.bak'
)

function Invoke-PackageExpectingReparseRejection([string]$name, [string]$portablePath, [string]$stagePath) {
    & powershell -NoProfile -ExecutionPolicy Bypass -File $packageScript -PortableDir $portablePath -StageDir $stagePath -StageOnly
    if ($LASTEXITCODE -eq 0) {
        throw "$name reparse point was accepted."
    }
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
        if (Test-Path -LiteralPath $portableLink) {
            Remove-Item -LiteralPath $portableLink -Force
        }
    }

    try {
        New-Item -ItemType Directory -Force -Path $stageWithLink, $stageTarget | Out-Null
        New-Item -ItemType Junction -Path $stageLink -Target $stageTarget | Out-Null
        Invoke-PackageExpectingReparseRejection 'Stage descendant' $portableRoot $stageWithLink
    } finally {
        if (Test-Path -LiteralPath $stageLink) {
            Remove-Item -LiteralPath $stageLink -Force
        }
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
} finally {
    Resolve-SafeArtifactPath $fixtureRoot 'Self-test fixture' -Recurse | Out-Null
    if (Test-Path -LiteralPath $fixtureRoot) {
        Remove-Item -LiteralPath $fixtureRoot -Recurse -Force
    }
}
