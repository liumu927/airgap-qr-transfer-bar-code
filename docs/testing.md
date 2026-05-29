# AirGap QR Transfer Testing

This document records repeatable checks for the MVP. The app must remain offline during all tests: do not enable network transfer, telemetry, cloud sync, or remote logging.

## Unit And Build Checks

Run the core, QR, and app-core tests:

```powershell
cmd /c "call ""C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat"" -arch=x64 && cmake -S . -B build-vcpkg-nmake -G ""NMake Makefiles"" -DCMAKE_TOOLCHAIN_FILE=""C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\vcpkg\scripts\buildsystems\vcpkg.cmake"" -DVCPKG_TARGET_TRIPLET=x64-windows && cmake --build build-vcpkg-nmake && ctest --test-dir build-vcpkg-nmake --output-on-failure"
```

The `airgap_app_tests` suite includes a real QR loopback check when `libqrencode` and ZXing are available: sender frames are encoded into QR images, rendered into larger camera-like grayscale frames, decoded with ZXing, and then reassembled by the receive collector.

Run the Windows Qt app build:

```powershell
cmd /c "call ""C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat"" -arch=x64 && cmake -S . -B build-qt-release-nmake -G ""NMake Makefiles"" -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE=""C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\vcpkg\scripts\buildsystems\vcpkg.cmake"" -DVCPKG_TARGET_TRIPLET=x64-windows -DCMAKE_PREFIX_PATH=""C:\Qt\6.8.3\msvc2022_64"" && cmake --build build-qt-release-nmake && ctest --test-dir build-qt-release-nmake --output-on-failure"
```

Deploy the Qt runtime before manual app checks:

```powershell
cmd /c "call ""C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat"" -arch=x64 && ""C:\Qt\6.8.3\msvc2022_64\bin\windeployqt.exe"" --release --qmldir ""C:\Users\weien\Documents\qr-transfer\src\app"" ""C:\Users\weien\Documents\qr-transfer\build-qt-release-nmake\src\app\airgap_qr_transfer_app.exe"""
```

Run the Android arm64 Debug APK build with the currently verified Qt Android kit.

Before building with Qt 6.5.3 Android on Android 16 / MIUI test devices, patch the Qt Camera2 Java backend used by `androiddeployqt`. The script backs up `Qt6AndroidMultimedia.jar` to `.airgap-backup`, adds a guard around exhausted `ImageReader` buffers, and prevents the camera background thread from killing the app:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\patch-qt-android-camera2.ps1
```

```powershell
$ninja = Get-ChildItem $env:LOCALAPPDATA\Microsoft\WinGet\Packages -Recurse -Filter ninja.exe | Select-Object -First 1 -ExpandProperty FullName
$env:JAVA_HOME = "C:\Program Files\Eclipse Adoptium\jdk-17.0.19.10-hotspot"
$env:ANDROID_SDK_ROOT = "C:\Android\Sdk"
$env:ANDROID_HOME = "C:\Android\Sdk"
$env:ANDROID_NDK_ROOT = "C:\Android\Sdk\ndk\27.2.12479018"
$env:Path = "$env:JAVA_HOME\bin;$env:ANDROID_SDK_ROOT\cmdline-tools\latest\bin;$env:ANDROID_SDK_ROOT\platform-tools;$(Split-Path $ninja);$env:Path"
cmake -S . -B build-android-arm64 -G Ninja `
  -DCMAKE_MAKE_PROGRAM="$ninja" `
  -DCMAKE_BUILD_TYPE=Debug `
  -DCMAKE_TOOLCHAIN_FILE="C:\Qt\6.5.3\android_arm64_v8a\lib\cmake\Qt6\qt.toolchain.cmake" `
  -DQT_HOST_PATH="C:\Qt\6.5.3\msvc2019_64" `
  -DANDROID_SDK_ROOT="C:\Android\Sdk" `
  -DANDROID_NDK_ROOT="C:\Android\Sdk\ndk\27.2.12479018" `
  -DANDROID_ABI=arm64-v8a `
  -DANDROID_PLATFORM=android-33 `
  -DBUILD_TESTING=OFF
cmake --build build-android-arm64
```

To validate Android with the real QR encoder and decoder adapters, first build the Android QR dependencies with vcpkg:

```powershell
$env:ANDROID_NDK_HOME = "C:\Android\Sdk\ndk\27.2.12479018"
$env:ANDROID_NDK_ROOT = "C:\Android\Sdk\ndk\27.2.12479018"
$env:ANDROID_SDK_ROOT = "C:\Android\Sdk"
& "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\vcpkg\vcpkg.exe" install --triplet arm64-android --x-install-root=build-vcpkg-android\vcpkg_installed
```

Then configure and build the QR-enabled Android APK:

```powershell
$ninja = Get-ChildItem $env:LOCALAPPDATA\Microsoft\WinGet\Packages -Recurse -Filter ninja.exe | Select-Object -First 1 -ExpandProperty FullName
$env:JAVA_HOME = "C:\Program Files\Eclipse Adoptium\jdk-17.0.19.10-hotspot"
$env:ANDROID_SDK_ROOT = "C:\Android\Sdk"
$env:ANDROID_HOME = "C:\Android\Sdk"
$env:ANDROID_NDK_ROOT = "C:\Android\Sdk\ndk\27.2.12479018"
$env:ANDROID_NDK_HOME = "C:\Android\Sdk\ndk\27.2.12479018"
$env:Path = "$env:JAVA_HOME\bin;$env:ANDROID_SDK_ROOT\cmdline-tools\latest\bin;$env:ANDROID_SDK_ROOT\platform-tools;$(Split-Path $ninja);$env:Path"
cmake -S . -B build-android-qr-arm64 -G Ninja `
  -DCMAKE_MAKE_PROGRAM="$ninja" `
  -DCMAKE_BUILD_TYPE=Debug `
  -DCMAKE_TOOLCHAIN_FILE="C:\Qt\6.5.3\android_arm64_v8a\lib\cmake\Qt6\qt.toolchain.cmake" `
  -DCMAKE_PREFIX_PATH="C:\Qt\6.5.3\android_arm64_v8a" `
  -DQT_HOST_PATH="C:\Qt\6.5.3\msvc2019_64" `
  -DANDROID_SDK_ROOT="C:\Android\Sdk" `
  -DANDROID_NDK_ROOT="C:\Android\Sdk\ndk\27.2.12479018" `
  -DANDROID_ABI=arm64-v8a `
  -DANDROID_PLATFORM=android-33 `
  -DAIRGAP_QR_DEP_ROOT="C:\Users\weien\Documents\qr-transfer\build-vcpkg-android\vcpkg_installed\arm64-android" `
  -DBUILD_TESTING=OFF
cmake --build build-android-qr-arm64 --verbose
```

During a successful verbose build, confirm the compile and link lines include:

```text
AIRGAP_HAS_LIBQRENCODE=1
AIRGAP_HAS_ZXING=1
libqrencode.a
libZXing.a
```

The generated APK should be:

```text
C:\Users\weien\Documents\qr-transfer\build-android-arm64\src\app\android-build\build\outputs\apk\debug\android-build-debug.apk
```

The QR-enabled APK should be:

```text
C:\Users\weien\Documents\qr-transfer\build-android-qr-arm64\src\app\android-build\build\outputs\apk\debug\android-build-debug.apk
```

Check that the Android manifest remains offline-first and requests only the camera permission:

```powershell
& "C:\Android\Sdk\build-tools\33.0.2\aapt.exe" dump permissions "C:\Users\weien\Documents\qr-transfer\build-android-arm64\src\app\android-build\build\outputs\apk\debug\android-build-debug.apk"
```

Expected permission output:

```text
uses-permission: name='android.permission.CAMERA'
```

The base Android APK validates Qt packaging and manifest permissions. The QR-enabled Android APK validates that Android-native `libqrencode` and ZXing are compiled, linked, and packaged into the app binary. Real camera behavior still requires a physical Android device check.

## Android Device Smoke Check

Connect exactly one Android device with USB debugging enabled, then run:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\android-smoke.ps1
```

The script installs and launches the QR-enabled APK, checks that the app process remains alive, prints the runtime camera permission state, and captures recent app logs. To skip the system permission prompt and focus on camera preview and scan diagnostics, run:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\android-smoke.ps1 -GrantCamera
```

For a Windows-to-Android manual transfer smoke test, start the Windows sender with a generated sample file, automatic playback, and full-screen QR display:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\windows-sender-smoke.ps1
```

The app also accepts these direct startup arguments:

```text
--send-file <path>
--send-text <text>
--play
--fullscreen
--receive
--scan
--save-dir <path>
--status-file <path>
--camera-index <index>
--speed-mode <safe|balanced|fast|cimbar>
```

The speed modes are intended for controlled throughput testing:

```text
safe:     256 byte chunks, 300 ms playback interval, 180 ms decode interval
balanced: 384 byte chunks, 240 ms playback interval, 150 ms decode interval
fast:     512 byte chunks, 180 ms playback interval, 120 ms decode interval
cimbar:   experimental high-density color barcode mode, available only in AIRGAP_ENABLE_CIMBAR builds
```

Use `safe` as the compatibility baseline, especially for Android-to-Windows transfers and long UTF-8 text. Then compare `balanced` and `fast` on the same sender, receiver, camera, brightness, and file size.

Record the result:

```text
date:
device:
android version:
apk:
camera permission prompt shown:
camera permission granted:
app launched:
camera preview visible:
Scan button starts scanning:
video frame count increases:
decode attempt count increases:
QR decoded count increases when pointed at sender:
notes:
```

Latest recorded result:

```text
date: 2026-05-26
device: Xiaomi 25102RKBEC
android version: 16
apk: build-android-qr-arm64\src\app\android-build\build\outputs\apk\debug\android-build-debug.apk
camera permission prompt shown: not observed by script
camera permission granted: yes
app launched: yes
camera preview visible: yes, Receive -> Scan shows live camera preview
Scan button starts scanning: yes
video frame count increases: yes, observed 1 through 270 in logcat
decode attempt count increases: yes, observed 23 in the UI screenshot
QR decoded count increases when pointed at sender: not checked; no sender QR was in camera view during smoke test
notes: adb install and activity launch succeeded; process stayed alive; APK manifest still declares only android.permission.CAMERA; after applying scripts\patch-qt-android-camera2.ps1 there was no AndroidRuntime/maxImages fatal exception during an 8 second scan.
```

## Windows To Android Manual Loop

Use the helper script to start the Windows sender:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\windows-sender-smoke.ps1
```

On the Android device, open `Receive`, tap `Scan`, and point the camera at the Windows sender QR code. After completion, the receiver should show the file name, full chunk progress, a completed progress bar, and a nonzero `QR accepted` count with `QR rejected: 0`.

After tapping `Save` on Android, the app opens the Android system document creator so the user can choose the destination and file name. For automated checks that need app-private output, `saveToAppData()` is still kept as a debug/test entry point and the pulled file can be compared with the Windows source:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\android-pull-received.ps1 -SourcePath .\build\manual-e2e\airgap-e2e-sample.txt
```

Latest recorded result:

```text
date: 2026-05-27
sender: Windows desktop app, build-qt-release-nmake\src\app\airgap_qr_transfer_app.exe
receiver: Xiaomi 25102RKBEC, Android 16
apk: build-android-qr-arm64\src\app\android-build\build\outputs\apk\debug\android-build-debug.apk
source file: build\manual-e2e\airgap-e2e-sample.txt
source size: 130 bytes
source sha256: 45B7E1F76520F257B1F1A363A7E261D5D6DA9BDF0807E8E41BDDB427CDDB0099
chunk size: 256
receiver progress: 1 / 1
video frame count: 638
decode attempt count: 60
QR decoded count: 4
QR accepted count: 4
QR rejected count: 0
android saved path: /data/user/0/org.airgapqr.transfer/files/received/airgap-e2e-sample.txt
pulled file: android-received-airgap-e2e-sample.txt
pulled size: 130 bytes
pulled sha256: 45B7E1F76520F257B1F1A363A7E261D5D6DA9BDF0807E8E41BDDB427CDDB0099
result: pass; Android receiver reported "Received airgap-e2e-sample.txt", Save wrote the app-private output file, and the pulled file hash matched the Windows source
```

## Windows To Windows Manual Loop

## Text Transfer Manual Check

Text transfer reuses the normal manifest/data/end QR flow with a virtual `message.txt` payload and manifest flag `0x00000001`. The receiver only exposes the copy panel after the assembled bytes pass SHA-256 verification, the text flag is present, and the bytes decode as valid UTF-8.

Manual UI check:

1. On the sender, enter text in the `Text` box on the Send page.
2. Click `Prepare Text`, then click `Play`.
3. On the receiver, click `Scan` and point the camera at the sender QR.
4. After completion, verify the received text panel appears.
5. Click `Copy`, then paste into a local editor and compare with the source text.

Command-line sender check:

```powershell
.\build-qt-release-nmake\src\app\airgap_qr_transfer_app.exe --send-text "hello from AirGap QR Transfer" --play --speed-mode safe
```

Windows sender smoke script:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\windows-sender-smoke.ps1 -Text "hello from Windows"
```

Android sender smoke script:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\android-sender-smoke.ps1 -Text "hello from Android"
```

When a receiver status file is enabled with `--status-file`, text sessions also report:

```text
receivedTextAvailable=true
receivedTextBase64=<UTF-8 text encoded as Base64>
```

Latest Android to Windows text loop result:

```text
date: 2026-05-27
sender: Xiaomi 25102RKBEC, Android 16
receiver: Windows desktop app, build-qt-release-nmake\src\app\airgap_qr_transfer_app.exe
text: AQRT_LOOP_20260527_143424
text sha256: 7895A79695E8D714CABE13FB486842BD9A9433DAC4DD5691636B96151A67AEA6
android sender: launched by scripts\android-sender-smoke.ps1 -Text AQRT_LOOP_20260527_143424
windows receiver: launched by scripts\windows-receiver-smoke.ps1 -CameraIndex 1 -SpeedMode safe
windows selected camera: 1080P USB Camera
windows video frame count: 305
windows decode attempt count: 62
windows QR decoded count: 6
windows QR accepted count: 4
windows QR rejected count: 2
receiver progress: 1 / 1
receivedTextAvailable: true
receivedTextBase64: QVFSVF9MT09QXzIwMjYwNTI3XzE0MzQyNA==
saved file: build\manual-e2e\windows-received-text\message.txt
saved sha256: 7895A79695E8D714CABE13FB486842BD9A9433DAC4DD5691636B96151A67AEA6
result: pass; Android displayed and looped the text QR frames, Windows received with camera index 1, the decoded text matched the source text, and the saved message.txt hash matched.
```

## Android To Windows Manual Loop

Android `Send -> Open` uses the Android system document picker (`ACTION_OPEN_DOCUMENT`) and reads the returned SAF `content://` URI through `ContentResolver`, so no broad external storage permission is required. For a manual sender check, tap `Open`, choose a local document from the picker, confirm that a nonzero frame count is shown, then tap `Play`.

Start the Android sender with a generated sample file, automatic playback, and full-screen QR display:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\android-sender-smoke.ps1
```

To test a faster sender profile:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\android-sender-smoke.ps1 -SpeedMode balanced
```

Start the Windows receiver with automatic scanning, a receive output directory, and a diagnostics status file:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\windows-receiver-smoke.ps1
```

When comparing speed modes, start the receiver with the same mode as the sender:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\windows-receiver-smoke.ps1 -SpeedMode balanced
```

On Windows machines with more than one camera, pass the zero-based camera index explicitly after checking the local device list. The status file prints `availableCameraNames`, `selectedCameraIndex`, and `selectedCameraName` so the correct index can be selected for the current environment:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\windows-receiver-smoke.ps1 -CameraIndex <index>
```

Place the Android screen in the Windows camera view. The QR code should be centered, large enough to occupy a meaningful part of the preview, and not overexposed. The receiver writes live diagnostics to:

```text
build\manual-e2e\windows-receiver-status.txt
```

After completion, compare the Android sender source file with the auto-saved Windows output:

```powershell
Get-FileHash .\build\manual-e2e\android-to-windows-sample.txt -Algorithm SHA256
Get-FileHash .\build\manual-e2e\windows-received\airgap-android-send-sample.txt -Algorithm SHA256
```

Latest recorded result:

```text
date: 2026-05-27
sender: Xiaomi 25102RKBEC, Android 16
receiver: Windows desktop app, build-qt-release-nmake\src\app\airgap_qr_transfer_app.exe
apk: build-android-qr-arm64\src\app\android-build\build\outputs\apk\debug\android-build-debug.apk
source file: build\manual-e2e\android-to-windows-sample.txt
source size: 140 bytes
source sha256: 7654260A686C80B74CB39D8EB0F861DD2D88CB6845CF3A3E7546A54AECB9CD5E
android sender: launched by scripts\android-sender-smoke.ps1; prepared 3 QR frames and started playback
windows receiver: launched by scripts\windows-receiver-smoke.ps1 with --receive --scan --save-dir and --status-file
windows available cameras: Integrated Camera|1080P USB Camera|Integrated IR Camera
camera index used in this run: 1
windows selected camera: 1080P USB Camera
windows video frame count: 220
windows decode attempt count: 45
windows QR decoded count: 11
windows QR accepted count: 9
windows QR rejected count: 2
receiver progress: 1 / 1
saved file: build\manual-e2e\windows-received\airgap-android-send-sample.txt
saved size: 140 bytes
saved sha256: 7654260A686C80B74CB39D8EB0F861DD2D88CB6845CF3A3E7546A54AECB9CD5E
result: pass; Android sender looped 3 QR frames, Windows receiver used camera index 1, reassembled the file, auto-saved it, and the saved SHA-256 matched the Android source
```

Android send-side open picker regression check:

```text
date: 2026-05-27
device: Xiaomi 25102RKBEC, Android 16
apk: build-android-qr-arm64\src\app\android-build\build\outputs\apk\debug\android-build-debug.apk
action: Send -> Open -> Android system document picker -> select airgap-after-fix-open.png
result: pass; the sender read the SAF URI, displayed file name airgap-after-fix-open.png, and prepared 161 QR frame(s)
```

Use the deployed app:

```text
C:\Users\weien\Documents\qr-transfer\build-qt-release-nmake\src\app\airgap_qr_transfer_app.exe
```

1. Create a small source file, such as a 1 KB text file and a 4 KB binary file.
2. Start one app instance as sender.
3. In `Send`, choose the source file, verify frame count is nonzero, and click `Play`.
4. Start a second app instance as receiver.
5. In `Receive`, click `Scan`, point the camera at the sender QR code, and wait for progress to reach all chunks.
6. After completion, click `Save` and choose a local output path.
7. Compare SHA-256 of source and saved files with:

```powershell
Get-FileHash .\source.bin -Algorithm SHA256
Get-FileHash .\received.bin -Algorithm SHA256
```

Record the result:

```text
date:
platform:
camera:
file name:
file size:
chunk size:
frame count:
playback interval:
decode interval:
video frame count:
decode attempt count:
decode failure count:
QR decoded count:
QR accepted count:
QR rejected count:
elapsed time:
result:
notes:
```

During receiver checks, use the diagnostics below the progress bar to separate camera, QR, and protocol issues. If video frames stay at zero, the camera preview is not feeding frames into C++. If decode attempts increase but QR decoded stays at zero, focus on screen brightness, QR size, camera focus, and ZXing parameters. If QR decoded increases but QR accepted does not, inspect the protocol status and last error.

## Failure Cases

Verify the receiver keeps scanning and does not crash when:

- Scanning starts after the sender is already mid-loop.
- The QR code is temporarily blocked.
- Sender playback is paused and resumed.
- The receiver sees a QR code from a different transfer session.
- Save is attempted before completion.

For each failure, record whether the UI status is clear and whether recovery is possible without restarting the app.
