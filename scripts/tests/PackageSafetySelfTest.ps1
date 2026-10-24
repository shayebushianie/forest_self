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

function Invoke-ExpectedRejection([scriptblock]$Action, [string]$MessagePattern, [string]$Description) {
    try {
        & $Action
    } catch {
        if ($_.Exception.Message -match $MessagePattern) {
            return
        }
        $script:regressionFailures.Add("$Description rejected for the wrong reason: $($_.Exception.Message)")
        return
    }
    $script:regressionFailures.Add("$Description was accepted.")
}

$regressionFailures = [Collections.Generic.List[string]]::new()

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

$repoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$installerSource = [IO.File]::ReadAllText((Join-Path $repoRoot 'scripts/installer/forest_installer.nsi'))
if ($installerSource -notmatch '(?ms)!ifndef VERIFICATION_ID\s+!define MUI_FINISHPAGE_RUN .*\r?\n\s*!define MUI_FINISHPAGE_RUN_TEXT .*\r?\n\s*!endif') {
    $regressionFailures.Add('Verification installer does not guard both finish-page run macros.')
}
if ($installerSource -notmatch '(?ms)Function CheckDestination\r?\n(?:(?!FunctionEnd).)*?StrCmp \$ValidateMode 0 validate_registration_done\r?\n\s*ReadRegStr \$2 HKCU "\$\{PRODUCT_REGKEY\}" "InstallDir"\r?\n\s*StrCmp \$2 \$INSTDIR invalid_destination\r?\n\s*validate_registration_done:') {
    $regressionFailures.Add('Validation mode does not reject overwriting its registered install directory.')
}

$fixtureQtBuild = Join-Path $fixtureRoot 'qt-mismatch-metadata'
$fixtureCmakeQt = Join-Path $fixtureRoot 'cmake-qt'
$fixtureExplicitQt = Join-Path $fixtureRoot 'explicit-qt'
New-Item -ItemType Directory -Path $fixtureQtBuild, (Join-Path $fixtureCmakeQt 'lib/cmake/Qt6'), (Join-Path $fixtureExplicitQt 'bin') -Force | Out-Null
[IO.File]::WriteAllText((Join-Path $fixtureQtBuild 'CMakeCache.txt'), @(
    'CMAKE_BUILD_TYPE:STRING=Release'
    "CMAKE_CXX_COMPILER:FILEPATH=$(Join-Path $fixtureRoot 'compiler/bin/g++.exe')"
    "Qt6_DIR:PATH=$(Join-Path $fixtureCmakeQt 'lib/cmake/Qt6')"
) -join "`r`n")
[IO.File]::WriteAllText((Join-Path $fixtureExplicitQt 'bin/windeployqt.exe'), 'synthetic executable; never launched')
Invoke-ExpectedRejection -Action {
    & (Join-Path $repoRoot 'scripts/package_portable.ps1') -BuildDir $fixtureQtBuild `
        -OutputDir "artifacts/.package-safety-fixture-$fixtureId" -QtDir $fixtureExplicitQt -NoBuild
} -MessagePattern 'does not match Qt6_DIR.*CMakeCache' -Description 'explicit QtDir mismatch'

$fixtureInstallerBuild = Join-Path $fixtureRoot 'installer-build-metadata'
New-Item -ItemType Directory -Path $fixtureInstallerBuild | Out-Null
[IO.File]::WriteAllText((Join-Path $fixtureInstallerBuild 'CMakeCache.txt'), 'CMAKE_BUILD_TYPE:STRING=Debug')
Invoke-ExpectedRejection -Action {
    & (Join-Path $repoRoot 'scripts/installer/build_installer.ps1') `
        -RuntimeDir "artifacts/.installer-safety-fixture-$fixtureId" -BuildDir $fixtureInstallerBuild `
        -NsisCompiler (Join-Path $fixtureRoot 'not-used-makensis.exe')
} -MessagePattern 'Release build' -Description 'installer non-Release build metadata'

$tokens = $null
$parseErrors = $null
$installerAst = [Management.Automation.Language.Parser]::ParseFile(
    (Join-Path $repoRoot 'scripts/installer/build_installer.ps1'), [ref]$tokens, [ref]$parseErrors)
if ($parseErrors.Count) { throw 'Installer script syntax is invalid.' }
$reader = $installerAst.Find({ param($node)
    $node -is [Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -eq 'Read-BuildValue'
}, $true)
if (-not $reader) { throw 'Installer build provenance reader is missing.' }
. ([scriptblock]::Create($reader.Extent.Text))
$build = $fixtureInstallerBuild
$header = 'inline constexpr auto buildConfiguration = FOREST_BUILD_CONFIGURATION;'
$commands = @(@{ command = 'g++ -DFOREST_BUILD_CONFIGURATION=\"Release\"' })
[IO.File]::WriteAllText((Join-Path $build 'compile_commands.json'),
    (ConvertTo-Json -InputObject $commands -Compress))
if ((Read-BuildValue 'buildConfiguration') -cne 'Release') {
    $regressionFailures.Add('Single Release compile configuration was truncated or rejected.')
}

if ($regressionFailures.Count) {
    throw "Release-script regression checks failed:`r`n - $($regressionFailures -join "`r`n - ")"
}

Write-Output "Package and runtime preflight self-test passed. Synthetic fixture retained at: $fixtureRoot"
