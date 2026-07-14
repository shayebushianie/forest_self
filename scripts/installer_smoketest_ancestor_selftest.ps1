$ErrorActionPreference = 'Stop'

function Remove-FixturePath([string]$Path) {
    if (-not (Test-Path -LiteralPath $Path)) {
        return
    }

    $item = Get-Item -LiteralPath $Path -Force
    if (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
        cmd.exe /d /s /c "rmdir `"$($item.FullName)`"" | Out-Null
        if (Test-Path -LiteralPath $item.FullName) {
            throw "Unable to remove reparse-point fixture: $($item.FullName)"
        }
        return
    }

    if (-not $item.PSIsContainer) {
        Remove-Item -LiteralPath $item.FullName -Force
        return
    }

    foreach ($child in Get-ChildItem -LiteralPath $item.FullName -Force) {
        Remove-FixturePath $child.FullName
    }
    Remove-Item -LiteralPath $item.FullName -Force
}

$fixtureRoot = Join-Path ([IO.Path]::GetTempPath()) ("forest-installer-smoke-ancestor-selftest-{0}" -f [guid]::NewGuid())
$runnerTemp = Join-Path $fixtureRoot 'runner-temp'
$outsidePath = Join-Path $fixtureRoot 'outside'
$pivotPath = Join-Path $runnerTemp 'pivot'
$installPath = Join-Path $pivotPath 'install'
$installerPath = Join-Path $fixtureRoot 'ancestor-junction-installer.cmd'
$logPath = Join-Path $fixtureRoot 'installer-smoke.log'
$smokeScript = Join-Path $PSScriptRoot 'installer_smoketest.ps1'
$previousRunnerTemp = $env:RUNNER_TEMP

try {
    New-Item -ItemType Directory -Force -Path $runnerTemp, $outsidePath | Out-Null
    Set-Content -LiteralPath (Join-Path $outsidePath 'keep.txt') -Value 'must survive ancestor junction cleanup test' -Encoding ascii

    cmd.exe /c "mklink /J `"$pivotPath`" `"$outsidePath`"" | Out-Null
    $junctionSupported = (Test-Path -LiteralPath $pivotPath) -and
        (((Get-Item -LiteralPath $pivotPath -Force).Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0)
    if (-not $junctionSupported) {
        Write-Output 'SKIP: Junction fixtures are not supported in this environment.'
        exit 0
    }
    Remove-FixturePath $pivotPath

    @(
        '@echo off',
        'set "INSTALL=%INSTALLER_SMOKE_TEST_INSTALL%"',
        'mkdir "%INSTALL%"',
        'exit /b 1'
    ) | Set-Content -LiteralPath $installerPath -Encoding ascii

    cmd.exe /c "mklink /J `"$pivotPath`" `"$outsidePath`"" | Out-Null
    $env:RUNNER_TEMP = $runnerTemp
    $env:INSTALLER_SMOKE_TEST_INSTALL = $installPath
    $previousErrorActionPreference = $ErrorActionPreference
    try {
        $ErrorActionPreference = 'Continue'
        & powershell -NoProfile -ExecutionPolicy Bypass -File $smokeScript -Installer $installerPath -InstallDir $installPath -LogPath $logPath 2>$null
        $smokeExit = $LASTEXITCODE
    }
    finally {
        $ErrorActionPreference = $previousErrorActionPreference
    }

    $log = Get-Content -LiteralPath $logPath -Raw
    if ($smokeExit -eq 0) {
        throw 'Ancestor-junction fixture unexpectedly succeeded.'
    }
    if ($log -notmatch 'FINAL: FAIL: .*reparse') {
        throw 'Ancestor-junction cleanup rejection was not recorded as FINAL: FAIL.'
    }
    if (-not (Test-Path -LiteralPath (Join-Path $outsidePath 'keep.txt') -PathType Leaf)) {
        throw 'Ancestor-junction cleanup reached the external fixture target.'
    }
    if (-not (Test-Path -LiteralPath $pivotPath)) {
        throw 'Ancestor junction was removed instead of rejecting cleanup.'
    }

    Write-Output 'PASS: Ancestor junction was rejected before cleanup and its external target was preserved.'
}
finally {
    $env:RUNNER_TEMP = $previousRunnerTemp
    Remove-FixturePath $fixtureRoot
}
