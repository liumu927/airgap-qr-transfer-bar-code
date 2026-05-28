param(
    [string]$QtAndroidRoot = "C:\Qt\6.5.3\android_arm64_v8a",
    [string]$AndroidSdkRoot = "C:\Android\Sdk",
    [string]$JdkRoot = "C:\Program Files\Eclipse Adoptium\jdk-17.0.19.10-hotspot",
    [string]$QtVersion = "v6.5.3"
)

$ErrorActionPreference = "Stop"

$jarPath = Join-Path $QtAndroidRoot "jar\Qt6AndroidMultimedia.jar"
$backupPath = "$jarPath.airgap-backup"
$androidJar = Join-Path $AndroidSdkRoot "platforms\android-33\android.jar"
$javac = Join-Path $JdkRoot "bin\javac.exe"
$jarTool = Join-Path $JdkRoot "bin\jar.exe"

foreach ($path in @($jarPath, $androidJar, $javac, $jarTool)) {
    if (-not (Test-Path $path)) {
        throw "Required file not found: $path"
    }
}

$workRoot = Join-Path $env:TEMP "airgap-qt-camera2-patch"
$sourceDir = Join-Path $workRoot "src"
$classesDir = Join-Path $workRoot "classes"
$sourcePath = Join-Path $sourceDir "QtCamera2.java"

Remove-Item -Recurse -Force $workRoot -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force $sourceDir, $classesDir | Out-Null

$sourceUrl = "https://code.qt.io/cgit/qt/qtmultimedia.git/plain/src/android/jar/src/org/qtproject/qt/android/multimedia/QtCamera2.java?h=$QtVersion"
Write-Host "Downloading QtCamera2.java from $sourceUrl"
$source = (Invoke-WebRequest -UseBasicParsing $sourceUrl).Content

$source = $source.Replace(
'            QtCamera2.this.onPhotoAvailable(mCameraId, reader.acquireLatestImage());',
'            Image image = null;
            try {
                image = reader.acquireLatestImage();
                if (image != null)
                    QtCamera2.this.onPhotoAvailable(mCameraId, image);
            } catch (IllegalStateException e) {
                Log.w("QtCamera2", "Dropping photo because ImageReader buffers are exhausted", e);
            } finally {
                if (image != null)
                    image.close();
            }')

$source = $source.Replace(
'            QtCamera2.this.onFrameAvailable(mCameraId, reader.acquireLatestImage());',
'            Image image = null;
            try {
                image = reader.acquireLatestImage();
                if (image != null)
                    QtCamera2.this.onFrameAvailable(mCameraId, image);
            } catch (IllegalStateException e) {
                Log.w("QtCamera2", "Dropping frame because ImageReader buffers are exhausted", e);
            } finally {
                if (image != null)
                    image.close();
            }')

$utf8NoBom = New-Object System.Text.UTF8Encoding($false)
[System.IO.File]::WriteAllText($sourcePath, $source.TrimStart([char]0xFEFF), $utf8NoBom)

Write-Host "Compiling patched QtCamera2.java"
& $javac -encoding UTF-8 -source 8 -target 8 -classpath "$androidJar;$jarPath" -d $classesDir $sourcePath
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}

if (-not (Test-Path $backupPath)) {
    Copy-Item $jarPath $backupPath
    Write-Host "Backed up original jar to $backupPath"
} else {
    Write-Host "Backup already exists at $backupPath"
}

Push-Location $classesDir
try {
    & $jarTool uf $jarPath `
        "org\qtproject\qt\android\multimedia\QtCamera2.class" `
        "org\qtproject\qt\android\multimedia\QtCamera2`$1.class" `
        "org\qtproject\qt\android\multimedia\QtCamera2`$2.class" `
        "org\qtproject\qt\android\multimedia\QtCamera2`$3.class" `
        "org\qtproject\qt\android\multimedia\QtCamera2`$4.class" `
        "org\qtproject\qt\android\multimedia\QtCamera2`$5.class" `
        "org\qtproject\qt\android\multimedia\QtCamera2`$6.class"
    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }
} finally {
    Pop-Location
}

Write-Host "Patched $jarPath"
Write-Host "Rebuild the Android APK after this script so androiddeployqt packages the patched jar."
