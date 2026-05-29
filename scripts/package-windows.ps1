param(
    [string]$BuildExe = "build-qt-release-nmake\src\app\airgap_qr_transfer_app.exe",
    [string]$QtRoot = "C:\Qt\6.8.3\msvc2022_64",
    [string]$PackageName = "AirGapQRTransfer-Windows10-x64",
    [switch]$NoDownloadVcRedist
)

$ErrorActionPreference = "Stop"
$RepoRoot = Split-Path -Parent $PSScriptRoot
$DistRoot = Join-Path $RepoRoot "dist"
$PackageDir = Join-Path $DistRoot $PackageName
$ZipPath = Join-Path $DistRoot "$PackageName.zip"
$DownloadDir = Join-Path $DistRoot "downloads"

function Resolve-RepoPath {
    param([string]$Path)

    if ([System.IO.Path]::IsPathRooted($Path)) {
        return [System.IO.Path]::GetFullPath($Path)
    }

    return [System.IO.Path]::GetFullPath((Join-Path $RepoRoot $Path))
}

function Assert-UnderDirectory {
    param(
        [string]$Child,
        [string]$Parent
    )

    $resolvedChild = [System.IO.Path]::GetFullPath($Child)
    $resolvedParent = [System.IO.Path]::GetFullPath($Parent).TrimEnd('\') + '\'
    if (-not $resolvedChild.StartsWith($resolvedParent, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing to modify path outside expected directory: $resolvedChild"
    }
}

$ResolvedBuildExe = Resolve-RepoPath $BuildExe
$Windeployqt = Join-Path $QtRoot "bin\windeployqt.exe"
$QmlDir = Join-Path $RepoRoot "src\app"

if (-not (Test-Path -LiteralPath $ResolvedBuildExe -PathType Leaf)) {
    throw "Built app not found: $ResolvedBuildExe"
}
if (-not (Test-Path -LiteralPath $Windeployqt -PathType Leaf)) {
    throw "windeployqt not found: $Windeployqt"
}

New-Item -ItemType Directory -Force -Path $DistRoot | Out-Null
Assert-UnderDirectory -Child $PackageDir -Parent $DistRoot
if (Test-Path -LiteralPath $PackageDir) {
    Remove-Item -LiteralPath $PackageDir -Recurse -Force
}
if (Test-Path -LiteralPath $ZipPath) {
    Remove-Item -LiteralPath $ZipPath -Force
}
New-Item -ItemType Directory -Force -Path $PackageDir | Out-Null

$PackageExe = Join-Path $PackageDir "airgap_qr_transfer_app.exe"
Copy-Item -LiteralPath $ResolvedBuildExe -Destination $PackageExe -Force

Write-Host "== Deploy Qt runtime =="
& $Windeployqt --release --qmldir $QmlDir $PackageExe
if ($LASTEXITCODE -ne 0) {
    throw "windeployqt failed with exit code $LASTEXITCODE"
}

Write-Host "== Copy non-Qt runtime dependencies =="
$BuildDir = Split-Path -Parent $ResolvedBuildExe
$NonQtDlls = Get-ChildItem -LiteralPath $BuildDir -Filter "*.dll" |
    Where-Object { $_.Name -notlike "Qt6*.dll" }
if ($NonQtDlls.Count -eq 0) {
    throw "No non-Qt runtime DLLs were found next to the built app: $BuildDir"
}
foreach ($dll in $NonQtDlls) {
    Copy-Item -LiteralPath $dll.FullName -Destination (Join-Path $PackageDir $dll.Name) -Force
}

Write-Host "== Add VC++ runtime installer =="
$RuntimeDir = Join-Path $PackageDir "runtime"
New-Item -ItemType Directory -Force -Path $RuntimeDir | Out-Null
$VcRedist = Join-Path $RuntimeDir "vc_redist.x64.exe"
$LocalVcRedist = Join-Path $BuildDir "vc_redist.x64.exe"
if ((-not $NoDownloadVcRedist) -and (Test-Path -LiteralPath $DownloadDir -PathType Container)) {
    $cached = Join-Path $DownloadDir "vc_redist.x64.exe"
    if (Test-Path -LiteralPath $cached -PathType Leaf) {
        Copy-Item -LiteralPath $cached -Destination $VcRedist -Force
    }
}
if (-not (Test-Path -LiteralPath $VcRedist -PathType Leaf)) {
    if (-not $NoDownloadVcRedist) {
        New-Item -ItemType Directory -Force -Path $DownloadDir | Out-Null
        $downloadTarget = Join-Path $DownloadDir "vc_redist.x64.exe"
        Invoke-WebRequest -Uri "https://aka.ms/vs/17/release/vc_redist.x64.exe" -OutFile $downloadTarget
        Copy-Item -LiteralPath $downloadTarget -Destination $VcRedist -Force
    } elseif (Test-Path -LiteralPath $LocalVcRedist -PathType Leaf) {
        Copy-Item -LiteralPath $LocalVcRedist -Destination $VcRedist -Force
    } else {
        throw "VC++ runtime installer is unavailable. Re-run without -NoDownloadVcRedist or place vc_redist.x64.exe next to the built app."
    }
}

$runBat = @"
@echo off
setlocal
cd /d "%~dp0"
start "" "%~dp0airgap_qr_transfer_app.exe"
"@
Set-Content -LiteralPath (Join-Path $PackageDir "Run-AirGapQRTransfer.bat") -Value $runBat -Encoding ASCII

$installBat = @"
@echo off
setlocal
cd /d "%~dp0"
"%~dp0runtime\vc_redist.x64.exe" /install /passive /norestart
echo.
echo VC++ runtime installer finished. If this is the first install, start Run-AirGapQRTransfer.bat afterwards.
pause
"@
Set-Content -LiteralPath (Join-Path $PackageDir "Install-VCRuntime-x64.bat") -Value $installBat -Encoding ASCII

$readme = @"
AirGap QR Transfer - Windows 10 x64 deployment package

Target system:
- Windows 10 x64, version 1809 or later.
- Camera access must be allowed for desktop apps.
- Network access is not required by AirGap QR Transfer.

First-time setup on the production PC:
1. Run Install-VCRuntime-x64.bat as administrator if Microsoft Visual C++ 2015-2022 Redistributable x64 is not installed.
2. Open Windows Settings -> Privacy -> Camera.
3. Enable camera access and desktop app camera access.
4. Run Run-AirGapQRTransfer.bat.

Camera selection:
- In Receive mode, choose the camera from the Camera drop-down list.
- Different machines have different camera names and indexes. Do not assume a fixed index.

Package contents:
- airgap_qr_transfer_app.exe
- Qt runtime DLLs, QML files, and plugins deployed by windeployqt
- qrencode.dll
- runtime\vc_redist.x64.exe
"@
Set-Content -LiteralPath (Join-Path $PackageDir "README-Windows10-1809.txt") -Value $readme -Encoding UTF8

Write-Host "== Create zip =="
Compress-Archive -LiteralPath $PackageDir -DestinationPath $ZipPath -Force

Write-Host "Package created:"
Write-Host "  folder: $PackageDir"
Write-Host "  zip:    $ZipPath"
