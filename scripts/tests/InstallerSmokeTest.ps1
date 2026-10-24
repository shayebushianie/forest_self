param([Parameter(Mandatory = $true)][string]$Installer)

$ErrorActionPreference = 'Stop'
. (Join-Path (Split-Path -Parent $PSScriptRoot) 'RuntimeSmoke.ps1')
$Installer = [IO.Path]::GetFullPath($Installer)
$manifest = Get-Content -LiteralPath "$Installer.manifest.json" -Raw | ConvertFrom-Json
if ((Get-FileHash -LiteralPath $Installer).Hash.ToLower() -ne $manifest.installer.sha256) { throw 'Installer hash differs from release manifest.' }
$root = Join-Path ([IO.Path]::GetTempPath()) ('forest-installer-smoke-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $root | Out-Null
$checks = [Collections.Generic.List[string]]::new()
function Assert-Test([bool]$condition, [string]$name) {
    if (-not $condition) { throw "Failed: $name. Fixture retained at $root" }
    $checks.Add($name)
}
function Run-Setup([string]$destination, [int]$expected) {
    $process = Start-Process -FilePath $Installer -ArgumentList "/S /VALIDATE /D=$destination" -WindowStyle Hidden -PassThru
    if (-not $process.WaitForExit(60000)) { throw 'Installer did not exit within one minute.' }
    Assert-Test ($process.ExitCode -eq $expected) "setup exit $expected : $(Split-Path $destination -Leaf)"
}
function Write-Fixture([string]$path, [string]$text) {
    [IO.File]::WriteAllText($path, $text, [Text.UTF8Encoding]::new($false))
}
function Registration-Snapshot {
    $keys = @('HKCU:\Software\ForestFocus','HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\ForestFocus')
    return (@($keys | ForEach-Object {
        if (Test-Path -LiteralPath $_) { Get-ItemProperty -LiteralPath $_ | Select-Object * -ExcludeProperty PS* }
    }) | ConvertTo-Json -Depth 4 -Compress)
}
$registration = Registration-Snapshot
$destination = Join-Path $root 'custom install'
Run-Setup $destination 0
Assert-Test (-not (Test-Path "$destination/forest.portable")) 'installed payload has no portable marker'
Assert-Test (-not (Test-Path "$destination/app_data")) 'installed payload has no user data'
foreach ($file in $manifest.files) {
    Assert-Test ((Get-FileHash -LiteralPath (Join-Path $destination $file.path)).Hash.ToLower() -eq $file.sha256) "payload hash : $($file.path)"
}

$foreign = Join-Path $root 'foreign'
New-Item -ItemType Directory -Path $foreign | Out-Null
Write-Fixture "$foreign/note.txt" 'foreign content'
Run-Setup $foreign 2
Assert-Test ((Get-Content "$foreign/note.txt" -Raw) -eq 'foreign content' -and -not (Test-Path "$foreign/forest.exe")) 'foreign content preserved'
$portable = Join-Path $root 'portable'
New-Item -ItemType Directory -Path $portable | Out-Null
Write-Fixture "$portable/forest.portable" 'portable'
Run-Setup $portable 2
Assert-Test ((Get-Content "$portable/forest.portable" -Raw) -eq 'portable') 'portable marker preserved'

$outside = Join-Path $root 'link-target'
New-Item -ItemType Directory -Path $outside | Out-Null
Write-Fixture "$outside/note.txt" 'link target'
$linked = Join-Path $root 'linked'
New-Item -ItemType Junction -Path $linked -Target $outside | Out-Null
Run-Setup $linked 2
Assert-Test (-not (Test-Path "$outside/forest.exe")) 'linked destination untouched'
$nested = Join-Path $root 'nested-link'
New-Item -ItemType Directory -Path $nested | Out-Null
Write-Fixture "$nested/.forest-install" "ForestFocus-Installed-v1`r`n1`r`n"
Write-Fixture "$nested/forest.exe" 'owned fixture'
New-Item -ItemType Junction -Path "$nested/platforms" -Target $outside | Out-Null
Run-Setup $nested 2
Assert-Test ((Get-Content "$nested/forest.exe" -Raw) -eq 'owned fixture' -and -not (Test-Path "$outside/qwindows.dll")) 'preflight prevents partial overwrite'

New-Item -ItemType Directory -Path "$destination/app_data" | Out-Null
Write-Fixture "$destination/app_data/keep.txt" 'test data'
Write-Fixture "$destination/note.txt" 'unknown root file'
Write-Fixture "$destination/platforms/note.txt" 'unknown plugin file'
Run-Setup $destination 0
Assert-Test ((Get-Content "$destination/app_data/keep.txt" -Raw) -eq 'test data') 'same-version upgrade preserves data'
Assert-Test ((Get-FileHash "$destination/forest.exe").Hash.ToLower() -eq ($manifest.files | Where-Object path -eq 'forest.exe').sha256) 'upgrade executable hash'

# The installed directory has no data marker; add one only to isolate this launch from the real Windows profile.
Write-Fixture "$destination/forest.portable" ''
$startup = Invoke-ForestRuntimeSmoke "$destination/forest.exe"
Assert-Test ($startup.ExitCode -eq 0 -and $startup.DeveloperPathExcluded) 'runtime clean startup/exit without developer PATH'

$uninstaller = Start-Process -FilePath "$destination/Uninstall.exe" -ArgumentList '/S' -WindowStyle Hidden -PassThru
Assert-Test ($uninstaller.WaitForExit(30000) -and $uninstaller.ExitCode -eq 0) 'uninstaller launcher exit'
$deadline = [DateTime]::UtcNow.AddSeconds(20)
while ((Test-Path "$destination/forest.exe") -and [DateTime]::UtcNow -lt $deadline) { Start-Sleep -Milliseconds 200 }
foreach ($file in $manifest.files) { Assert-Test (-not (Test-Path -LiteralPath (Join-Path $destination $file.path))) "uninstalled : $($file.path)" }
Assert-Test (-not (Test-Path "$destination/.forest-install") -and -not (Test-Path "$destination/release-manifest.json")) 'installer metadata removed'
Assert-Test ((Get-Content "$destination/app_data/keep.txt" -Raw) -eq 'test data') 'uninstall preserves test data'
Assert-Test ((Get-Content "$destination/note.txt" -Raw) -eq 'unknown root file') 'uninstall preserves unknown root file'
Assert-Test ((Get-Content "$destination/platforms/note.txt" -Raw) -eq 'unknown plugin file') 'uninstall preserves unknown plugin file'
Assert-Test ((Get-Content "$outside/note.txt" -Raw) -eq 'link target') 'link target remains unchanged'
Assert-Test ((Registration-Snapshot) -ceq $registration) 'validation mode leaves real registration unchanged'
$result = [ordered]@{ fixture = $root; installerSha256 = $manifest.installer.sha256; checks = $checks.ToArray(); passed = $true; launchIsolation = 'test-only portable marker'; registration = 'skipped by VALIDATE'; createdUtc = [DateTime]::UtcNow.ToString('o') }
$result | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath "$root/result.json" -Encoding utf8
[pscustomobject]@{ Passed = $true; Checks = $checks.Count; Evidence = "$root/result.json" }
