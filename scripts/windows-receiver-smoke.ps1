param(
    [string]$AppPath = "",
    [string]$SaveDir = "",
    [string]$StatusFile = "",
    [int]$CameraIndex = -1,
    [ValidateSet("safe", "balanced", "fast", "cimbar")]
    [string]$SpeedMode = "safe",
    [switch]$Windowed,
    [switch]$NoScan
)

$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
if ([string]::IsNullOrWhiteSpace($AppPath)) {
    $AppPath = Join-Path $repoRoot "build-qt-release-nmake\src\app\airgap_qr_transfer_app.exe"
}
if (-not (Test-Path -LiteralPath $AppPath -PathType Leaf)) {
    throw "App executable not found: $AppPath"
}

function Quote-WindowsArgument {
    param([string]$Value)

    if ($Value.Length -eq 0) {
        return '""'
    }
    if ($Value -notmatch '[\s"]') {
        return $Value
    }
    return '"' + ($Value -replace '"', '\"') + '"'
}

if ([string]::IsNullOrWhiteSpace($SaveDir)) {
    $SaveDir = Join-Path $repoRoot "build\manual-e2e\windows-received"
}
New-Item -ItemType Directory -Force -Path $SaveDir | Out-Null

$resolvedSaveDir = (Resolve-Path -LiteralPath $SaveDir).Path
if ([string]::IsNullOrWhiteSpace($StatusFile)) {
    $StatusFile = Join-Path $repoRoot "build\manual-e2e\windows-receiver-status.txt"
}
$statusDir = Split-Path -Parent $StatusFile
if ($statusDir) {
    New-Item -ItemType Directory -Force -Path $statusDir | Out-Null
}
$resolvedStatusFile = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($StatusFile)

$arguments = @("--receive", "--save-dir", $resolvedSaveDir, "--status-file", $resolvedStatusFile, "--speed-mode", $SpeedMode)
if ($CameraIndex -ge 0) {
    $arguments += @("--camera-index", $CameraIndex.ToString())
}
if (-not $NoScan) {
    $arguments += "--scan"
}
if (-not $Windowed) {
    $arguments += "--fullscreen"
}

Write-Host "Launching receiver:"
Write-Host "  app:      $AppPath"
Write-Host "  save dir: $resolvedSaveDir"
Write-Host "  status:   $resolvedStatusFile"
Write-Host "  speed:    $SpeedMode"
if ($CameraIndex -ge 0) {
    Write-Host "  camera:   $CameraIndex"
}
$argumentLine = ($arguments | ForEach-Object { Quote-WindowsArgument $_ }) -join " "
Write-Host "  args:     $argumentLine"
Start-Process -FilePath $AppPath -ArgumentList $argumentLine -WorkingDirectory (Split-Path -Parent $AppPath)
