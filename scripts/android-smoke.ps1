param(
    [string]$ApkPath = "build-android-qr-arm64\src\app\android-build\build\outputs\apk\debug\android-build-debug.apk",
    [string]$PackageName = "org.airgapqr.transfer",
    [string]$ActivityName = "org.qtproject.qt.android.bindings.QtActivity",
    [switch]$GrantCamera,
    [int]$LogSeconds = 8
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
    param(
        [string[]]$Arguments,
        [string]$Serial = ""
    )

    $adbArgs = @()
    if ($Serial.Length -gt 0) {
        $adbArgs += @("-s", $Serial)
    }
    $adbArgs += $Arguments

    & adb @adbArgs
    if ($LASTEXITCODE -ne 0) {
        throw "adb $($adbArgs -join ' ') failed with exit code $LASTEXITCODE"
    }
}

$resolvedApk = Resolve-RepoPath $ApkPath
if (-not (Test-Path -LiteralPath $resolvedApk -PathType Leaf)) {
    throw "APK not found: $resolvedApk"
}

$androidSdkRoot = $env:ANDROID_SDK_ROOT
if (-not $androidSdkRoot) {
    $androidSdkRoot = "C:\Android\Sdk"
}
$platformTools = Join-Path $androidSdkRoot "platform-tools"
if (Test-Path -LiteralPath $platformTools) {
    $env:Path = "$platformTools;$env:Path"
}

$adbCommand = Get-Command adb -ErrorAction SilentlyContinue
if (-not $adbCommand) {
    throw "adb was not found. Install Android SDK platform-tools or set ANDROID_SDK_ROOT."
}

$aapt = Join-Path $androidSdkRoot "build-tools\33.0.2\aapt.exe"
if (Test-Path -LiteralPath $aapt) {
    Write-Host "== APK permissions =="
    & $aapt dump permissions $resolvedApk
    if ($LASTEXITCODE -ne 0) {
        throw "aapt permission check failed with exit code $LASTEXITCODE"
    }
}

$devicesOutput = & adb devices -l
if ($LASTEXITCODE -ne 0) {
    throw "adb devices failed with exit code $LASTEXITCODE"
}

$devices = @()
foreach ($line in $devicesOutput) {
    if ($line -match "^(\S+)\s+device\s") {
        $devices += $Matches[1]
    }
}

if ($devices.Count -eq 0) {
    Write-Host "No Android device is connected. Enable USB debugging and run this script again."
    exit 2
}
if ($devices.Count -gt 1) {
    Write-Host "Multiple Android devices are connected. Keep exactly one device attached for this smoke check."
    $devices | ForEach-Object { Write-Host " - $_" }
    exit 3
}

$serial = $devices[0]
Write-Host "== Device =="
Invoke-Adb -Serial $serial -Arguments @("shell", "getprop", "ro.product.manufacturer")
Invoke-Adb -Serial $serial -Arguments @("shell", "getprop", "ro.product.model")
Invoke-Adb -Serial $serial -Arguments @("shell", "getprop", "ro.build.version.release")

Write-Host "== Install APK =="
Invoke-Adb -Serial $serial -Arguments @("install", "-r", "-d", $resolvedApk)

if ($GrantCamera) {
    Write-Host "== Grant camera permission =="
    Invoke-Adb -Serial $serial -Arguments @("shell", "pm", "grant", $PackageName, "android.permission.CAMERA")
} else {
    Write-Host "Camera permission was not pre-granted. Use the app UI permission prompt during manual receive checks."
}

Write-Host "== Launch app =="
Invoke-Adb -Serial $serial -Arguments @("shell", "am", "force-stop", $PackageName)
Invoke-Adb -Serial $serial -Arguments @("shell", "am", "start", "-n", "$PackageName/$ActivityName")
Start-Sleep -Seconds 3

Write-Host "== Process =="
$pidOutput = & adb -s $serial shell pidof $PackageName
if ($LASTEXITCODE -ne 0 -or -not $pidOutput) {
    throw "App process is not running after launch."
}
$appPid = ($pidOutput | Select-Object -First 1).Trim()
Write-Host "pid=$appPid"

Write-Host "== Runtime permission state =="
Invoke-Adb -Serial $serial -Arguments @("shell", "dumpsys", "package", $PackageName) |
    Select-String -Pattern "android.permission.CAMERA|granted="

Write-Host "== Recent app logs =="
Start-Sleep -Seconds $LogSeconds
& adb -s $serial logcat -d --pid=$appPid -t 200
if ($LASTEXITCODE -ne 0) {
    Write-Host "pid-scoped logcat failed; falling back to recent global logcat."
    & adb -s $serial logcat -d -t 200
}

Write-Host "Android smoke check finished. Open the app on the device and verify camera preview, Scan, and receive diagnostics manually."
