param(
    [string]$FilePath = "",
    [string]$Text = "",
    [string]$PackageName = "org.airgapqr.transfer",
    [string]$ActivityName = "org.qtproject.qt.android.bindings.QtActivity",
    [string]$RemoteFileName = "airgap-android-send-sample.txt",
    [string]$AdbPath = "C:\Android\Sdk\platform-tools\adb.exe",
    [ValidateSet("safe", "balanced", "fast")]
    [string]$SpeedMode = "safe",
    [switch]$Windowed,
    [switch]$NoPlay
)

$ErrorActionPreference = "Stop"
$RepoRoot = Split-Path -Parent $PSScriptRoot

function Resolve-RepoPath {
    param([string]$Path)

    if ([System.IO.Path]::IsPathRooted($Path)) {
        return [System.IO.Path]::GetFullPath($Path)
    }

    return [System.IO.Path]::GetFullPath((Join-Path $RepoRoot $Path))
}

function Invoke-Adb {
    param([string[]]$Arguments)

    & $AdbPath @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "adb $($Arguments -join ' ') failed with exit code $LASTEXITCODE"
    }
}

function Quote-AndroidShell {
    param([string]$Value)

    return "'" + ($Value -replace "'", "'\\''") + "'"
}

function Quote-AppArgument {
    param([string]$Value)

    return '"' + (($Value -replace '\\', '\\') -replace '"', '\"') + '"'
}

if (-not (Test-Path -LiteralPath $AdbPath)) {
    $adbCommand = Get-Command adb -ErrorAction Stop
    $AdbPath = $adbCommand.Source
}

if (-not [string]::IsNullOrWhiteSpace($Text) -and -not [string]::IsNullOrWhiteSpace($FilePath)) {
    throw "Use either -Text or -FilePath, not both."
}

if ([string]::IsNullOrWhiteSpace($Text) -and [string]::IsNullOrWhiteSpace($FilePath)) {
    $sampleDir = Join-Path $RepoRoot "build\manual-e2e"
    New-Item -ItemType Directory -Force -Path $sampleDir | Out-Null
    $FilePath = Join-Path $sampleDir "android-to-windows-sample.txt"
    $content = @(
        "AirGap QR Transfer Android sender sample"
        "Generated for Android to Windows smoke testing."
        "Timestamp: $(Get-Date -Format o)"
    )
    Set-Content -LiteralPath $FilePath -Value $content -Encoding UTF8
}

if ([string]::IsNullOrWhiteSpace($Text)) {
    $resolvedFile = Resolve-RepoPath $FilePath
    if (-not (Test-Path -LiteralPath $resolvedFile -PathType Leaf)) {
        throw "Source file not found: $resolvedFile"
    }

    $tmpRemote = "/data/local/tmp/$RemoteFileName"
    $appRelativePath = "files/send/$RemoteFileName"
    $appAbsolutePath = "/data/user/0/$PackageName/$appRelativePath"

    Write-Host "== Prepare Android sender file =="
    Write-Host "  source: $resolvedFile"
    Write-Host "  temp:   $tmpRemote"
    Write-Host "  app:    $appAbsolutePath"
    Invoke-Adb -Arguments @("push", $resolvedFile, $tmpRemote)
    Invoke-Adb -Arguments @("shell", "run-as", $PackageName, "mkdir", "-p", "files/send")
    Invoke-Adb -Arguments @("shell", "run-as", $PackageName, "cp", $tmpRemote, $appRelativePath)
    Invoke-Adb -Arguments @("shell", "run-as", $PackageName, "ls", "-l", $appRelativePath)

    $appArgs = "--send-file $appAbsolutePath --speed-mode $SpeedMode"
} else {
    $resolvedFile = ""
    $appArgs = "--send-text $(Quote-AppArgument $Text) --speed-mode $SpeedMode"
}
if (-not $NoPlay) {
    $appArgs += " --play"
}
if (-not $Windowed) {
    $appArgs += " --fullscreen"
}

Write-Host "== Launch Android sender =="
Write-Host "  speed: $SpeedMode"
if ($resolvedFile) {
    Write-Host "  source: $resolvedFile"
} else {
    Write-Host "  text: $Text"
}
Write-Host "  args: $appArgs"
Invoke-Adb -Arguments @("shell", "am", "force-stop", $PackageName)
$component = "$PackageName/$ActivityName"
$launchCommand = "am start -n $(Quote-AndroidShell $component) --es applicationArguments $(Quote-AndroidShell $appArgs)"
Invoke-Adb -Arguments @("shell", $launchCommand)
Start-Sleep -Seconds 3

$pidOutput = & $AdbPath shell pidof $PackageName
if ($LASTEXITCODE -ne 0 -or -not $pidOutput) {
    throw "App process is not running after Android sender launch."
}

Write-Host "Android sender launched."
Write-Host "  pid:    $($pidOutput.Trim())"
if ($resolvedFile) {
    $sourceHash = Get-FileHash -LiteralPath $resolvedFile -Algorithm SHA256
    Write-Host "  sha256: $($sourceHash.Hash)"
} else {
    $textBytes = [System.Text.Encoding]::UTF8.GetBytes($Text)
    $sha256 = [System.Security.Cryptography.SHA256]::Create()
    try {
        $hash = [BitConverter]::ToString($sha256.ComputeHash($textBytes)).Replace("-", "")
        Write-Host "  sha256: $hash"
    } finally {
        $sha256.Dispose()
    }
}
