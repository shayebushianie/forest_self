param([Parameter(Mandatory = $true)][string]$Installer)

$ErrorActionPreference = 'Stop'
if ($env:OS -ne 'Windows_NT') { throw 'This registration smoke test requires Windows.' }

$Installer = [IO.Path]::GetFullPath($Installer)
$manifestPath = "$Installer.manifest.json"
if (-not (Test-Path -LiteralPath $Installer -PathType Leaf) -or -not (Test-Path -LiteralPath $manifestPath -PathType Leaf)) {
    throw 'Installer and adjacent manifest are required.'
}
$manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
if ($manifest.format -cne 'forest-verification-v1' -or $manifest.verificationOnly -ne $true -or $manifest.deliverable -ne $false) {
    throw 'A verification-only installer manifest is required.'
}
$verificationId = [string]$manifest.verificationId
if ($verificationId -notmatch '^[a-z0-9]{8,24}$') { throw 'Manifest has an invalid verification ID.' }
if ($manifest.installer.name -cne (Split-Path -Leaf $Installer) -or
    $manifest.installer.name -notlike "ForestFocus_Verification_${verificationId}_*.exe") {
    throw 'Installer filename does not identify this verification build.'
}
$expectedHash = [string]$manifest.installer.sha256
if ($expectedHash -notmatch '^[a-f0-9]{64}$' -or
    (Get-FileHash -LiteralPath $Installer).Hash.ToLowerInvariant() -cne $expectedHash) {
    throw 'Installer hash differs from its verification manifest.'
}

$identity = $manifest.verificationIdentity
$expectedIdentity = @{
    productName = "Forest 专注森林 Verification-$verificationId"
    productRegKey = "Software\ForestFocus-Verification-$verificationId"
    uninstallRegKey = "Software\Microsoft\Windows\CurrentVersion\Uninstall\ForestFocus-Verification-$verificationId"
    defaultInstallDir = "%LOCALAPPDATA%/Programs/ForestFocus-Verification-$verificationId"
    ownerMarker = ".forest-install-verification-$verificationId"
    ownerMarkerValue = "ForestFocus-Installed-Verification-$verificationId-v1"
}
foreach ($name in $expectedIdentity.Keys) {
    if ([string]$identity.$name -cne $expectedIdentity[$name]) { throw 'Manifest verification identity is inconsistent.' }
}

$productKeyPath = "HKCU:\$($identity.productRegKey)"
$uninstallKeyPath = "HKCU:\$($identity.uninstallRegKey)"
$desktopRoot = [Environment]::GetFolderPath('DesktopDirectory')
$programsRoot = [Environment]::GetFolderPath('Programs')
if (-not $desktopRoot -or -not $programsRoot) { throw 'Could not resolve current-user shortcut locations.' }
$programFolder = Join-Path $programsRoot $identity.productName
$shortcuts = [ordered]@{
    desktop = Join-Path $desktopRoot ($identity.productName + '.lnk')
    program = Join-Path $programFolder ($identity.productName + '.lnk')
    uninstall = Join-Path $programFolder '卸载.lnk'
}

$productionKeys = @(
    'Software\ForestFocus',
    'Software\Microsoft\Windows\CurrentVersion\Uninstall\ForestFocus'
)
$productionShortcuts = @(
    (Join-Path $desktopRoot 'Forest 专注森林.lnk'),
    (Join-Path (Join-Path $programsRoot 'Forest 专注森林') 'Forest 专注森林.lnk'),
    (Join-Path (Join-Path $programsRoot 'Forest 专注森林') '卸载.lnk')
)

function Get-RegistryNodeSnapshot([Microsoft.Win32.RegistryKey]$Key) {
    $values = [Collections.Generic.List[object]]::new()
    foreach ($name in @($Key.GetValueNames() | Sort-Object)) {
        $values.Add([pscustomobject]@{
            name = $name
            kind = $Key.GetValueKind($name).ToString()
            data = $Key.GetValue($name, $null, [Microsoft.Win32.RegistryValueOptions]::DoNotExpandEnvironmentNames)
        })
    }
    $children = [Collections.Generic.List[object]]::new()
    foreach ($name in @($Key.GetSubKeyNames() | Sort-Object)) {
        $child = $Key.OpenSubKey($name, $false)
        if ($null -ne $child) {
            try { $children.Add([pscustomobject]@{ name = $name; node = (Get-RegistryNodeSnapshot $child) }) }
            finally { $child.Close() }
        }
    }
    return [pscustomobject]@{ values = @($values.ToArray()); children = @($children.ToArray()) }
}

function Get-StringSha256([string]$Value) {
    $sha = [Security.Cryptography.SHA256]::Create()
    try {
        $bytes = [Text.UTF8Encoding]::new($false).GetBytes($Value)
        return ([BitConverter]::ToString($sha.ComputeHash($bytes))).Replace('-', '').ToLowerInvariant()
    } finally { $sha.Dispose() }
}

function Get-RegistryFingerprint([string]$SubKey) {
    $key = [Microsoft.Win32.Registry]::CurrentUser.OpenSubKey($SubKey, $false)
    if ($null -eq $key) { return 'absent' }
    try {
        $snapshot = Get-RegistryNodeSnapshot $key
        $serialized = ConvertTo-Json -InputObject $snapshot -Depth 32 -Compress
        return (Get-StringSha256 $serialized)
    } finally { $key.Close() }
}

function Get-FileFingerprint([string]$Path) {
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) { return 'absent' }
    return (Get-FileHash -LiteralPath $Path).Hash.ToLowerInvariant()
}

function Get-ProductionSnapshot {
    $registry = [ordered]@{}
    for ($i = 0; $i -lt $productionKeys.Count; $i++) {
        $registry["key$i"] = Get-RegistryFingerprint $productionKeys[$i]
    }
    $links = [ordered]@{}
    for ($i = 0; $i -lt $productionShortcuts.Count; $i++) {
        $links["shortcut$i"] = Get-FileFingerprint $productionShortcuts[$i]
    }
    return (ConvertTo-Json -InputObject ([ordered]@{ registry = $registry; shortcuts = $links }) -Depth 4 -Compress)
}

if ((Test-Path -LiteralPath $productKeyPath) -or (Test-Path -LiteralPath $uninstallKeyPath)) {
    throw 'Verification registration key already exists; refusing to continue.'
}
foreach ($shortcut in $shortcuts.Values) {
    if (Test-Path -LiteralPath $shortcut) { throw 'Verification shortcut already exists; refusing to continue.' }
}
if (Test-Path -LiteralPath $programFolder) { throw 'Verification Start Menu folder already exists; refusing to continue.' }

$productionSnapshot = Get-ProductionSnapshot
$destination = Join-Path ([IO.Path]::GetTempPath()) ("forest-registration-$verificationId-" + [Guid]::NewGuid().ToString('N'))
if (Test-Path -LiteralPath $destination) { throw 'Temporary install destination already exists.' }
$uninstaller = Join-Path $destination 'Uninstall.exe'
$checks = [Collections.Generic.List[string]]::new()
$setupProcess = $null
$uninstallProcess = $null
$uninstallAttempted = $false
$shell = $null
$uninstallComplete = $false
$failure = $null
$cleanupFailure = $null

function Assert-Test([bool]$Condition, [string]$Name) {
    if (-not $Condition) { throw "Failed: $Name." }
    $checks.Add($Name)
}

function Assert-ProductionUnchanged([string]$Phase) {
    Assert-Test ((Get-ProductionSnapshot) -ceq $productionSnapshot) "production registration hashes unchanged after $Phase"
}

function Invoke-VerificationSetup([string]$Name) {
    $arguments = '/S /D=' + $destination
    $script:setupProcess = Start-Process -FilePath $Installer -ArgumentList $arguments -PassThru -WindowStyle Hidden
    if (-not $script:setupProcess.WaitForExit(60000)) { throw 'Silent installer exceeded the one-minute timeout.' }
    $exitCode = $script:setupProcess.ExitCode
    $script:setupProcess = $null
    Assert-Test ($exitCode -eq 0) $Name
}

function Assert-ShortcutTarget([string]$Path, [string]$Target, [string]$Name) {
    Assert-Test (Test-Path -LiteralPath $Path -PathType Leaf) "$Name exists"
    $link = $shell.CreateShortcut($Path)
    try {
        $actualTarget = [IO.Path]::GetFullPath($link.TargetPath)
        Assert-Test ([string]::Equals($actualTarget, [IO.Path]::GetFullPath($Target), [StringComparison]::OrdinalIgnoreCase)) "$Name target"
    } finally { [Runtime.InteropServices.Marshal]::ReleaseComObject($link) | Out-Null }
}

function Assert-InstalledRegistration([string]$Phase) {
    $product = [Microsoft.Win32.Registry]::CurrentUser.OpenSubKey($identity.productRegKey, $false)
    Assert-Test ($null -ne $product) "$Phase product key exists"
    try {
        Assert-Test ([string]$product.GetValue('InstallDir') -ieq $destination) "$Phase install directory"
    } finally { $product.Close() }

    $uninstallKey = [Microsoft.Win32.Registry]::CurrentUser.OpenSubKey($identity.uninstallRegKey, $false)
    Assert-Test ($null -ne $uninstallKey) "$Phase uninstall key exists"
    try {
        Assert-Test ([string]$uninstallKey.GetValue('DisplayName') -ceq $identity.productName) "$Phase display name"
        Assert-Test ([string]$uninstallKey.GetValue('DisplayVersion') -ceq [string]$manifest.appVersion) "$Phase display version"
        Assert-Test ([string]$uninstallKey.GetValue('InstallLocation') -ieq $destination) "$Phase install location"
        Assert-Test ([string]$uninstallKey.GetValue('UninstallString') -ieq ('"' + $uninstaller + '"')) "$Phase uninstall command"
        Assert-Test ([int]$uninstallKey.GetValue('NoModify') -eq 1 -and [int]$uninstallKey.GetValue('NoRepair') -eq 1) "$Phase uninstall flags"
    } finally { $uninstallKey.Close() }

    $marker = Join-Path $destination $identity.ownerMarker
    Assert-Test (Test-Path -LiteralPath $marker -PathType Leaf) "$Phase verification owner marker"
    Assert-Test ([IO.File]::ReadAllText($marker) -ceq ($identity.ownerMarkerValue + "`r`n0`r`n")) "$Phase owner marker identity"
    Assert-Test (-not (Test-Path -LiteralPath (Join-Path $destination 'forest.portable'))) "$Phase has no portable marker"
    Assert-Test (-not (Test-Path -LiteralPath (Join-Path $destination 'app_data'))) "$Phase has no app data"

    Assert-ShortcutTarget $shortcuts.desktop (Join-Path $destination 'forest.exe') "$Phase desktop shortcut"
    Assert-ShortcutTarget $shortcuts.program (Join-Path $destination 'forest.exe') "$Phase Start Menu shortcut"
    Assert-ShortcutTarget $shortcuts.uninstall $uninstaller "$Phase uninstall shortcut"
}

function Test-VerificationRegistrationRemoved {
    if ((Test-Path -LiteralPath $productKeyPath) -or (Test-Path -LiteralPath $uninstallKeyPath)) { return $false }
    foreach ($shortcut in $shortcuts.Values) {
        if (Test-Path -LiteralPath $shortcut) { return $false }
    }
    return (-not (Test-Path -LiteralPath $programFolder) -and -not (Test-Path -LiteralPath $destination))
}

function Wait-VerificationRegistrationRemoved {
    $deadline = [DateTime]::UtcNow.AddSeconds(20)
    while ([DateTime]::UtcNow -lt $deadline) {
        if (Test-VerificationRegistrationRemoved) { return $true }
        Start-Sleep -Milliseconds 200
    }
    return (Test-VerificationRegistrationRemoved)
}

function Invoke-VerificationUninstall {
    if (-not (Test-Path -LiteralPath $uninstaller -PathType Leaf)) { throw 'Verification uninstaller is unavailable.' }
    $script:uninstallProcess = Start-Process -FilePath $uninstaller -ArgumentList '/S' -PassThru -WindowStyle Hidden
    $script:uninstallAttempted = $true
    if (-not $script:uninstallProcess.WaitForExit(45000)) { throw 'Silent verification uninstaller exceeded its timeout.' }
    $exitCode = $script:uninstallProcess.ExitCode
    $script:uninstallProcess = $null
    if ($exitCode -ne 0) { throw 'Verification uninstaller returned a failure exit code.' }
    if (-not (Wait-VerificationRegistrationRemoved)) { throw 'Verification registration cleanup did not finish within its timeout.' }
}

function Assert-VerificationRegistrationRemoved {
    Assert-Test (-not (Test-Path -LiteralPath $productKeyPath)) 'verification product key removed'
    Assert-Test (-not (Test-Path -LiteralPath $uninstallKeyPath)) 'verification uninstall key removed'
    foreach ($shortcut in $shortcuts.Values) {
        Assert-Test (-not (Test-Path -LiteralPath $shortcut)) 'verification shortcut removed'
    }
    Assert-Test (-not (Test-Path -LiteralPath $programFolder)) 'verification Start Menu folder removed'
    Assert-Test (-not (Test-Path -LiteralPath $destination)) 'temporary installation directory removed by uninstaller'
}

try {
    Invoke-VerificationSetup 'silent real-registration install'
    $shell = New-Object -ComObject WScript.Shell
    Assert-InstalledRegistration 'initial install'
    Assert-ProductionUnchanged 'initial install'

    Invoke-VerificationSetup 'same-version overwrite'
    Assert-InstalledRegistration 'same-version overwrite'
    Assert-ProductionUnchanged 'same-version overwrite'

    Invoke-VerificationUninstall
    Assert-VerificationRegistrationRemoved
    $uninstallComplete = $true
    Assert-ProductionUnchanged 'uninstall'
} catch {
    $failure = $_.Exception.Message
} finally {
    if (-not $uninstallComplete -and -not $uninstallAttempted -and ($null -eq $setupProcess -or $setupProcess.HasExited) -and
        ($null -eq $uninstallProcess -or $uninstallProcess.HasExited) -and
        (Test-Path -LiteralPath $uninstaller -PathType Leaf)) {
        try {
            Invoke-VerificationUninstall
            $uninstallComplete = $true
        } catch { $cleanupFailure = $_.Exception.Message }
    }
    if ($null -ne $shell) { [Runtime.InteropServices.Marshal]::ReleaseComObject($shell) | Out-Null }
    try { Assert-ProductionUnchanged 'final cleanup' }
    catch {
        if ($null -eq $failure) { $failure = $_.Exception.Message }
        else { $cleanupFailure = 'Production registration snapshot changed.' }
    }
}

if ($cleanupFailure) {
    if ($failure) { throw "$failure Cleanup issue: $cleanupFailure" }
    throw "Cleanup issue: $cleanupFailure"
}
if ($failure) { throw $failure }

[pscustomobject]@{
    Passed = $true
    VerificationId = $verificationId
    InstallerSha256 = $expectedHash
    Checks = $checks.Count
    RuntimeLaunch = 'not run; covered by InstallerSmokeTest.ps1'
}
