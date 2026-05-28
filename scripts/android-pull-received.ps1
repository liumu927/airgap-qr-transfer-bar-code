param(
    [string]$PackageName = "org.airgapqr.transfer",
    [string]$RemotePath = "files/received/airgap-e2e-sample.txt",
    [string]$OutputPath = "android-received-airgap-e2e-sample.txt",
    [string]$SourcePath = "",
    [string]$AdbPath = "C:\Android\Sdk\platform-tools\adb.exe"
)

$ErrorActionPreference = "Stop"

if (-not (Test-Path -LiteralPath $AdbPath)) {
    $adbCommand = Get-Command adb -ErrorAction Stop
    $AdbPath = $adbCommand.Source
}

$outputFullPath = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($OutputPath)
$outputDir = Split-Path -Parent $outputFullPath
if ($outputDir -and -not (Test-Path -LiteralPath $outputDir)) {
    New-Item -ItemType Directory -Force -Path $outputDir | Out-Null
}

$processInfo = [System.Diagnostics.ProcessStartInfo]::new()
$processInfo.FileName = $AdbPath
$processInfo.Arguments = "exec-out run-as $PackageName cat $RemotePath"
$processInfo.RedirectStandardOutput = $true
$processInfo.RedirectStandardError = $true
$processInfo.UseShellExecute = $false

$process = [System.Diagnostics.Process]::Start($processInfo)
try {
    $fileStream = [System.IO.File]::Open($outputFullPath, [System.IO.FileMode]::Create, [System.IO.FileAccess]::Write)
    try {
        $process.StandardOutput.BaseStream.CopyTo($fileStream)
    } finally {
        $fileStream.Dispose()
    }

    $stderr = $process.StandardError.ReadToEnd()
    $process.WaitForExit()
    if ($process.ExitCode -ne 0) {
        throw "adb exec-out failed with exit code $($process.ExitCode): $stderr"
    }
} finally {
    $process.Dispose()
}

$receivedHash = Get-FileHash -LiteralPath $outputFullPath -Algorithm SHA256
Write-Host "Pulled Android file:"
Write-Host "  package: $PackageName"
Write-Host "  remote:  $RemotePath"
Write-Host "  output:  $outputFullPath"
Write-Host "  sha256:  $($receivedHash.Hash)"

if ($SourcePath.Length -gt 0) {
    $sourceFullPath = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($SourcePath)
    $sourceHash = Get-FileHash -LiteralPath $sourceFullPath -Algorithm SHA256
    Write-Host "Source file:"
    Write-Host "  path:    $sourceFullPath"
    Write-Host "  sha256:  $($sourceHash.Hash)"

    if ($sourceHash.Hash -ne $receivedHash.Hash) {
        throw "SHA-256 mismatch between source and pulled Android file."
    }

    Write-Host "SHA-256 match: yes"
}
