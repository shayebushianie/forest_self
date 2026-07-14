param(
    [Parameter(Mandatory = $true)]
    [string]$Executable,
    [Parameter(ValueFromRemainingArguments = $true)]
    [string[]]$TestArguments
)

$ErrorActionPreference = "Stop"

if (-not (Test-Path -LiteralPath $Executable)) {
    throw "UI smoke executable not found: $Executable"
}

Add-Type -AssemblyName System.Windows.Forms
$logicalWidth = [System.Windows.Forms.Screen]::PrimaryScreen.Bounds.Width
$display = Get-CimInstance Win32_VideoController |
    Where-Object { $_.CurrentHorizontalResolution -gt 0 } |
    Select-Object -First 1
$physicalWidth = if ($display) { [int]$display.CurrentHorizontalResolution } else { 0 }

if ($logicalWidth -gt 0 -and $physicalWidth -gt 0) {
    $env:QT_SCALE_FACTOR = ($logicalWidth / $physicalWidth).ToString(
        "0.######", [Globalization.CultureInfo]::InvariantCulture)
} else {
    $env:QT_SCALE_FACTOR = "1"
}

$logPath = Join-Path (Split-Path -Parent $Executable) "ui-smoketest.log"
$output = & $Executable @TestArguments 2>&1
$exitCode = $LASTEXITCODE
$output | Out-File -LiteralPath $logPath -Encoding utf8
$output | ForEach-Object { Write-Output $_ }
exit $exitCode
