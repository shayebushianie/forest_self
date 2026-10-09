$ErrorActionPreference = "Stop"

. (Join-Path (Split-Path -Parent $PSScriptRoot) "PackageSafety.ps1")
. (Join-Path (Split-Path -Parent $PSScriptRoot) "RuntimeSmoke.ps1")

function Assert-Throws([scriptblock]$Action, [string]$Description) {
    try {
        & $Action
    } catch {
        return
    }
    throw "Expected rejection: $Description"
}

$fixtureId = [Guid]::NewGuid().ToString("N")
$fixtureRoot = Join-Path ([IO.Path]::GetTempPath()) "forest-package-safety-$fixtureId"
$repoRoot = Join-Path $fixtureRoot "repo"
$artifactRoot = Join-Path $repoRoot "artifacts"
$sourcePath = Join-Path $repoRoot "src"
$buildPath = Join-Path $repoRoot "build"
New-Item -ItemType Directory -Path $artifactRoot, $sourcePath, $buildPath | Out-Null

$defaultPaths = Assert-ForestPortableOutputSafe -RepoRoot $repoRoot -BuildDir "build" -OutputDir "artifacts/forest_portable"
if ($defaultPaths.OutputDirectory -ne (Join-Path $artifactRoot "forest_portable")) {
    throw "Default package path did not normalize to the dedicated artifacts directory."
}

Assert-Throws {
    Assert-ForestPortableOutputSafe -RepoRoot $repoRoot -BuildDir "build" -OutputDir $repoRoot
} "repository root output"
Assert-Throws {
    Assert-ForestPortableOutputSafe -RepoRoot $repoRoot -BuildDir "build" -OutputDir "src"
} "source directory output"
Assert-Throws {
    Assert-ForestPortableOutputSafe -RepoRoot $repoRoot -BuildDir "build" -OutputDir "build"
} "build directory output"
Assert-Throws {
    Assert-ForestPortableOutputSafe -RepoRoot $repoRoot -BuildDir "build" -OutputDir "artifacts"
} "artifacts root output"
Assert-Throws {
    Assert-ForestPortableOutputSafe -RepoRoot $repoRoot -BuildDir "build" -OutputDir "artifacts/nested/package"
} "nested artifacts output"
Assert-Throws {
    Assert-ForestPortableOutputSafe -RepoRoot $repoRoot -BuildDir "build" -OutputDir (Join-Path $fixtureRoot "outside/package")
} "outside output"
Assert-Throws {
    Assert-ForestPortableOutputSafe -RepoRoot $repoRoot -BuildDir "artifacts/overlap" -OutputDir "artifacts/overlap"
} "build overlap"

$zipOnlyPath = Join-Path $artifactRoot "archive-only.zip"
[IO.File]::WriteAllText($zipOnlyPath, "synthetic archive")
Assert-Throws {
    Assert-ForestPortableOutputSafe -RepoRoot $repoRoot -BuildDir "build" -OutputDir "artifacts/archive-only"
} "existing ZIP output"

$existingOutput = Join-Path $artifactRoot "forest_portable"
$existingAppData = Join-Path $existingOutput "app_data"
New-Item -ItemType Directory -Path $existingAppData | Out-Null
$existingDataFile = Join-Path $existingAppData "synthetic-users.dat"
[IO.File]::WriteAllText($existingDataFile, "synthetic user data")
Assert-Throws {
    Assert-ForestPortableOutputSafe -RepoRoot $repoRoot -BuildDir "build" -OutputDir "artifacts/forest_portable"
} "existing app_data"
if (-not (Test-Path -LiteralPath $existingDataFile -PathType Leaf)) {
    throw "Existing app_data fixture was removed."
}

$unknownOutput = Join-Path $artifactRoot "unknown"
New-Item -ItemType Directory -Path $unknownOutput | Out-Null
$unknownFile = Join-Path $unknownOutput "notes.txt"
[IO.File]::WriteAllText($unknownFile, "synthetic unknown content")
Assert-Throws {
    Assert-ForestPortableOutputSafe -RepoRoot $repoRoot -BuildDir "build" -OutputDir "artifacts/unknown"
} "unknown output content"
if (-not (Test-Path -LiteralPath $unknownFile -PathType Leaf)) {
    throw "Unknown-content fixture was removed."
}

$junctionPath = Join-Path $artifactRoot "linked-output"
$outsideTarget = Join-Path $fixtureRoot "outside-target"
New-Item -ItemType Directory -Path $outsideTarget | Out-Null
try {
    New-Item -ItemType Junction -Path $junctionPath -Target $outsideTarget | Out-Null
} catch {
    throw "Reparse-point fixture unavailable; safety validation is incomplete: $($_.Exception.Message)"
}
Assert-Throws {
    Assert-ForestPortableOutputSafe -RepoRoot $repoRoot -BuildDir "build" -OutputDir "artifacts/linked-output"
} "reparse-point output"

$runtime = Join-Path $fixtureRoot 'runtime'
New-Item -ItemType Directory -Path $runtime | Out-Null
$executable = Join-Path $runtime 'forest.exe'
[IO.File]::WriteAllText($executable, 'synthetic executable; never launched')
try {
    Assert-ForestRuntimeComplete $executable
    throw 'Incomplete runtime was accepted.'
} catch {
    if ($_.Exception.Message -notlike 'Missing adjacent runtime files:*Qt6Core.dll*Qt6Gui.dll*Qt6Widgets.dll*') {
        throw 'Missing Qt libraries were not diagnosed before startup.'
    }
}
foreach ($relative in @('Qt6Core.dll','Qt6Gui.dll','Qt6Widgets.dll','Qt6Sql.dll',
    'libgcc_s_seh-1.dll','libstdc++-6.dll','libwinpthread-1.dll','platforms/qwindows.dll','sqldrivers/qsqlite.dll')) {
    $path = Join-Path $runtime $relative
    New-Item -ItemType Directory -Path (Split-Path $path) -Force | Out-Null
    [IO.File]::WriteAllText($path, 'synthetic runtime; presence check only')
}
Assert-ForestRuntimeComplete $executable
Assert-Throws { Invoke-ForestRuntimeSmoke $executable } 'startup without isolated portable marker'
Write-Output "Package and runtime preflight self-test passed. Synthetic fixture retained at: $fixtureRoot"
