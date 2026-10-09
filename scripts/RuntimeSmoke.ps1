function Assert-ForestRuntimeComplete([string]$Executable) {
    $directory = Split-Path -Parent ([IO.Path]::GetFullPath($Executable))
    $required = @('Qt6Core.dll','Qt6Gui.dll','Qt6Widgets.dll','Qt6Sql.dll',
        'libgcc_s_seh-1.dll','libstdc++-6.dll','libwinpthread-1.dll',
        'platforms/qwindows.dll','sqldrivers/qsqlite.dll')
    $missing = @($required | Where-Object { -not (Test-Path -LiteralPath (Join-Path $directory $_) -PathType Leaf) })
    if (-not (Test-Path -LiteralPath $Executable -PathType Leaf)) { throw 'Runtime executable is missing.' }
    if ($missing.Count) { throw ('Missing adjacent runtime files: ' + ($missing -join ', ')) }
}

function Invoke-ForestRuntimeSmoke([string]$Executable) {
    Assert-ForestRuntimeComplete $Executable
    $Executable = [IO.Path]::GetFullPath($Executable)
    $directory = Split-Path -Parent $Executable
    if (-not (Test-Path -LiteralPath (Join-Path $directory 'forest.portable') -PathType Leaf)) {
        throw 'Smoke startup requires an isolated portable data directory.'
    }
    $start = [Diagnostics.ProcessStartInfo]::new()
    $start.FileName = $Executable
    $start.WorkingDirectory = $directory
    $start.UseShellExecute = $false
    $start.ErrorDialog = $false
    $start.WindowStyle = [Diagnostics.ProcessWindowStyle]::Hidden
    $start.EnvironmentVariables['PATH'] = "$env:SystemRoot\System32;$env:SystemRoot"
    foreach ($key in @($start.EnvironmentVariables.Keys)) {
        if ($key -match '^(QT_|QML|QTDIR$)') { $start.EnvironmentVariables.Remove($key) }
    }
    if (-not ('ForestRuntimeSmokeNative' -as [type])) {
        Add-Type -TypeDefinition @'
using System.Runtime.InteropServices;
public static class ForestRuntimeSmokeNative {
    [DllImport("kernel32.dll")] public static extern uint GetErrorMode();
    [DllImport("kernel32.dll")] public static extern uint SetErrorMode(uint mode);
}
'@
    }
    $process = [Diagnostics.Process]::new()
    $process.StartInfo = $start
    $oldMode = [ForestRuntimeSmokeNative]::GetErrorMode()
    try {
        # A child inherits this mode; loader failures must report an exit code, not block on a system dialog.
        $null = [ForestRuntimeSmokeNative]::SetErrorMode($oldMode -bor 0x8003)
        if (-not $process.Start()) { throw 'Runtime process did not start.' }
    } finally {
        $null = [ForestRuntimeSmokeNative]::SetErrorMode($oldMode)
    }
    try {
        Start-Sleep -Seconds 3
        if ($process.HasExited) { throw "Runtime exited before showing its window: $($process.ExitCode)." }
        if (-not $process.CloseMainWindow() -or -not $process.WaitForExit(10000) -or $process.ExitCode -ne 0) {
            throw 'Runtime did not close normally; startup verification failed.'
        }
        [pscustomobject]@{ Executable = $Executable; ExitCode = $process.ExitCode; DeveloperPathExcluded = $true }
    } finally {
        if (-not $process.HasExited) {
            # Only this failed test child is stopped; forced termination never counts as a successful smoke test.
            $process.Kill()
            $null = $process.WaitForExit(5000)
        }
        $process.Dispose()
    }
}
