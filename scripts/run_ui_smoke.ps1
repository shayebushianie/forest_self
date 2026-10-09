param(
    [Parameter(Mandatory = $true)]
    [string]$Executable,
    [int]$ScreenWidth = 0,
    [int]$ScreenHeight = 0,
    [string]$QtScaleFactor,
    [Parameter(ValueFromRemainingArguments = $true)]
    [string[]]$TestArguments
)

$ErrorActionPreference = "Stop"

if (-not (Test-Path -LiteralPath $Executable)) {
    throw "UI smoke executable not found: $Executable"
}
if (-not $TestArguments) {
    $resultPath = Join-Path ([IO.Path]::GetDirectoryName($Executable)) "ui-smoke-results.txt"
    $TestArguments = @("-o", ($resultPath + ",txt"))
}

Add-Type -AssemblyName System.Windows.Forms
if ($env:GITHUB_ACTIONS -eq 'true' -and $ScreenWidth -eq 0 -and $ScreenHeight -eq 0) {
    $ScreenWidth = 1920
    $ScreenHeight = 1080
}
if (($ScreenWidth -gt 0) -ne ($ScreenHeight -gt 0)) {
    throw "Provide both screen dimensions or neither."
}
if ($QtScaleFactor) {
    $scale = [double]::Parse($QtScaleFactor, [Globalization.CultureInfo]::InvariantCulture)
    if ($scale -lt 0.5 -or $scale -gt 3) { throw "Qt scale factor must be between 0.5 and 3." }
}

$displayMode = $null
if ($ScreenWidth -gt 0) {
    # Apply a supported temporary display mode, never write the desktop registry.
    Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public sealed class ForestSmokeDisplayMode : IDisposable {
    [DllImport("user32.dll", CharSet=CharSet.Unicode)]
    static extern bool EnumDisplaySettings(string device, int mode, IntPtr data);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)]
    static extern int ChangeDisplaySettings(IntPtr data, int flags);
    IntPtr original;
    public ForestSmokeDisplayMode(int width, int height) {
        original = Marshal.AllocHGlobal(220);
        Marshal.WriteInt16(original, 68, 220);
        if (!EnumDisplaySettings(null, -1, original)) {
            Marshal.FreeHGlobal(original); original = IntPtr.Zero;
            throw new Exception("Cannot read display mode.");
        }
        var candidate = Marshal.AllocHGlobal(220);
        try {
            bool found = false;
            for (int i = 0; i < 1000; ++i) {
                Marshal.WriteInt16(candidate, 68, 220);
                if (!EnumDisplaySettings(null, i, candidate)) break;
                if (Marshal.ReadInt32(candidate, 172) == width && Marshal.ReadInt32(candidate, 176) == height) {
                    found = true; break;
                }
            }
            if (!found) throw new Exception("Requested display mode is unsupported.");
            int result = ChangeDisplaySettings(candidate, 2);
            if (result != 0) throw new Exception("Display mode test failed: " + result);
            result = ChangeDisplaySettings(candidate, 4);
            if (result != 0) throw new Exception("Display mode switch failed: " + result);
        } catch { Dispose(); throw; }
        finally { Marshal.FreeHGlobal(candidate); }
    }
    public void Dispose() {
        if (original == IntPtr.Zero) return;
        int result = ChangeDisplaySettings(original, 0);
        Marshal.FreeHGlobal(original); original = IntPtr.Zero;
        if (result != 0) throw new Exception("Original display mode restore failed: " + result);
    }
}
'@
    $displayMode = [ForestSmokeDisplayMode]::new($ScreenWidth, $ScreenHeight)
    Start-Sleep -Milliseconds 800
}
$oldScaleFactor = $env:QT_SCALE_FACTOR
try {
$logicalWidth = [System.Windows.Forms.Screen]::PrimaryScreen.Bounds.Width
$display = Get-CimInstance Win32_VideoController |
    Where-Object { $_.CurrentHorizontalResolution -gt 0 } |
    Select-Object -First 1
$physicalWidth = if ($display) { [int]$display.CurrentHorizontalResolution } else { 0 }
Write-Host "UI smoke desktop: logical width=$logicalWidth; physical width=$physicalWidth; requested=${ScreenWidth}x${ScreenHeight}"

if ($QtScaleFactor) {
    $env:QT_SCALE_FACTOR = $QtScaleFactor
} elseif ($logicalWidth -gt 0 -and $physicalWidth -gt 0) {
    $env:QT_SCALE_FACTOR = ($logicalWidth / $physicalWidth).ToString(
        "0.######", [Globalization.CultureInfo]::InvariantCulture)
} else {
    $env:QT_SCALE_FACTOR = "1"
}

& $Executable @TestArguments
$result = $LASTEXITCODE
if ($resultPath -and (Test-Path -LiteralPath $resultPath)) {
    Get-Content -LiteralPath $resultPath
}
} finally {
    $env:QT_SCALE_FACTOR = $oldScaleFactor
    if ($displayMode) { $displayMode.Dispose() }
}
exit $result
