param()

$ErrorActionPreference = 'Stop'

$repoRoot = Split-Path -Parent $PSScriptRoot
$artifactsRoot = [System.IO.Path]::GetFullPath((Join-Path $repoRoot 'artifacts'))
$fixtureRoot = [System.IO.Path]::GetFullPath((Join-Path $artifactsRoot ("installer-stage-selftest-" + [guid]::NewGuid())))
$portableRoot = Join-Path $fixtureRoot 'portable'
$stageRoot = Join-Path $fixtureRoot 'stage'
$packageScript = Join-Path $PSScriptRoot 'package_installer.ps1'

function Test-PathWithin([string]$path, [string]$parent) {
    $normalizedPath = [System.IO.Path]::GetFullPath($path).TrimEnd('\')
    $normalizedParent = [System.IO.Path]::GetFullPath($parent).TrimEnd('\')
    return $normalizedPath.StartsWith($normalizedParent + '\', [System.StringComparison]::OrdinalIgnoreCase)
}

if (-not (Test-PathWithin $fixtureRoot $artifactsRoot)) {
    throw "Self-test fixture must be created below artifacts: $fixtureRoot"
}

$required = @(
    'forest.exe', 'Qt6Sql.dll',
    'platforms\qwindows.dll', 'sqldrivers\qsqlite.dll'
)
$forbidden = @(
    'forest.portable', 'app_data\sessions.sqlite',
    'session.dat', 'app.log', 'backup.bak'
)

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
} finally {
    if ((Test-Path -LiteralPath $fixtureRoot) -and (Test-PathWithin $fixtureRoot $artifactsRoot)) {
        Remove-Item -LiteralPath $fixtureRoot -Recurse -Force
    }
}
