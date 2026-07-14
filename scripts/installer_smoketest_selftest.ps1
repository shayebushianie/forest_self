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

$fixtureRoot = Join-Path ([IO.Path]::GetTempPath()) ("forest-installer-smoke-selftest-{0}" -f [guid]::NewGuid())
$installPath = Join-Path $fixtureRoot 'install'
$outsidePath = Join-Path $fixtureRoot 'outside'
$junctionPath = Join-Path $installPath 'escape'
$installerPath = Join-Path $fixtureRoot 'junction-installer.cmd'
$logPath = Join-Path $fixtureRoot 'installer-smoke.log'
$smokeScript = Join-Path $PSScriptRoot 'installer_smoketest.ps1'

function Test-MissingLeafInsideTemporaryRoot {
    $installPath = Join-Path ([IO.Path]::GetTempPath()) ("forest-installer-smoke-missing-leaf-{0}" -f [guid]::NewGuid())
    $installerPath = Join-Path $fixtureRoot 'missing-leaf-installer.cmd'
    $logPath = Join-Path $fixtureRoot 'missing-leaf-smoke.log'

    try {
        New-Item -ItemType Directory -Force -Path $fixtureRoot | Out-Null
        @('@echo off', 'exit /b 1') | Set-Content -LiteralPath $installerPath -Encoding ascii
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
            throw 'Missing-leaf temporary install fixture unexpectedly succeeded.'
        }
        if ($log -notmatch 'RUN: Starting installer') {
            throw 'Missing-leaf temporary install path was rejected before installer startup.'
        }
        if ($log -match 'resolves outside the temporary root') {
            throw 'Missing-leaf temporary install path was incorrectly treated as outside the temporary root.'
        }

        Write-Output 'PASS: Missing-leaf install path below the temporary root reaches installer startup.'
    }
    finally {
        Remove-FixturePath $installPath
        Remove-Item -LiteralPath $installerPath,$logPath -Force -ErrorAction SilentlyContinue
    }
}

try {
    Test-MissingLeafInsideTemporaryRoot
    New-Item -ItemType Directory -Force -Path $outsidePath | Out-Null
    New-Item -ItemType Directory -Force -Path $installPath | Out-Null
    Set-Content -LiteralPath (Join-Path $outsidePath 'keep.txt') -Value 'must survive junction cleanup test' -Encoding ascii

    cmd.exe /c "mklink /J `"$junctionPath`" `"$outsidePath`"" | Out-Null
    $junctionSupported = (Test-Path -LiteralPath $junctionPath) -and
        (((Get-Item -LiteralPath $junctionPath -Force).Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0)
    if (-not $junctionSupported) {
        Write-Output 'SKIP: Junction fixtures are not supported in this environment.'
        exit 0
    }
    Remove-FixturePath $junctionPath
    Remove-FixturePath $installPath

    @(
        '@echo off',
        'set "INSTALL=%INSTALLER_SMOKE_TEST_INSTALL%"',
        'mkdir "%INSTALL%"',
        "mklink /J `"%INSTALL%\\escape`" `"$outsidePath`"",
        'exit /b 1'
    ) | Set-Content -LiteralPath $installerPath -Encoding ascii

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
        throw 'Junction fixture unexpectedly succeeded.'
    }
    if ($log -notmatch 'FINAL: FAIL: .*reparse') {
        throw 'Junction cleanup rejection was not recorded as FINAL: FAIL.'
    }
    if (-not (Test-Path -LiteralPath (Join-Path $outsidePath 'keep.txt') -PathType Leaf)) {
        throw 'Junction cleanup reached the external fixture target.'
    }
    if (-not (Test-Path -LiteralPath $installPath)) {
        throw 'Junction-bearing install directory was removed instead of rejected.'
    }

    Write-Output 'PASS: Junction fixture was rejected before cleanup and its external target was preserved.'
}
finally {
    Remove-FixturePath $fixtureRoot
}
