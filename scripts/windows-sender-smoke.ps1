param(
    [string]$FilePath = "",
    [string]$Text = "",
    [string]$AppPath = "",
    [ValidateSet("safe", "balanced", "fast")]
    [string]$SpeedMode = "safe",
    [switch]$Windowed,
    [switch]$NoPlay
)

$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
if ([string]::IsNullOrWhiteSpace($AppPath)) {
    $AppPath = Join-Path $repoRoot "build-qt-release-nmake\src\app\airgap_qr_transfer_app.exe"
}
if (-not (Test-Path $AppPath)) {
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

if (-not [string]::IsNullOrWhiteSpace($Text) -and -not [string]::IsNullOrWhiteSpace($FilePath)) {
    throw "Use either -Text or -FilePath, not both."
}

if ([string]::IsNullOrWhiteSpace($Text) -and [string]::IsNullOrWhiteSpace($FilePath)) {
    $sampleDir = Join-Path $repoRoot "build\manual-e2e"
    New-Item -ItemType Directory -Force $sampleDir | Out-Null
    $FilePath = Join-Path $sampleDir "airgap-e2e-sample.txt"
    $content = @(
        "AirGap QR Transfer E2E sample"
        "Generated for dynamic QR transfer smoke testing."
        "Timestamp: $(Get-Date -Format o)"
    )
    Set-Content -LiteralPath $FilePath -Value $content -Encoding UTF8
}

if ([string]::IsNullOrWhiteSpace($Text)) {
    $resolvedFile = (Resolve-Path -LiteralPath $FilePath).Path
    $arguments = @("--send-file", $resolvedFile, "--speed-mode", $SpeedMode)
} else {
    $resolvedFile = ""
    $arguments = @("--send-text", $Text, "--speed-mode", $SpeedMode)
}
if (-not $NoPlay) {
    $arguments += "--play"
}
if (-not $Windowed) {
    $arguments += "--fullscreen"
}

Write-Host "Launching sender:"
Write-Host "  app:  $AppPath"
if ($resolvedFile) {
    Write-Host "  file: $resolvedFile"
} else {
    Write-Host "  text: $Text"
}
Write-Host "  speed: $SpeedMode"
$argumentLine = ($arguments | ForEach-Object { Quote-WindowsArgument $_ }) -join " "
Write-Host "  args: $argumentLine"
Start-Process -FilePath $AppPath -ArgumentList $argumentLine -WorkingDirectory (Split-Path -Parent $AppPath)
