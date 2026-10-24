param(
    [Parameter(Mandatory = $true)][string]$RuntimeDir,
    [Parameter(Mandatory = $true)][string]$BuildDir,
    [string]$OutputName = '',
    [string]$NsisCompiler = '',
    [string]$VerificationId = ''
)

$ErrorActionPreference = 'Stop'
$verificationOnly = $VerificationId.Length -gt 0
if ($verificationOnly) {
    if ($VerificationId -notmatch '^[A-Za-z0-9]{8,24}$') { throw 'VerificationId must contain 8-24 ASCII letters or digits.' }
    $VerificationId = $VerificationId.ToLowerInvariant()
}
$repoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$workspace = Split-Path -Parent (Split-Path -Parent $repoRoot)
. (Join-Path $repoRoot 'scripts/PackageSafety.ps1')
$runtime = Get-ForestPortableFullPath $RuntimeDir $repoRoot
$build = Get-ForestPortableFullPath $BuildDir $repoRoot
$cmakeCache = Join-Path $build 'CMakeCache.txt'
if (-not (Test-Path -LiteralPath $cmakeCache -PathType Leaf)) { throw 'CMakeCache.txt missing; cannot verify Release build configuration.' }
$buildTypeLine = Get-Content -LiteralPath $cmakeCache |
    Where-Object { $_ -match '^CMAKE_BUILD_TYPE(:[^=]+)?=(.*)$' } |
    Select-Object -First 1
$buildConfiguration = ''
if ($buildTypeLine -and ($buildTypeLine -match '^CMAKE_BUILD_TYPE(:[^=]+)?=(.*)$')) {
    $buildConfiguration = $Matches[2]
}
if ($buildConfiguration -cne 'Release') { throw "Installer packages require a CMake Release build (found '$buildConfiguration')." }
$release = Join-Path $workspace 'release'
foreach ($path in @($runtime, $build, $release)) { Assert-ForestNoReparsePoints $path }
foreach ($path in @("$runtime/forest.exe", "$build/forest.exe", "$runtime/app_data")) { Assert-ForestNoReparsePoints $path }
if (-not $NsisCompiler) { $NsisCompiler = Join-Path $workspace 'tools/nsis-3.12/nsis-3.12/makensis.exe' }
if (-not (Test-Path -LiteralPath $NsisCompiler -PathType Leaf)) { throw 'Pass -NsisCompiler with an existing NSIS compiler.' }
if (-not (Test-ForestPortablePathWithin $runtime (Join-Path $repoRoot 'artifacts'))) { throw 'Runtime input must be a dedicated artifacts package.' }
if ((Get-FileHash "$runtime/forest.exe").Hash -ne (Get-FileHash "$build/forest.exe").Hash) { throw 'Runtime executable differs from the validated build.' }
foreach ($required in @('Qt6Core.dll','Qt6Gui.dll','Qt6Widgets.dll','Qt6Sql.dll','sqldrivers/qsqlite.dll','platforms/qwindows.dll','libgcc_s_seh-1.dll','libstdc++-6.dll','libwinpthread-1.dll')) {
    if (-not (Test-Path -LiteralPath (Join-Path $runtime $required) -PathType Leaf)) { throw "Missing runtime: $required" }
}
if (Test-Path "$runtime/app_data") {
    if (@(Get-ChildItem -LiteralPath "$runtime/app_data" -Force).Count) { throw 'Do not package a runtime directory that has been used.' }
}

$header = [IO.File]::ReadAllText("$build/generated/BuildProvenance.h")
function Read-BuildValue([string]$name) {
    $match = [regex]::Match($header, $name + '\s*=\s*R"forest\(([^)]*)\)forest"')
    if ($match.Success) { return $match.Groups[1].Value }
    if ($name -eq 'buildConfiguration' -and $header -match '\bbuildConfiguration\s*=\s*FOREST_BUILD_CONFIGURATION\s*;') {
        $compileCommandsPath = Join-Path $build 'compile_commands.json'
        if (-not (Test-Path -LiteralPath $compileCommandsPath -PathType Leaf)) {
            throw 'Build provenance missing: buildConfiguration.'
        }
        $compileCommands = Get-Content -LiteralPath $compileCommandsPath -Raw | ConvertFrom-Json
        $configurations = @(
            foreach ($entry in $compileCommands) {
                $command = if ($entry.arguments) { [string]::Join(' ', $entry.arguments) } else { $entry.command }
                $configurationMatch = [regex]::Match($command, '-DFOREST_BUILD_CONFIGURATION=\\?"([^"\\]+)\\?"')
                if ($configurationMatch.Success) { $configurationMatch.Groups[1].Value }
            }
        ) | Sort-Object -Unique
        if ($configurations.Count -eq 1) { return @($configurations)[0] }
    }
    throw "Build provenance missing or ambiguous: $name"
}
$provenanceConfiguration = Read-BuildValue 'buildConfiguration'
if ($provenanceConfiguration -cne $buildConfiguration) {
    throw "Build provenance configuration '$provenanceConfiguration' does not match CMake configuration '$buildConfiguration'."
}
$version = Read-BuildValue 'appVersion'
if ($version -notmatch '^\d+\.\d+\.\d+$') { throw 'Invalid application version.' }
if (-not $OutputName) {
    if ($verificationOnly) {
        $OutputName = "ForestFocus_Verification_${VerificationId}_${version}_$(Get-Date -Format yyyyMMdd).exe"
    } else {
        $OutputName = "ForestFocus_Setup_${version}_$(Get-Date -Format yyyyMMdd).exe"
    }
}
if ($OutputName -notmatch '^[A-Za-z0-9_.-]+\.exe$') { throw 'OutputName must be a plain installer filename.' }
if ($verificationOnly -and -not $OutputName.StartsWith("ForestFocus_Verification_${VerificationId}_", [StringComparison]::Ordinal)) {
    throw 'Verification installer filename must identify its verification ID.'
}
$output = Join-Path $release $OutputName
if ((Test-Path $output) -or (Test-Path "$output.manifest.json")) { throw 'Release output already exists; choose a new name, never overwrite it.' }

$files = @()
$pending = [Collections.Generic.Queue[string]]::new()
$pending.Enqueue($runtime)
while ($pending.Count) {
    $directory = $pending.Dequeue()
    Assert-ForestNoReparsePoints $directory
    foreach ($item in Get-ChildItem -LiteralPath $directory -Force) {
        Assert-ForestNoReparsePoints $item.FullName
        $relative = $item.FullName.Substring($runtime.Length + 1)
        if ($relative -in @('app_data','forest.portable')) { continue }
        if ($relative -notmatch '^[A-Za-z0-9_.+\\-]+$') { throw "Unsupported runtime path: $relative" }
        if ($item.PSIsContainer) {
            if (($relative -split '\\')[0] -notin @('generic','iconengines','imageformats','networkinformation','platforms','sqldrivers','styles','tls','translations')) { throw "Unknown runtime directory: $relative" }
            $pending.Enqueue($item.FullName)
        } else {
            if ($relative -ne 'forest.exe' -and $item.Extension -notin @('.dll','.qm')) { throw "Unexpected runtime file: $relative" }
            $files += [pscustomobject]@{ path = $relative; bytes = $item.Length; sha256 = (Get-FileHash -LiteralPath $item.FullName).Hash.ToLower() }
        }
    }
}
$files = @($files | Sort-Object path)
New-Item -ItemType Directory -Path $release -Force | Out-Null
$inputs = Join-Path $release ('.installer-inputs-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $inputs | Out-Null
$manifestFormat = if ($verificationOnly) { 'forest-verification-v1' } else { 'forest-release-v1' }
$manifest = [ordered]@{
    format = $manifestFormat; appVersion = $version; sourceRevision = (Read-BuildValue 'revision')
    sourceFingerprint = (Read-BuildValue 'sourceFingerprint'); qtVersion = (Read-BuildValue 'qtVersion')
    compiler = (Read-BuildValue 'compiler'); configuration = $buildConfiguration; sqliteSchema = 4
    createdUtc = [DateTime]::UtcNow.ToString('o'); installedData = '%LOCALAPPDATA%/Forest'
    files = $files
    packagingInputs = @('build_installer.ps1','forest_installer.nsi') | ForEach-Object {
        @{ path = "scripts/installer/$_"; sha256 = (Get-FileHash (Join-Path $PSScriptRoot $_)).Hash.ToLower() }
    }
}
if ($verificationOnly) {
    $productName = "Forest 专注森林 Verification-$VerificationId"
    $productRegKey = "Software\ForestFocus-Verification-$VerificationId"
    $uninstallRegKey = "Software\Microsoft\Windows\CurrentVersion\Uninstall\ForestFocus-Verification-$VerificationId"
    $ownerMarker = ".forest-install-verification-$VerificationId"
    $ownerMarkerValue = "ForestFocus-Installed-Verification-$VerificationId-v1"
    $manifest['verificationOnly'] = $true
    $manifest['deliverable'] = $false
    $manifest['verificationId'] = $VerificationId
    $manifest['installedData'] = 'not exercised; runtime launch is intentionally omitted'
    $manifest['verificationIdentity'] = [ordered]@{
        productName = $productName
        productRegKey = $productRegKey
        uninstallRegKey = $uninstallRegKey
        defaultInstallDir = "%LOCALAPPDATA%/Programs/ForestFocus-Verification-$VerificationId"
        ownerMarker = $ownerMarker
        ownerMarkerValue = $ownerMarkerValue
    }
}
$portableZip = "$runtime.zip"
if (Test-Path -LiteralPath $portableZip) {
    Assert-ForestNoReparsePoints $portableZip
    $manifest['portable'] = @{ name = (Split-Path $portableZip -Leaf); bytes = (Get-Item $portableZip).Length; sha256 = (Get-FileHash $portableZip).Hash.ToLower() }
}
$manifest | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath "$inputs/release-manifest.json" -Encoding utf8
function Nsis-Path([string]$path) {
    if ($path -match '[\$"\r\n]') { throw 'Paths containing NSIS metacharacters are not accepted.' }
    return $path.Replace('/', '\')
}
$installLines = [Collections.Generic.List[string]]::new()
$uninstallLines = [Collections.Generic.List[string]]::new()
$directories = @($files | ForEach-Object { Split-Path $_.path -Parent } | Where-Object { $_ } | Sort-Object -Unique)
foreach ($generated in @('Uninstall.exe','release-manifest.json')) {
    $installLines.Add('Push "$INSTDIR\' + $generated + '"')
    $installLines.Add('Call CheckPath')
}
foreach ($file in $files) {
    $installLines.Add('Push "$INSTDIR\' + $file.path + '"')
    $installLines.Add('Call CheckPath')
}
foreach ($directory in $directories) {
    $uninstallLines.Add('Push "$INSTDIR\' + $directory + '"')
    $uninstallLines.Add('Call un.CheckPath')
}
foreach ($file in $files) {
    $parent = Split-Path $file.path -Parent
    $installLines.Add('SetOutPath "$INSTDIR' + $(if ($parent) { '\' + $parent }) + '"')
    $installLines.Add('File "' + (Nsis-Path (Join-Path $runtime $file.path)) + '"')
    $uninstallLines.Add('Delete "$INSTDIR\' + $file.path + '"')
}
$installLines.Add('SetOutPath "$INSTDIR"')
$installLines.Add('File "' + (Nsis-Path "$inputs/release-manifest.json") + '"')
$uninstallLines.Add('Delete "$INSTDIR\release-manifest.json"')
foreach ($directory in $directories | Sort-Object Length -Descending) { $uninstallLines.Add('RMDir "$INSTDIR\' + $directory + '"') }
$installLines | Set-Content -LiteralPath "$inputs/install-files.nsh" -Encoding utf8
$uninstallLines | Set-Content -LiteralPath "$inputs/uninstall-files.nsh" -Encoding utf8
$compilerArguments = @(
    '/V3', "/DPRODUCT_VERSION=$version", "/DOUTPUT_FILE=$(Nsis-Path $output)",
    "/DINPUTS_DIR=$(Nsis-Path $inputs)"
)
if ($verificationOnly) { $compilerArguments += "/DVERIFICATION_ID=$VerificationId" }
$compilerArguments += (Join-Path $PSScriptRoot 'forest_installer.nsi')
& $NsisCompiler @compilerArguments
if ($LASTEXITCODE -ne 0 -or -not (Test-Path $output)) { throw 'NSIS build failed; inputs retained for diagnosis.' }
$manifest['installer'] = @{ name = $OutputName; bytes = (Get-Item $output).Length; sha256 = (Get-FileHash $output).Hash.ToLower() }
$manifest | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath "$output.manifest.json" -Encoding utf8
[pscustomobject]@{ Installer = $output; Manifest = "$output.manifest.json"; Sha256 = $manifest.installer.sha256 }
