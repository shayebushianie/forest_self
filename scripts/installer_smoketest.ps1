param(
    [Parameter(Mandatory = $true)][string]$Installer,
    [Parameter(Mandatory = $true)][string]$InstallDir,
    [string]$LogPath = 'artifacts/installer-smoke.log'
)

$ErrorActionPreference = 'Stop'

$expected = @(
    'app\forest.exe', 'app\Qt6Sql.dll',
    'app\platforms\qwindows.dll', 'app\sqldrivers\qsqlite.dll'
)
$forbidden = @('app\forest.portable', 'app\app_data')

function Resolve-AbsolutePath([string]$Path) {
    if ([IO.Path]::IsPathRooted($Path)) {
        return [IO.Path]::GetFullPath($Path)
    }

    return [IO.Path]::GetFullPath((Join-Path (Get-Location).Path $Path))
}

function Write-SmokeLog([string]$Message) {
    $line = "{0} {1}" -f (Get-Date -Format o), $Message
    Add-Content -LiteralPath $logPath -Value $line -Encoding utf8
    Write-Host $line
}

function Assert-Smoke([bool]$Condition, [string]$Message) {
    if (-not $Condition) {
        Write-SmokeLog "FAIL: $Message"
        throw $Message
    }

    Write-SmokeLog "PASS: $Message"
}

function Test-ReparsePoint([IO.FileSystemInfo]$Item) {
    return (($Item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0)
}

function Find-ReparsePoint([string]$Path) {
    $rootItem = Get-Item -LiteralPath $Path -Force
    if (Test-ReparsePoint $rootItem) {
        return $rootItem.FullName
    }
    if (-not $rootItem.PSIsContainer) {
        return $null
    }

    $directories = [Collections.Generic.Stack[string]]::new()
    $directories.Push($rootItem.FullName)
    while ($directories.Count -gt 0) {
        $directory = $directories.Pop()
        foreach ($child in Get-ChildItem -LiteralPath $directory -Force) {
            if (Test-ReparsePoint $child) {
                return $child.FullName
            }
            if ($child.PSIsContainer) {
                $directories.Push($child.FullName)
            }
        }
    }

    return $null
}

function Assert-SafeTemporaryInstallPath([string]$Path) {
    $resolvedPath = [IO.Path]::GetFullPath($Path)
    if (-not $resolvedPath.StartsWith($tempRoot, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing cleanup outside the temporary root: $resolvedPath"
    }
    if (-not (Test-Path -LiteralPath $resolvedPath)) {
        return $resolvedPath
    }

    $reparsePoint = Find-ReparsePoint $resolvedPath
    if ($reparsePoint) {
        throw "Refusing cleanup because InstallDir contains a reparse point: $reparsePoint"
    }

    return $resolvedPath
}

function Remove-NonTraversingPath([string]$Path) {
    $item = Get-Item -LiteralPath $Path -Force
    if (Test-ReparsePoint $item) {
        throw "Refusing to delete reparse point: $($item.FullName)"
    }
    if (-not $item.PSIsContainer) {
        Remove-Item -LiteralPath $item.FullName -Force
        return
    }

    foreach ($child in Get-ChildItem -LiteralPath $item.FullName -Force) {
        Remove-NonTraversingPath $child.FullName
    }
    Remove-Item -LiteralPath $item.FullName -Force
}

function Remove-ValidatedTemporaryInstallDirectory([string]$Path) {
    $cleanupPath = Assert-SafeTemporaryInstallPath $Path
    if (-not (Test-Path -LiteralPath $cleanupPath)) {
        return
    }

    Remove-NonTraversingPath $cleanupPath
    Write-SmokeLog "CLEANUP: Removed temporary installation directory: $cleanupPath"
}

$installerPath = Resolve-AbsolutePath $Installer
$installPath = Resolve-AbsolutePath $InstallDir
$logPath = Resolve-AbsolutePath $LogPath
$logDirectory = Split-Path -Parent $logPath
New-Item -ItemType Directory -Force -Path $logDirectory | Out-Null
Set-Content -LiteralPath $logPath -Value "Installer smoke test started $(Get-Date -Format o)" -Encoding utf8

$tempRoot = [IO.Path]::GetFullPath(
    $(if ($env:RUNNER_TEMP) { $env:RUNNER_TEMP } else { [IO.Path]::GetTempPath() })
).TrimEnd('\') + '\'
$sentinelCreated = $false
$sentinelPath = $null
$installerStarted = $false
$appProcess = $null
$succeeded = $false
$primaryFailure = $null
$cleanupFailure = $null

try {
    Assert-Smoke (Test-Path -LiteralPath $installerPath -PathType Leaf) "Installer exists: $installerPath"
    Assert-Smoke ($installPath.StartsWith($tempRoot, [StringComparison]::OrdinalIgnoreCase)) "InstallDir is below the temporary root: $tempRoot"
    Assert-Smoke (-not [string]::IsNullOrWhiteSpace($env:LOCALAPPDATA)) 'LOCALAPPDATA is available for preservation verification'

    $productionInstall = [IO.Path]::GetFullPath((Join-Path $env:LOCALAPPDATA 'Programs\ForestFocus'))
    Assert-Smoke (-not ($installPath -eq $productionInstall -and (Test-Path -LiteralPath $productionInstall))) 'InstallDir is not an existing production installation directory'
    Assert-Smoke (-not (Test-Path -LiteralPath $installPath)) "InstallDir does not already exist: $installPath"
    Assert-Smoke (-not ($installPath -match '\s')) 'InstallDir does not contain whitespace required by NSIS /D='

    $runningForest = @(Get-Process -Name 'forest' -ErrorAction SilentlyContinue)
    Assert-Smoke ($runningForest.Count -eq 0) 'No forest process is running before installation'

    $localDataPath = Join-Path $env:LOCALAPPDATA 'Forest'
    if (Test-Path -LiteralPath $localDataPath) {
        Write-SmokeLog "PASS: Existing LocalAppData preservation verification is skipped; directory is never modified: $localDataPath"
    } else {
        New-Item -ItemType Directory -Force -Path $localDataPath | Out-Null
        $sentinelPath = Join-Path $localDataPath ("installer-smoke-sentinel-{0}.txt" -f [guid]::NewGuid())
        New-Item -ItemType File -Path $sentinelPath | Out-Null
        $sentinelCreated = $true
        Write-SmokeLog "PASS: Created preservation sentinel: $sentinelPath"
    }

    Write-SmokeLog "RUN: Starting installer: $installerPath"
    $installerStarted = $true
    $installerProcess = Start-Process -FilePath $installerPath -ArgumentList '/S', "/D=$installPath" -Wait -PassThru
    Assert-Smoke ($installerProcess.ExitCode -eq 0) "Installer exited with code 0"

    foreach ($relative in $expected) {
        Assert-Smoke (Test-Path -LiteralPath (Join-Path $installPath $relative) -PathType Leaf) "Expected runtime exists: $relative"
    }
    foreach ($relative in $forbidden) {
        Assert-Smoke (-not (Test-Path -LiteralPath (Join-Path $installPath $relative))) "Forbidden installed content is absent: $relative"
    }

    $appPath = Join-Path $installPath 'app\forest.exe'
    $forbiddenRuntimeFiles = @(
        Get-ChildItem -LiteralPath (Join-Path $installPath 'app') -Recurse -Force -File |
            Where-Object { $_.Name -match '\.sqlite$|\.dat$|\.log$|\.bak$' }
    )
    Assert-Smoke ($forbiddenRuntimeFiles.Count -eq 0) 'No SQLite, DAT, LOG, or BAK file is installed below app'

    Write-SmokeLog "RUN: Starting application: $appPath"
    $appProcess = Start-Process -FilePath $appPath -WindowStyle Hidden -PassThru
    Start-Sleep -Seconds 3
    Assert-Smoke (-not $appProcess.HasExited) 'Application process remains live after three seconds'
    Stop-Process -InputObject $appProcess -ErrorAction Stop
    Assert-Smoke ($appProcess.WaitForExit(10000)) 'Application process stopped within ten seconds'
    Write-SmokeLog "PASS: Stopped only the application process started by this smoke test (PID $($appProcess.Id))"

    $uninstallerPath = Join-Path $installPath 'Uninstall.exe'
    Assert-Smoke (Test-Path -LiteralPath $uninstallerPath -PathType Leaf) 'Uninstaller exists before silent uninstallation'
    Write-SmokeLog "RUN: Starting uninstaller: $uninstallerPath"
    $uninstallerProcess = Start-Process -FilePath $uninstallerPath -ArgumentList '/S' -Wait -PassThru
    Assert-Smoke ($uninstallerProcess.ExitCode -eq 0) 'Uninstaller exited with code 0'
    Assert-Smoke (-not (Test-Path -LiteralPath $appPath)) 'Application executable is absent after uninstallation'
    if ($sentinelCreated) {
        Assert-Smoke (Test-Path -LiteralPath $sentinelPath -PathType Leaf) 'Created LocalAppData sentinel remains after uninstallation'
    }

    $succeeded = $true
}
catch {
    $primaryFailure = $_.Exception
}
finally {
    try {
        if ($appProcess -and -not $appProcess.HasExited) {
            Stop-Process -InputObject $appProcess -ErrorAction Stop
            if (-not $appProcess.WaitForExit(10000)) {
                throw "Application process did not stop during cleanup (PID $($appProcess.Id))"
            }
            Write-SmokeLog "CLEANUP: Stopped the application process started by this smoke test (PID $($appProcess.Id))"
        }

        if ($installerStarted -and (Test-Path -LiteralPath $installPath)) {
            Remove-ValidatedTemporaryInstallDirectory $installPath
        }
    }
    catch {
        $cleanupFailure = $_.Exception
    }
}

if ($primaryFailure -or $cleanupFailure) {
    $failureMessages = @()
    if ($primaryFailure) {
        $failureMessages += $primaryFailure.Message
    }
    if ($cleanupFailure) {
        $failureMessages += "Cleanup failure: $($cleanupFailure.Message)"
    }
    Write-SmokeLog ("FINAL: FAIL: " + ($failureMessages -join ' | '))
    if ($primaryFailure) {
        throw $primaryFailure
    }
    throw $cleanupFailure
}

Assert-Smoke $succeeded 'Install, startup, forbidden-content, and uninstall assertions passed'
Write-SmokeLog 'FINAL: PASS: install, startup, forbidden-content, and uninstall assertions passed'
exit 0
