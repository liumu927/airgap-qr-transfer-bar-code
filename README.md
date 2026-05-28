# AirGap QR Transfer

基于动态二维码的跨平台离线文件摆渡系统。

本项目目标是在无网络、无 U 盘、无光盘等传输介质的物理隔离环境下，通过一端屏幕连续显示动态二维码、另一端摄像头连续扫描，实现 Windows 与 Android 之间的离线文件传输。

## 当前状态

当前处于 MVP 阶段 6 前置验证：Windows 桌面端已具备发送/接收 UI 与真实 QR adapter，Android arm64 Debug APK 打包链路已打通，Android 侧真实 QR 依赖也已完成构建和链接验证；Android 真机已完成安装、相机授权、接收页 Scan 和摄像头预览烟测；Windows 到 Android、Android 到 Windows 的小文件真实端到端接收、保存和外部 SHA-256 对比已通过。

已建立：

- CMake 工程骨架。
- `/src/core` 核心协议库。
- manifest、data、end 三类协议帧结构。
- 通用帧头编码/解析。
- CRC32。
- SHA-256。
- `session_id` 随机生成与 `file_id` 派生。
- 固定大小 chunk 规划与 data 帧生成。
- manifest 构建输入校验，包括空文件、文件大小上限、危险文件名和过长文件名。
- 接收端 chunk 去重、冲突检测、重组和 SHA-256 校验。
- `/src/qr` 二维码编码/解码 adapter 接口。
- `/src/app` Qt/QML 发送端应用骨架，当前包含文件选择、帧生成、二维码预览和循环播放控制。
- `/src/app` Qt/QML 接收端页面骨架，当前包含发送/接收模式切换、默认摄像头预览、扫描状态、chunk 进度、错误状态和接收诊断计数。
- 接收端支持摄像头选择：Receive 页提供摄像头下拉框，桌面端可通过 `--camera-index <index>` 指定启动时使用的摄像头，并在状态文件中记录可用摄像头列表和当前选择。
- 接收端视频帧采样管线：QML `VideoOutput` 将 `videoSink` 交给 C++，`ReceiveController` 节流读取 `QVideoFrame`、转灰度图、调用 ZXing adapter，并把识别出的 payload 交给 `ReceiveFrameCollector`。
- 接收端运行时相机权限处理：扫描前检查并请求 `QCameraPermission`，权限拒绝时给出明确状态。
- 接收端保存动作：只有 SHA-256 校验通过并完成重组后，才允许把内存中的接收结果写入用户选择的本地路径。
- Android 接收端保存动作：只有 SHA-256 校验通过并完成重组后，点击 Save 会打开 Android 系统文件保存器，由用户选择保存位置和文件名；自动化测试仍保留 app 私有目录保存入口。
- `airgap_app_core` 发送帧构建库，用于把文件字节生成 manifest/data/end payload 和二维码帧序列。
- `airgap_app_core` 接收帧收集库，用于把已解码的二维码 payload 解析为协议帧，按 manifest/data/end 喂给接收会话，并在 SHA-256 校验成功后交付文件字节。
- QR mock adapter，用于验证协议 payload 到 QR 层的往返边界。
- 基于 vcpkg `libqrencode` 的真实二维码 encoder adapter。
- 基于 vcpkg `nu-book-zxing-cpp` 的真实二维码 decoder adapter。
- QR 模块图到灰度栅格图的渲染辅助，用于连接 encoder 输出、截图和后续摄像头帧。
- 核心协议、二维码适配层、发送帧构建流程、接收帧收集流程的单元测试。
- Android arm64 Debug APK 构建链路：JDK 17、Android SDK platform 33、NDK r27c、Qt 6.5.3 Android arm64 kit，已生成 APK 并检查 manifest 权限。
- Android arm64 真实 QR 依赖链路：通过 vcpkg `arm64-android` triplet 构建 `libqrencode` 与 `nu-book-zxing-cpp`，并通过 `AIRGAP_QR_DEP_ROOT` 接入 Qt Android 构建。
- Android 自定义 manifest：当前 APK 仅声明 `android.permission.CAMERA`，不声明网络、蓝牙、录音或外部存储权限。
- Android 真机烟测：Xiaomi 25102RKBEC / Android 16 通过 USB 调试连接，QR-enabled APK 安装成功，运行时相机权限已授权，Receive 页点击 Scan 后摄像头预览可见，视频帧和解码尝试计数持续增长。
- Windows 到 Android 小文件端到端烟测：Windows 发送端全屏循环播放样本文件二维码，Android 接收端完成 `airgap-e2e-sample.txt` 接收，进度 `1 / 1`，`QR decoded: 4`，`QR accepted: 4`，`QR rejected: 0`；点击 Save 后写入 Android 应用私有目录，再通过 `adb run-as` 拉出文件，SHA-256 与 Windows 源文件一致。
- Android 到 Windows 小文件端到端烟测：Android 发送端循环播放 3 帧二维码，Windows 接收端通过本机环境中的摄像头索引选择 `1080P USB Camera`，完成 `airgap-android-send-sample.txt` 接收和自动保存，`QR decoded: 11`，`QR accepted: 9`，`QR rejected: 2`，保存文件 SHA-256 与 Android 源文件一致。

尚未实现：

- Windows 到 Windows 的真实摄像头端到端验收。

## 构建要求

- CMake 3.20 或更高版本。
- 支持 C++20 的 C++ 编译器。
- Windows 推荐使用 Visual Studio 2022 或 Qt Creator 配套的 MSVC/Clang 工具链。
- 真实 QR 依赖通过 vcpkg manifest 默认安装：`libqrencode`、`nu-book-zxing-cpp`。
- Qt 桌面应用推荐使用官方预编译 Qt 6.8.3 MSVC 2022 x64 Kit，并需要 `qtmultimedia` 模块。
- Qt 桌面应用依赖也放在 vcpkg 可选 feature `qt-app` 中，但当前机器的 Windows Device Guard 会阻止 vcpkg 现场编译 Qt 工具。
- Android Debug APK 当前使用 Qt 6.5.3 Android arm64 kit 构建。aqt 当前公开 Android 索引没有提供 Qt 6.8.3 Android kit，因此 Android 打包验证先使用 Qt 6.5.3；桌面端仍使用 Qt 6.8.3。

## Windows 构建

当前已验证的 Windows 命令需要先进入 Visual Studio 2022 Build Tools 开发者环境：

```powershell
cmd /c "call ""C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat"" -arch=x64 && cmake -S . -B build-nmake -G ""NMake Makefiles"" && cmake --build build-nmake && ctest --test-dir build-nmake --output-on-failure"
```

启用 vcpkg 依赖并构建真实 `libqrencode` / `ZXing-C++` adapter：

```powershell
cmd /c "call ""C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat"" -arch=x64 && cmake -S . -B build-vcpkg-nmake -G ""NMake Makefiles"" -DCMAKE_TOOLCHAIN_FILE=""C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\vcpkg\scripts\buildsystems\vcpkg.cmake"" -DVCPKG_TARGET_TRIPLET=x64-windows && cmake --build build-vcpkg-nmake && ctest --test-dir build-vcpkg-nmake --output-on-failure"
```

Qt 桌面应用目标只有在 CMake 找到 Qt6 时才会构建。当前机器的 vcpkg Qt 构建被 Windows Device Guard 阻止执行 `syncqt.exe`，因此默认构建会跳过 app 目标。

当前已验证的 Qt 安装方式：

```powershell
python -m pip install --user aqtinstall
python -m aqt install-qt windows desktop 6.8.3 win64_msvc2022_64 --outputdir C:\Qt
python -m aqt install-qt windows desktop 6.8.3 win64_msvc2022_64 --outputdir C:\Qt --modules qtmultimedia
```

构建并测试 Release 桌面应用：

```powershell
cmd /c "call ""C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat"" -arch=x64 && cmake -S . -B build-qt-release-nmake -G ""NMake Makefiles"" -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE=""C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\vcpkg\scripts\buildsystems\vcpkg.cmake"" -DVCPKG_TARGET_TRIPLET=x64-windows -DCMAKE_PREFIX_PATH=""C:\Qt\6.8.3\msvc2022_64"" && cmake --build build-qt-release-nmake && ctest --test-dir build-qt-release-nmake --output-on-failure"
```

当前测试套件包含：

- `airgap_app_tests`：发送帧序列、非法文件名、空文件、QR 容量不足、接收端乱序重组、重复帧去重、坏 payload、跨 session 拒绝、end 早于 manifest 拒绝、真实 QR 编码/解码 loopback 重组。
- `airgap_core_tests`：协议编解码、分片、重组、校验。
- `airgap_qr_tests`：mock QR、真实 QR encoder/decoder 条件测试。

部署 Qt 运行时：

```powershell
cmd /c "call ""C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat"" -arch=x64 && ""C:\Qt\6.8.3\msvc2022_64\bin\windeployqt.exe"" --release --qmldir ""C:\Users\weien\Documents\qr-transfer\src\app"" ""C:\Users\weien\Documents\qr-transfer\build-qt-release-nmake\src\app\airgap_qr_transfer_app.exe"""
```

生成 Windows 10 x64 生产部署包：

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\package-windows.ps1
```

生成结果位于 `dist\AirGapQRTransfer-Windows10-x64.zip`，包内包含应用、Qt 运行库、`qrencode.dll`、启动脚本和 VC++ 2015-2022 x64 运行库安装程序。

部署后的发送端可执行文件位于：

```text
C:\Users\weien\Documents\qr-transfer\build-qt-release-nmake\src\app\airgap_qr_transfer_app.exe
```

如果使用 Visual Studio IDE 或 Qt Creator，也可以选择已安装的 MSVC 19.44 x64 工具链。

如果后续安装 Ninja，也可以使用：

```powershell
cmake -S . -B build-ninja -G Ninja
cmake --build build-ninja
ctest --test-dir build-ninja --output-on-failure
```

## Android 构建

Android 应用层已使用 Qt/QML 共用，接收端扫描前会通过 `QCameraPermission` 处理运行时相机权限。

Qt 6.5.3 Android 的 Camera2 后端在 Android 16 / MIUI 设备上可能触发 `ImageReader` 的 `maxImages has already been acquired` 崩溃。当前开发环境已对 `C:\Qt\6.5.3\android_arm64_v8a\jar\Qt6AndroidMultimedia.jar` 应用本项目脚本补丁，并在同目录保留 `.airgap-backup` 原始备份。新环境或恢复 Qt jar 后，请先运行：

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\patch-qt-android-camera2.ps1
```

当前已验证的 Android 工具链：

- JDK：`C:\Program Files\Eclipse Adoptium\jdk-17.0.19.10-hotspot`
- Android SDK：`C:\Android\Sdk`
- Android platform：`android-33`
- Android build-tools：`33.0.2`
- Android NDK：`27.2.12479018`
- Qt Android kit：`C:\Qt\6.5.3\android_arm64_v8a`
- Qt host tools：`C:\Qt\6.5.3\msvc2019_64`

安装命令记录：

```powershell
winget install --id EclipseAdoptium.Temurin.17.JDK --exact --accept-package-agreements --accept-source-agreements --silent
winget install --id Ninja-build.Ninja --exact --accept-package-agreements --accept-source-agreements --silent
python -m aqt install-qt windows android 6.5.3 android_arm64_v8a --outputdir C:\Qt --modules qtmultimedia
python -m aqt install-qt windows desktop 6.5.3 win64_msvc2019_64 --outputdir C:\Qt --modules qtmultimedia
```

Android SDK command-line tools 安装在 `C:\Android\Sdk\cmdline-tools\latest` 后，安装 SDK 包：

```powershell
$env:JAVA_HOME = "C:\Program Files\Eclipse Adoptium\jdk-17.0.19.10-hotspot"
$env:ANDROID_SDK_ROOT = "C:\Android\Sdk"
$env:ANDROID_HOME = "C:\Android\Sdk"
$env:Path = "$env:JAVA_HOME\bin;$env:ANDROID_SDK_ROOT\cmdline-tools\latest\bin;$env:ANDROID_SDK_ROOT\platform-tools;$env:Path"
sdkmanager.bat --install "platform-tools" "platforms;android-33" "build-tools;33.0.2" "ndk;27.2.12479018"
```

构建 Debug APK：

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

上面的命令会验证基础 Android 打包链路。如果需要启用 Android 侧真实 `libqrencode` / ZXing adapter，先构建 Android 版 QR 依赖：

```powershell
$env:ANDROID_NDK_HOME = "C:\Android\Sdk\ndk\27.2.12479018"
$env:ANDROID_NDK_ROOT = "C:\Android\Sdk\ndk\27.2.12479018"
$env:ANDROID_SDK_ROOT = "C:\Android\Sdk"
& "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\vcpkg\vcpkg.exe" install --triplet arm64-android --x-install-root=build-vcpkg-android\vcpkg_installed
```

然后配置带真实 QR adapter 的 Android APK：

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
cmake --build build-android-qr-arm64
```

生成的基础 Debug APK：

```text
C:\Users\weien\Documents\qr-transfer\build-android-arm64\src\app\android-build\build\outputs\apk\debug\android-build-debug.apk
```

生成的带真实 QR adapter 的 Debug APK：

```text
C:\Users\weien\Documents\qr-transfer\build-android-qr-arm64\src\app\android-build\build\outputs\apk\debug\android-build-debug.apk
```

检查 APK 权限：

```powershell
& "C:\Android\Sdk\build-tools\33.0.2\aapt.exe" dump permissions "C:\Users\weien\Documents\qr-transfer\build-android-arm64\src\app\android-build\build\outputs\apk\debug\android-build-debug.apk"
```

期望输出只包含：

```text
uses-permission: name='android.permission.CAMERA'
```

连接 Android 真机并启用 USB 调试后，可以运行烟测脚本安装并启动 QR-enabled APK：

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\android-smoke.ps1
```

如果只想验证启动后的摄像头画面和扫码计数，不需要观察系统权限弹窗，可以预授权相机：

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\android-smoke.ps1 -GrantCamera
```

脚本会检查 APK 权限、安装 APK、启动 `org.airgapqr.transfer`、确认进程存活、输出相机权限状态和近期 app 日志。真实接收能力仍需要在设备上进入 `Receive`、点击 `Scan`，并观察视频帧计数、解码尝试计数和 QR 识别计数。

## 启动参数

桌面端支持用于手工验收的启动参数：

```text
--send-file <path>  启动后直接准备发送指定本地文件
--play              与 --send-file 配合，准备完成后自动循环播放
--fullscreen        启动后全屏显示
--receive           启动后进入 Receive 页
--scan              与 --receive 配合，Receive 视图就绪后自动开始扫描
--camera-index <n>  启动接收端时选择第 n 个摄像头，索引从 0 开始
--save-dir <path>   接收完成并通过校验后自动保存到指定目录
--status-file <p>   接收状态变化时写出诊断状态文件
```

Windows 发送端烟测脚本会生成一个小样本文件，并以全屏自动播放方式启动发送端：

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\windows-sender-smoke.ps1
```

Android 接收端完成并点击 Save 后，可以用脚本从应用私有目录拉出文件并和 Windows 源文件做 SHA-256 对比：

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\android-pull-received.ps1 -SourcePath .\build\manual-e2e\airgap-e2e-sample.txt
```

Windows 接收端烟测脚本支持多摄像头选择。状态文件会列出 `availableCameraNames`、`selectedCameraIndex` 和 `selectedCameraName`，应按当前机器实际列表选择索引：

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\windows-receiver-smoke.ps1 -CameraIndex <index>
```

后续阶段仍需补充和验证：

- Android 真机安装和相机权限弹窗。
- Android 与 Windows 的真实端到端传输。

## 文档

- [总体架构](docs/architecture.md)
- [动态二维码文件传输协议](docs/protocol.md)
- [MVP 开发计划](docs/mvp-plan.md)
- [测试说明](docs/testing.md)
