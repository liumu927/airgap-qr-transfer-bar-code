# AirGap QR Transfer - 扫描器模式改造计划

## 1. 项目概述

### 1.1 背景

AirGap QR Transfer 当前设计基于摄像头（UVC 设备）接收动态二维码视频流。用户希望使用 **新大陆 NLS-FM430 固定式条码扫描器** 作为接收端设备，通过 **USB CDC COM Port（虚拟串口）** 实现离线环境下的大文件传输。

### 1.2 目标

在现有项目基础上，添加"扫描器模式"，实现：

- ✅ Windows 10/11 兼容
- ✅ GUI 桌面程序
- ✅ 单 EXE 发布
- ✅ 近距离扫描（半米内）
- ✅ USB CDC COM Port 通信
- ✅ NLS-FM430 连续读码模式
- ✅ 离线环境大文件传输（最高 64MB）

### 1.3 核心挑战

| 挑战 | 说明 |
|------|------|
| 输入源不同 | 摄像头输出视频帧，扫描器输出文本流 |
| 数据格式不同 | 需要处理扫描器的文本编码和前后缀 |
| 工作模式不同 | 需要逐帧手动触发或连续读码 |
| 协议适配 | 二进制帧数据需要正确传递 |

---

## 2. 架构设计

### 2.1 现有架构（摄像头模式）

```
发送端                              接收端
┌─────────┐                        ┌─────────┐
│ 文件    │                        │ 摄像头  │
└────┬────┘                        └────┬────┘
     ▼                                  ▼
┌─────────┐                        ┌─────────┐
│ 分片    │                        │ 视频帧  │
└────┬────┘                        └────┬────┘
     ▼                                  ▼
┌─────────┐                        ┌─────────┐
│ QR编码  │                        │ QR解码  │ (ZXing 软件解码)
└────┬────┘                        └────┬────┘
     ▼                                  ▼
┌─────────┐                        ┌─────────┐
│ 屏幕播放│                        │ 帧重组  │
│ (动态)  │                        └────┬────┘
└─────────┘                             ▼
                                   ┌─────────┐
                                   │ 文件    │
                                   └─────────┘
```

### 2.2 新增架构（扫描器模式）

```
发送端                              接收端
┌─────────┐                        ┌─────────┐
│ 文件    │                        │ NLS-FM  │
└────┬────┘                        │ 430     │
     ▼                             └────┬────┘
┌─────────┐                             ▼
│ 分片    │                        ┌─────────┐
└────┬────┘                        │ 串口    │ (QSerialPort)
     ▼                             │ COM Port│
┌─────────┐                        └────┬────┘
│ QR编码  │                             ▼
└────┬────┘                        ┌─────────┐
     ▼                             │ 文本解析│ (扫描器已完成硬件解码)
┌─────────┐                        └────┬────┘
│ 逐帧显示│                             ▼
│ (静态)  │                        ┌─────────┐
└─────────┘                        │ 帧重组  │
                                   └────┬────┘
                                        ▼
                                   ┌─────────┐
                                   │ 文件    │
                                   └─────────┘
```

### 2.3 双模式共存

```
┌─────────────────────────────────────────────────────────────────┐
│                    AirGap QR Transfer                            │
├─────────────────────────────────────────────────────────────────┤
│                                                                 │
│  ┌─────────────────────────────────────────────────────────┐   │
│  │                    核心协议层 (共享)                      │   │
│  │  frame.hpp/cpp                                           │   │
│  │  chunker.hpp/cpp                                         │   │
│  │  assembler.hpp/cpp                                       │   │
│  │  crc32.hpp/cpp                                           │   │
│  │  sha256.hpp/cpp                                          │   │
│  │  session.hpp/cpp                                         │   │
│  └─────────────────────────────────────────────────────────┘   │
│                              │                                  │
│              ┌───────────────┴───────────────┐                  │
│              ▼                               ▼                  │
│  ┌─────────────────────┐       ┌─────────────────────┐         │
│  │   摄像头模式 (现有)  │       │   扫描器模式 (新增)  │         │
│  ├─────────────────────┤       ├─────────────────────┤         │
│  │ - QCamera          │       │ - QSerialPort       │         │
│  │ - QVideoSink       │       │ - SerialPortReader  │         │
│  │ - ZXing 软件解码   │       │ - 文本解析          │         │
│  │ - 动态二维码播放   │       │ - 逐帧静态显示      │         │
│  └─────────────────────┘       └─────────────────────┘         │
│                                                                 │
└─────────────────────────────────────────────────────────────────┘
```

---

## 3. 详细实施计划

### 3.1 第一阶段：基础设施（第 1-2 天）

#### 3.1.1 添加 Qt SerialPort 依赖

**文件：`vcpkg.json`**

```json
{
  "name": "airgap-qr-transfer",
  "version-string": "0.1.0",
  "builtin-baseline": "d015e31e90838a4c9dfa3eed45979bc70d9357fc",
  "dependencies": [
    "libqrencode",
    "nu-book-zxing-cpp"
  ],
  "features": {
    "qt-app": {
      "description": "Qt/QML desktop application target dependencies.",
      "dependencies": [
        {
          "name": "qtbase",
          "default-features": false,
          "features": [
            "freetype",
            "gui",
            "png",
            "thread",
            "serialport"  // 新增
          ]
        },
        "qtdeclarative"
      ]
    },
    "cimbar": {
      "description": "Experimental libcimbar high-density color barcode backend.",
      "dependencies": [
        "opencv4"
      ]
    }
  }
}
```

**文件：`CMakeLists.txt`**

```cmake
# 在 Qt 依赖部分添加
find_package(Qt6 REQUIRED COMPONENTS SerialPort)
target_link_libraries(${PROJECT_NAME} PRIVATE Qt6::SerialPort)
```

#### 3.1.2 创建串口读取器

**新文件：`src/app/serial_port_reader.hpp`**

```cpp
#pragma once

#include <QObject>
#include <QSerialPort>
#include <QByteArray>
#include <QString>
#include <QStringList>

namespace aqrt::app {

struct SerialPortConfig {
    QString portName;
    int baudRate = 9600;
    QSerialPort::DataBits dataBits = QSerialPort::Data8;
    QSerialPort::Parity parity = QSerialPort::NoParity;
    QSerialPort::StopBits stopBits = QSerialPort::OneStop;
    QSerialPort::FlowControl flowControl = QSerialPort::NoFlowControl;
    
    // 扫描器配置
    QByteArray framePrefix;  // 帧前缀（可选）
    QByteArray frameSuffix;  // 帧后缀（通常为 \r\n 或 \r）
    int frameTimeoutMs = 100;  // 帧超时（毫秒）
};

class SerialPortReader : public QObject {
    Q_OBJECT
public:
    explicit SerialPortReader(QObject* parent = nullptr);
    ~SerialPortReader() override;

    // 端口管理
    bool open(const SerialPortConfig& config);
    void close();
    bool isOpen() const;
    
    // 可用端口列表
    static QStringList availablePorts();
    static QString portDescription(const QString& portName);
    
    // 配置
    void setConfig(const SerialPortConfig& config);
    SerialPortConfig config() const;

signals:
    void frameReceived(const QByteArray& frameData);
    void rawDataReceived(const QByteArray& rawData);
    void errorOccurred(const QString& errorString);
    void portOpened();
    void portClosed();

private slots:
    void onReadyRead();
    void onErrorOccurred(QSerialPort::SerialPortError error);

private:
    void processBuffer();
    void emitFrame(const QByteArray& frame);
    
    QSerialPort* port_;
    SerialPortConfig config_;
    QByteArray buffer_;
    qint64 lastReadTimeMs_ = 0;
};

} // namespace aqrt::app
```

**新文件：`src/app/serial_port_reader.cpp`**

```cpp
#include "serial_port_reader.hpp"

#include <QDateTime>
#include <QDebug>
#include <QSerialPortInfo>

namespace aqrt::app {

SerialPortReader::SerialPortReader(QObject* parent)
    : QObject(parent)
    , port_(new QSerialPort(this))
{
    connect(port_, &QSerialPort::readyRead, this, &SerialPortReader::onReadyRead);
    connect(port_, &QSerialPort::errorOccurred, this, &SerialPortReader::onErrorOccurred);
}

SerialPortReader::~SerialPortReader()
{
    close();
}

bool SerialPortReader::open(const SerialPortConfig& config)
{
    if (port_->isOpen()) {
        close();
    }
    
    config_ = config;
    port_->setPortName(config.portName);
    port_->setBaudRate(config.baudRate);
    port_->setDataBits(config.dataBits);
    port_->setParity(config.parity);
    port_->setStopBits(config.stopBits);
    port_->setFlowControl(config.flowControl);
    
    if (!port_->open(QIODevice::ReadOnly)) {
        emit errorOccurred(QString("无法打开串口 %1: %2")
            .arg(config.portName, port_->errorString()));
        return false;
    }
    
    buffer_.clear();
    lastReadTimeMs_ = 0;
    
    qInfo() << "SerialPortReader: 已打开串口" << config.portName;
    emit portOpened();
    return true;
}

void SerialPortReader::close()
{
    if (port_->isOpen()) {
        port_->close();
        qInfo() << "SerialPortReader: 已关闭串口";
        emit portClosed();
    }
    buffer_.clear();
}

bool SerialPortReader::isOpen() const
{
    return port_->isOpen();
}

QStringList SerialPortReader::availablePorts()
{
    QStringList ports;
    const auto infos = QSerialPortInfo::availablePorts();
    for (const auto& info : infos) {
        ports.append(info.portName());
    }
    return ports;
}

QString SerialPortReader::portDescription(const QString& portName)
{
    const auto infos = QSerialPortInfo::availablePorts();
    for (const auto& info : infos) {
        if (info.portName() == portName) {
            QString desc = info.description();
            if (desc.isEmpty()) {
                desc = info.manufacturer();
            }
            return desc.isEmpty() ? portName : QString("%1 (%2)").arg(portName, desc);
        }
    }
    return portName;
}

void SerialPortReader::setConfig(const SerialPortConfig& config)
{
    config_ = config;
}

SerialPortConfig SerialPortReader::config() const
{
    return config_;
}

void SerialPortReader::onReadyRead()
{
    const QByteArray newData = port_->readAll();
    buffer_.append(newData);
    lastReadTimeMs_ = QDateTime::currentMSecsSinceEpoch();
    
    emit rawDataReceived(newData);
    processBuffer();
}

void SerialPortReader::onErrorOccurred(QSerialPort::SerialPortError error)
{
    if (error == QSerialPort::NoError) {
        return;
    }
    
    const QString errorMsg = port_->errorString();
    qWarning() << "SerialPortReader: 串口错误" << error << errorMsg;
    emit errorOccurred(errorMsg);
    
    if (error != QSerialPort::TimeoutError) {
        close();
    }
}

void SerialPortReader::processBuffer()
{
    // 如果配置了帧后缀，按后缀分割
    if (!config_.frameSuffix.isEmpty()) {
        while (true) {
            int suffixPos = buffer_.indexOf(config_.frameSuffix);
            if (suffixPos < 0) {
                break;
            }
            
            QByteArray frame = buffer_.left(suffixPos);
            buffer_.remove(0, suffixPos + config_.frameSuffix.size());
            
            // 移除前缀（如果配置了）
            if (!config_.framePrefix.isEmpty() && frame.startsWith(config_.framePrefix)) {
                frame.remove(0, config_.framePrefix.size());
            }
            
            if (!frame.isEmpty()) {
                emitFrame(frame);
            }
        }
    }
    // 如果没有配置后缀，使用超时机制
    else if (config_.frameTimeoutMs > 0) {
        qint64 now = QDateTime::currentMSecsSinceEpoch();
        if (lastReadTimeMs_ > 0 && (now - lastReadTimeMs_) >= config_.frameTimeoutMs) {
            if (!buffer_.isEmpty()) {
                QByteArray frame = buffer_;
                buffer_.clear();
                
                // 移除前缀
                if (!config_.framePrefix.isEmpty() && frame.startsWith(config_.framePrefix)) {
                    frame.remove(0, config_.framePrefix.size());
                }
                
                emitFrame(frame);
            }
        }
    }
}

void SerialPortReader::emitFrame(const QByteArray& frame)
{
    if (frame.isEmpty()) {
        return;
    }
    
    qInfo() << "SerialPortReader: 收到帧，大小" << frame.size();
    emit frameReceived(frame);
}

} // namespace aqrt::app
```

---

### 3.2 第二阶段：扫描器接收控制器（第 3-5 天）

#### 3.2.1 创建扫描器接收控制器

**新文件：`src/app/scanner_receive_controller.hpp`**

```cpp
#pragma once

#include "receive_frame_collector.hpp"
#include "serial_port_reader.hpp"

#include <QObject>
#include <QString>
#include <QStringList>
#include <QUrl>

#include <atomic>
#include <cstdint>

namespace aqrt::app {

class ScannerReceiveController final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString status READ status NOTIFY stateChanged)
    Q_PROPERTY(QString fileName READ fileName NOTIFY stateChanged)
    Q_PROPERTY(int receivedChunks READ receivedChunks NOTIFY stateChanged)
    Q_PROPERTY(int totalChunks READ totalChunks NOTIFY stateChanged)
    Q_PROPERTY(double progress READ progress NOTIFY stateChanged)
    Q_PROPERTY(bool scanning READ scanning NOTIFY stateChanged)
    Q_PROPERTY(bool completed READ completed NOTIFY stateChanged)
    Q_PROPERTY(int receivedFrameCount READ receivedFrameCount NOTIFY stateChanged)
    Q_PROPERTY(int acceptedFrameCount READ acceptedFrameCount NOTIFY stateChanged)
    Q_PROPERTY(int rejectedFrameCount READ rejectedFrameCount NOTIFY stateChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY stateChanged)
    Q_PROPERTY(QString lastSavedPath READ lastSavedPath NOTIFY stateChanged)
    Q_PROPERTY(QString receivedText READ receivedText NOTIFY stateChanged)
    Q_PROPERTY(bool receivedTextAvailable READ receivedTextAvailable NOTIFY stateChanged)
    Q_PROPERTY(QStringList availablePorts READ availablePorts NOTIFY stateChanged)
    Q_PROPERTY(QString selectedPort READ selectedPort NOTIFY stateChanged)
    Q_PROPERTY(bool portOpen READ portOpen NOTIFY stateChanged)

public:
    explicit ScannerReceiveController(QObject* parent = nullptr);
    ~ScannerReceiveController() override;

    // 状态
    [[nodiscard]] QString status() const;
    [[nodiscard]] QString fileName() const;
    [[nodiscard]] int receivedChunks() const;
    [[nodiscard]] int totalChunks() const;
    [[nodiscard]] double progress() const;
    [[nodiscard]] bool scanning() const;
    [[nodiscard]] bool completed() const;
    [[nodiscard]] int receivedFrameCount() const;
    [[nodiscard]] int acceptedFrameCount() const;
    [[nodiscard]] int rejectedFrameCount() const;
    [[nodiscard]] QString lastError() const;
    [[nodiscard]] QString lastSavedPath() const;
    [[nodiscard]] QString receivedText() const;
    [[nodiscard]] bool receivedTextAvailable() const;
    [[nodiscard]] QStringList availablePorts() const;
    [[nodiscard]] QString selectedPort() const;
    [[nodiscard]] bool portOpen() const;

    // 操作
    Q_INVOKABLE void selectPort(const QString& portName);
    Q_INVOKABLE void startScanning();
    Q_INVOKABLE void stopScanning();
    Q_INVOKABLE void reset();
    Q_INVOKABLE void saveToFile(const QUrl& fileUrl);
    Q_INVOKABLE void saveToDirectory(const QUrl& directoryUrl);
    Q_INVOKABLE void copyReceivedText();
    Q_INVOKABLE void refreshPorts();

signals:
    void stateChanged();

private slots:
    void onFrameReceived(const QByteArray& frameData);
    void onSerialError(const QString& error);

private:
    void setStatus(QString status);
    void setLastError(QString error);
    void resetStats();
    [[nodiscard]] bool hasVerifiedPayload() const;
    [[nodiscard]] QString verifiedFileName() const;
    [[nodiscard]] const core::Bytes& verifiedBytes() const;
    
    // 解析扫描器输出
    QByteArray parseScannerOutput(const QByteArray& rawData);
    
    ReceiveFrameCollector collector_;
    SerialPortReader* serialReader_;
    SerialPortConfig portConfig_;
    
    QString status_;
    QString lastError_;
    QString lastSavedPath_;
    QString receivedText_;
    
    std::atomic_bool scanning_{false};
    int receivedFrameCount_ = 0;
    int acceptedFrameCount_ = 0;
    int rejectedFrameCount_ = 0;
};

} // namespace aqrt::app
```

**新文件：`src/app/scanner_receive_controller.cpp`**

```cpp
#include "scanner_receive_controller.hpp"

#include <QClipboard>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QStandardPaths>
#include <QStringDecoder>

#include <algorithm>

namespace aqrt::app {

namespace {

QString decode_utf8_text(const core::Bytes& bytes)
{
    if (bytes.empty()) {
        return {};
    }

    QByteArray raw;
    raw.append(reinterpret_cast<const char*>(bytes.data()), 
               static_cast<qsizetype>(bytes.size()));

    QStringDecoder decoder(QStringDecoder::Utf8);
    const QString text = decoder.decode(raw);
    if (decoder.hasError()) {
        return {};
    }
    return text;
}

} // namespace

ScannerReceiveController::ScannerReceiveController(QObject* parent)
    : QObject(parent)
    , serialReader_(new SerialPortReader(this))
{
    // 默认配置
    portConfig_.baudRate = 9600;
    portConfig_.dataBits = QSerialPort::Data8;
    portConfig_.parity = QSerialPort::NoParity;
    portConfig_.stopBits = QSerialPort::OneStop;
    portConfig_.flowControl = QSerialPort::NoFlowControl;
    portConfig_.frameSuffix = "\r\n";  // NLS-FM430 默认后缀
    portConfig_.frameTimeoutMs = 100;
    
    connect(serialReader_, &SerialPortReader::frameReceived,
            this, &ScannerReceiveController::onFrameReceived);
    connect(serialReader_, &SerialPortReader::errorOccurred,
            this, &ScannerReceiveController::onSerialError);
}

ScannerReceiveController::~ScannerReceiveController() = default;

QString ScannerReceiveController::status() const { return status_; }
QString ScannerReceiveController::fileName() const 
{ 
    return QString::fromStdString(collector_.file_name()); 
}
int ScannerReceiveController::receivedChunks() const 
{ 
    return static_cast<int>(collector_.received_chunks()); 
}
int ScannerReceiveController::totalChunks() const 
{ 
    return static_cast<int>(collector_.total_chunks()); 
}
double ScannerReceiveController::progress() const
{
    const int total = totalChunks();
    if (total <= 0) {
        return 0.0;
    }
    return std::clamp(static_cast<double>(receivedChunks()) / static_cast<double>(total), 0.0, 1.0);
}
bool ScannerReceiveController::scanning() const { return scanning_.load(); }
bool ScannerReceiveController::completed() const { return collector_.completed(); }
int ScannerReceiveController::receivedFrameCount() const { return receivedFrameCount_; }
int ScannerReceiveController::acceptedFrameCount() const { return acceptedFrameCount_; }
int ScannerReceiveController::rejectedFrameCount() const { return rejectedFrameCount_; }
QString ScannerReceiveController::lastError() const { return lastError_; }
QString ScannerReceiveController::lastSavedPath() const { return lastSavedPath_; }
QString ScannerReceiveController::receivedText() const { return receivedText_; }
bool ScannerReceiveController::receivedTextAvailable() const { return !receivedText_.isEmpty(); }

QStringList ScannerReceiveController::availablePorts() const
{
    return SerialPortReader::availablePorts();
}

QString ScannerReceiveController::selectedPort() const
{
    return portConfig_.portName;
}

bool ScannerReceiveController::portOpen() const
{
    return serialReader_->isOpen();
}

void ScannerReceiveController::selectPort(const QString& portName)
{
    if (portConfig_.portName == portName) {
        return;
    }
    
    if (serialReader_->isOpen()) {
        serialReader_->close();
    }
    
    portConfig_.portName = portName;
    emit stateChanged();
}

void ScannerReceiveController::startScanning()
{
    if (scanning_.load()) {
        return;
    }
    
    if (portConfig_.portName.isEmpty()) {
        setLastError("未选择串口");
        setStatus("未选择串口");
        return;
    }
    
    if (!serialReader_->open(portConfig_)) {
        setLastError("无法打开串口");
        setStatus("无法打开串口");
        return;
    }
    
    scanning_.store(true);
    setLastError({});
    setStatus("等待扫描...");
    qInfo() << "ScannerReceiveController: 开始扫描";
    emit stateChanged();
}

void ScannerReceiveController::stopScanning()
{
    if (!scanning_.load()) {
        return;
    }
    
    scanning_.store(false);
    serialReader_->close();
    setStatus("已停止");
    qInfo() << "ScannerReceiveController: 停止扫描";
    emit stateChanged();
}

void ScannerReceiveController::reset()
{
    collector_.reset();
    resetStats();
    scanning_.store(false);
    serialReader_->close();
    setStatus("就绪");
    emit stateChanged();
}

void ScannerReceiveController::saveToFile(const QUrl& fileUrl)
{
    if (!hasVerifiedPayload()) {
        setLastError("没有已验证的文件");
        setStatus("没有已验证的文件");
        return;
    }

    const QString filePath = fileUrl.toLocalFile();
    if (filePath.isEmpty()) {
        setLastError("保存路径无效");
        setStatus("保存路径无效");
        return;
    }

    QFile out(filePath);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        setLastError("无法打开保存路径");
        setStatus("无法打开保存路径");
        return;
    }

    const auto& bytes = verifiedBytes();
    const auto written = out.write(
        reinterpret_cast<const char*>(bytes.data()),
        static_cast<qint64>(bytes.size()));
    
    if (written != static_cast<qint64>(bytes.size())) {
        setLastError("无法完整写入文件");
        setStatus("无法完整写入文件");
        return;
    }

    setLastError({});
    lastSavedPath_ = filePath;
    setStatus("已保存");
    emit stateChanged();
}

void ScannerReceiveController::saveToDirectory(const QUrl& directoryUrl)
{
    if (!hasVerifiedPayload()) {
        setLastError("没有已验证的文件");
        setStatus("没有已验证的文件");
        return;
    }

    const QString dirPath = directoryUrl.toLocalFile();
    if (dirPath.isEmpty()) {
        setLastError("保存目录无效");
        setStatus("保存目录无效");
        return;
    }

    QDir dir(dirPath);
    if (!dir.mkpath(QStringLiteral("."))) {
        setLastError("无法创建保存目录");
        setStatus("无法创建保存目录");
        return;
    }

    const QString safeName = QFileInfo(verifiedFileName()).fileName();
    saveToFile(QUrl::fromLocalFile(dir.filePath(safeName)));
}

void ScannerReceiveController::copyReceivedText()
{
    if (receivedText_.isEmpty()) {
        setLastError("没有接收到的文本");
        setStatus("没有接收到的文本");
        return;
    }

    if (auto* clipboard = QGuiApplication::clipboard()) {
        clipboard->setText(receivedText_);
        setLastError({});
        setStatus("已复制文本");
        emit stateChanged();
        return;
    }

    setLastError("剪贴板不可用");
    setStatus("剪贴板不可用");
}

void ScannerReceiveController::refreshPorts()
{
    emit stateChanged();
}

void ScannerReceiveController::onFrameReceived(const QByteArray& frameData)
{
    if (!scanning_.load()) {
        return;
    }
    
    ++receivedFrameCount_;
    
    // 尝试解析为二进制帧
    QByteArray binaryFrame = parseScannerOutput(frameData);
    
    // 转换为 Bytes
    core::Bytes payload;
    payload.resize(binaryFrame.size());
    std::memcpy(payload.data(), binaryFrame.constData(), binaryFrame.size());
    
    // 尝试添加到收集器
    const auto result = collector_.add_payload(payload);
    
    if (!result.ok()) {
        ++rejectedFrameCount_;
        const QString error = QString("帧被拒绝: %1")
            .arg(QString::fromStdString(result.message));
        setLastError(error);
        setStatus(error);
        qWarning() << "ScannerReceiveController:" << error;
        emit stateChanged();
        return;
    }
    
    ++acceptedFrameCount_;
    
    if (result.completed) {
        scanning_.store(false);
        serialReader_->close();
        
        if (result.text_message) {
            receivedText_ = decode_utf8_text(result.assembled_bytes);
        }
        
        setStatus(QString("已接收 %1").arg(QString::fromStdString(result.file_name)));
    } else if (result.total_chunks > 0) {
        setStatus(QString("扫描中 %1/%2")
            .arg(result.received_chunks)
            .arg(result.total_chunks));
    } else {
        setStatus("扫描中...");
    }
    
    qInfo() << "ScannerReceiveController: 帧已接受" 
            << acceptedFrameCount_ << "/" << receivedFrameCount_;
    emit stateChanged();
}

void ScannerReceiveController::onSerialError(const QString& error)
{
    setLastError(QString("串口错误: %1").arg(error));
    setStatus(QString("串口错误: %1").arg(error));
    
    scanning_.store(false);
    emit stateChanged();
}

void ScannerReceiveController::setStatus(QString status)
{
    status_ = std::move(status);
}

void ScannerReceiveController::setLastError(QString error)
{
    lastError_ = std::move(error);
}

void ScannerReceiveController::resetStats()
{
    lastError_.clear();
    lastSavedPath_.clear();
    receivedText_.clear();
    receivedFrameCount_ = 0;
    acceptedFrameCount_ = 0;
    rejectedFrameCount_ = 0;
}

bool ScannerReceiveController::hasVerifiedPayload() const
{
    return collector_.completed();
}

QString ScannerReceiveController::verifiedFileName() const
{
    return QString::fromStdString(collector_.file_name());
}

const core::Bytes& ScannerReceiveController::verifiedBytes() const
{
    return collector_.assembled_bytes();
}

QByteArray ScannerReceiveController::parseScannerOutput(const QByteArray& rawData)
{
    // NLS-FM430 在 USB CDC COM Port 模式下输出原始数据
    // 但可能会添加前后缀，需要根据扫描器配置处理
    
    QByteArray result = rawData;
    
    // 移除常见的行尾字符
    while (result.endsWith('\r') || result.endsWith('\n')) {
        result.chop(1);
    }
    
    // 如果数据看起来是十六进制编码，尝试解码
    // （某些扫描器配置可能输出十六进制）
    if (result.startsWith("5152") || result.startsWith("AQRT")) {
        // 可能是原始数据，直接返回
        return result;
    }
    
    return result;
}

} // namespace aqrt::app
```

---

### 3.3 第三阶段：修改发送端（第 6-8 天）

#### 3.3.1 添加静态帧显示模式

**修改文件：`src/app/send_controller.hpp`**

```cpp
// 在类定义中添加
Q_PROPERTY(bool staticFrameMode READ staticFrameMode WRITE setStaticFrameMode NOTIFY stateChanged)
Q_PROPERTY(int currentStaticFrame READ currentStaticFrame NOTIFY stateChanged)
Q_PROPERTY(int totalStaticFrames READ totalStaticFrames NOTIFY stateChanged)

// 新增方法
[[nodiscard]] bool staticFrameMode() const;
void setStaticFrameMode(bool enabled);
[[nodiscard]] int currentStaticFrame() const;
[[nodiscard]] int totalStaticFrames() const;

Q_INVOKABLE void showNextStaticFrame();
Q_INVOKABLE void showPreviousStaticFrame();
Q_INVOKABLE void resetStaticFrame();

// 新增信号
void staticFrameChanged();

// 新增成员变量
bool static_frame_mode_ = false;
int current_static_frame_ = 0;
```

**修改文件：`src/app/send_controller.cpp`**

```cpp
bool SendController::staticFrameMode() const
{
    return static_frame_mode_;
}

void SendController::setStaticFrameMode(bool enabled)
{
    if (static_frame_mode_ == enabled) {
        return;
    }
    
    static_frame_mode_ = enabled;
    
    if (enabled) {
        // 停止动态播放
        stopPlayback();
        current_static_frame_ = 0;
    }
    
    emit stateChanged();
    emit staticFrameChanged();
}

int SendController::currentStaticFrame() const
{
    return current_static_frame_;
}

int SendController::totalStaticFrames() const
{
    return static_cast<int>(frame_builder_.frame_count());
}

void SendController::showNextStaticFrame()
{
    if (!static_frame_mode_) {
        return;
    }
    
    const int total = totalStaticFrames();
    if (total <= 0) {
        return;
    }
    
    current_static_frame_ = (current_static_frame_ + 1) % total;
    emit staticFrameChanged();
    emit stateChanged();
}

void SendController::showPreviousStaticFrame()
{
    if (!static_frame_mode_) {
        return;
    }
    
    const int total = totalStaticFrames();
    if (total <= 0) {
        return;
    }
    
    current_static_frame_ = (current_static_frame_ - 1 + total) % total;
    emit staticFrameChanged();
    emit stateChanged();
}

void SendController::resetStaticFrame()
{
    current_static_frame_ = 0;
    emit staticFrameChanged();
    emit stateChanged();
}
```

---

### 3.4 第四阶段：UI 适配（第 9-11 天）

#### 3.4.1 修改主界面

**修改文件：`src/app/Main.qml`**

```qml
import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

ApplicationWindow {
    id: root
    // ... 现有代码 ...
    
    // 添加模式选择
    property bool scannerMode: false
    
    // 模式切换
    RowLayout {
        Layout.fillWidth: true
        
        Label {
            text: "接收模式:"
            color: root.mutedColor
        }
        
        RadioButton {
            id: cameraModeRadio
            text: "摄像头"
            checked: !root.scannerMode
            onCheckedChanged: {
                if (checked) {
                    root.scannerMode = false
                }
            }
        }
        
        RadioButton {
            id: scannerModeRadio
            text: "扫描器"
            checked: root.scannerMode
            onCheckedChanged: {
                if (checked) {
                    root.scannerMode = true
                }
            }
        }
    }
    
    // 串口选择（扫描器模式）
    RowLayout {
        Layout.fillWidth: true
        visible: root.scannerMode && modeTabs.currentIndex === 1
        
        Label {
            text: "串口:"
            color: root.mutedColor
            Layout.preferredWidth: 48
        }
        
        ComboBox {
            id: portSelector
            Layout.fillWidth: true
            model: scannerReceiveController.availablePorts
            onActivated: function(index) {
                scannerReceiveController.selectPort(model[index])
            }
        }
        
        Button {
            text: "刷新"
            onClicked: scannerReceiveController.refreshPorts()
        }
    }
    
    // 扫描器模式状态显示
    ColumnLayout {
        visible: root.scannerMode && modeTabs.currentIndex === 1
        Layout.fillWidth: true
        spacing: 8
        
        Label {
            text: "状态: " + scannerReceiveController.status
            color: root.mutedColor
        }
        
        Label {
            text: "已接收帧: " + scannerReceiveController.acceptedFrameCount + " / " + scannerReceiveController.receivedFrameCount
            color: root.mutedColor
            visible: scannerReceiveController.receivedFrameCount > 0
        }
        
        Label {
            text: "进度: " + (scannerReceiveController.progress * 100).toFixed(1) + "%"
            color: root.mutedColor
            visible: scannerReceiveController.totalChunks > 0
        }
        
        Label {
            text: "错误: " + scannerReceiveController.lastError
            color: root.dangerColor
            visible: scannerReceiveController.lastError.length > 0
        }
    }
    
    // 扫描器模式控制按钮
    RowLayout {
        visible: root.scannerMode && modeTabs.currentIndex === 1
        Layout.fillWidth: true
        spacing: 8
        
        AppButton {
            text: scannerReceiveController.scanning ? "停止" : "开始扫描"
            primary: !scannerReceiveController.scanning
            danger: scannerReceiveController.scanning
            enabled: scannerReceiveController.selectedPort.length > 0 || scannerReceiveController.scanning
            onClicked: scannerReceiveController.scanning ? scannerReceiveController.stopScanning() : scannerReceiveController.startScanning()
        }
        
        AppButton {
            text: "重置"
            enabled: !scannerReceiveController.scanning
            onClicked: scannerReceiveController.reset()
        }
        
        AppButton {
            text: "保存文件"
            visible: scannerReceiveController.completed
            primary: true
            onClicked: fileSaveDialog.open()
        }
        
        AppButton {
            text: "复制文本"
            visible: scannerReceiveController.receivedTextAvailable
            onClicked: scannerReceiveController.copyReceivedText()
        }
    }
    
    // 静态帧模式（扫描器发送端）
    ColumnLayout {
        visible: root.scannerMode && modeTabs.currentIndex === 0
        Layout.fillWidth: true
        spacing: 8
        
        Label {
            text: "静态帧模式"
            color: root.mutedColor
            font.bold: true
        }
        
        Label {
            text: "帧 " + (sendController.currentStaticFrame + 1) + " / " + sendController.totalStaticFrames
            color: root.mutedColor
            visible: sendController.totalStaticFrames > 0
        }
        
        RowLayout {
            spacing: 8
            
            AppButton {
                text: "上一帧"
                enabled: sendController.totalStaticFrames > 0
                onClicked: sendController.showPreviousStaticFrame()
            }
            
            AppButton {
                text: "下一帧"
                enabled: sendController.totalStaticFrames > 0
                onClicked: sendController.showNextStaticFrame()
            }
            
            AppButton {
                text: "重置"
                onClicked: sendController.resetStaticFrame()
            }
        }
        
        Label {
            text: "提示：使用扫描器逐帧扫描上方二维码"
            color: root.mutedColor
            font.pixelSize: 12
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }
    }
    
    // ... 现有代码 ...
}
```

---

### 3.5 第五阶段：集成测试（第 12-14 天）

#### 3.5.1 单元测试

**新文件：`tests/app/scanner_receive_controller_test.cpp`**

```cpp
#include <QTest>
#include <QSignalSpy>

#include "scanner_receive_controller.hpp"

using namespace aqrt::app;

class ScannerReceiveControllerTest : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void testInitialState();
    void testSelectPort();
    void testStartStopScanning();
    void testFrameProcessing();
    void testReset();

private:
    ScannerReceiveController* controller_;
};

void ScannerReceiveControllerTest::initTestCase()
{
    controller_ = new ScannerReceiveController(this);
}

void ScannerReceiveControllerTest::cleanupTestCase()
{
    delete controller_;
}

void ScannerReceiveControllerTest::testInitialState()
{
    QCOMPARE(controller_->status(), "就绪");
    QCOMPARE(controller_->scanning(), false);
    QCOMPARE(controller_->completed(), false);
    QCOMPARE(controller_->receivedFrameCount(), 0);
}

void ScannerReceiveControllerTest::testSelectPort()
{
    QSignalSpy spy(controller_, &ScannerReceiveController::stateChanged);
    
    controller_->selectPort("COM3");
    QCOMPARE(controller_->selectedPort(), "COM3");
    QCOMPARE(spy.count(), 1);
}

void ScannerReceiveControllerTest::testStartStopScanning()
{
    // 注意：实际测试需要模拟串口
    // 这里只测试状态变化逻辑
    
    QSignalSpy spy(controller_, &ScannerReceiveController::stateChanged);
    
    // 测试无端口时启动失败
    controller_->startScanning();
    QCOMPARE(controller_->scanning(), false);
    QVERIFY(!controller_->lastError().isEmpty());
}

void ScannerReceiveControllerTest::testFrameProcessing()
{
    // 测试帧处理逻辑
    // 需要模拟串口输入
}

void ScannerReceiveControllerTest::testReset()
{
    controller_->reset();
    QCOMPARE(controller_->status(), "就绪");
    QCOMPARE(controller_->receivedFrameCount(), 0);
}

QTEST_MAIN(ScannerReceiveControllerTest)
#include "scanner_receive_controller_test.moc"
```

#### 3.5.2 集成测试脚本

**新文件：`scripts/scanner-smoke-test.ps1`**

```powershell
# 扫描器模式冒烟测试
param(
    [string]$Port = "COM3",
    [string]$TestFile = "test.txt"
)

Write-Host "=== AirGap QR Transfer 扫描器模式冒烟测试 ===" -ForegroundColor Cyan

# 1. 生成测试文件
Write-Host "`n[1/5] 生成测试文件..." -ForegroundColor Yellow
$testContent = "Hello 你好世界 " + ("A" * 1000)
$testContent | Out-File -FilePath $TestFile -Encoding UTF8
Write-Host "  已生成: $TestFile ($((Get-Item $TestFile).Length) bytes)"

# 2. 启动发送端（静态帧模式）
Write-Host "`n[2/5] 启动发送端..." -ForegroundColor Yellow
$sendProcess = Start-Process -FilePath ".\airgap_qr_transfer_app.exe" `
    -ArgumentList "--send-file", $TestFile, "--static-frame-mode" `
    -PassThru

# 等待用户准备
Write-Host "  发送端已启动，请将扫描器对准二维码" -ForegroundColor Green
Read-Host "  按 Enter 继续"

# 3. 启动接收端（扫描器模式）
Write-Host "`n[3/5] 启动接收端..." -ForegroundColor Yellow
$receiveProcess = Start-Process -FilePath ".\airgap_qr_transfer_app.exe" `
    -ArgumentList "--scanner-mode", "--port", $Port, "--scan" `
    -PassThru

# 4. 等待传输完成
Write-Host "`n[4/5] 等待传输完成..." -ForegroundColor Yellow
$timeout = 60
$elapsed = 0
while ($elapsed -lt $timeout) {
    Start-Sleep -Seconds 1
    $elapsed++
    Write-Host "." -NoNewline
    
    # 检查是否完成（需要实现状态文件或轮询）
    if (Test-Path "receive_status.txt") {
        $status = Get-Content "receive_status.txt"
        if ($status -match "completed") {
            break
        }
    }
}
Write-Host ""

# 5. 验证结果
Write-Host "`n[5/5] 验证结果..." -ForegroundColor Yellow
if (Test-Path "received_$TestFile") {
    $originalHash = (Get-FileHash $TestFile -Algorithm SHA256).Hash
    $receivedHash = (Get-FileHash "received_$TestFile" -Algorithm SHA256).Hash
    
    if ($originalHash -eq $receivedHash) {
        Write-Host "  ✅ 测试通过！文件哈希匹配" -ForegroundColor Green
    } else {
        Write-Host "  ❌ 测试失败！文件哈希不匹配" -ForegroundColor Red
        Write-Host "  原始: $originalHash"
        Write-Host "  接收: $receivedHash"
    }
} else {
    Write-Host "  ❌ 测试失败！未找到接收文件" -ForegroundColor Red
}

# 清理
Write-Host "`n=== 测试完成 ===" -ForegroundColor Cyan
```

---

## 4. 文件修改清单

### 4.1 新增文件

| 文件 | 说明 |
|------|------|
| `src/app/serial_port_reader.hpp` | 串口读取器头文件 |
| `src/app/serial_port_reader.cpp` | 串口读取器实现 |
| `src/app/scanner_receive_controller.hpp` | 扫描器接收控制器头文件 |
| `src/app/scanner_receive_controller.cpp` | 扫描器接收控制器实现 |
| `tests/app/scanner_receive_controller_test.cpp` | 扫描器控制器单元测试 |
| `scripts/scanner-smoke-test.ps1` | 扫描器模式冒烟测试脚本 |
| `docs/scanner-mode-plan.md` | 本文档 |

### 4.2 修改文件

| 文件 | 修改内容 |
|------|---------|
| `vcpkg.json` | 添加 `serialport` 特性 |
| `CMakeLists.txt` | 添加 Qt SerialPort 依赖 |
| `src/app/main.cpp` | 添加扫描器模式启动参数 |
| `src/app/main.cpp` | 注册 ScannerReceiveController |
| `src/app/send_controller.hpp` | 添加静态帧模式支持 |
| `src/app/send_controller.cpp` | 实现静态帧模式逻辑 |
| `src/app/Main.qml` | 添加扫描器模式 UI |
| `scripts/package-windows.ps1` | 添加串口 DLL 部署 |

---

## 5. 风险与注意事项

### 5.1 技术风险

| 风险 | 影响 | 缓解措施 |
|------|------|---------|
| 扫描器输出格式不确定 | 高 | 需要实际设备测试，调整解析逻辑 |
| 二进制数据丢失 | 高 | 考虑 Base64 编码作为备选方案 |
| 扫描器添加不可见字符 | 中 | 添加数据清洗逻辑 |
| 串口通信不稳定 | 中 | 添加重试机制和错误恢复 |
| 扫描速度限制 | 低 | 可能需要调整帧大小和数量 |

### 5.2 NLS-FM430 配置建议

使用 EasySet 工具配置扫描器：

| 配置项 | 推荐值 | 说明 |
|--------|--------|------|
| 接口模式 | USB CDC COM Port | 虚拟串口，稳定性最高 |
| 波特率 | 9600 | 默认值，可尝试提高到 115200 |
| 数据位 | 8 | 标准配置 |
| 校验位 | None | 无校验 |
| 停止位 | 1 | 标准配置 |
| 帧后缀 | CR+LF (\r\n) | 标准行尾 |
| 帧前缀 | 无 | 不添加前缀 |
| 连续读码 | 启用 | 支持自动连续扫描 |
| 解码超时 | 500ms | 根据实际情况调整 |

### 5.3 编码问题处理

扫描器可能对二进制数据进行字符编码转换，解决方案：

```cpp
// 在 ScannerReceiveController 中添加编码处理
QByteArray ScannerReceiveController::parseScannerOutput(const QByteArray& rawData)
{
    QByteArray result = rawData;
    
    // 移除行尾字符
    while (result.endsWith('\r') || result.endsWith('\n')) {
        result.chop(1);
    }
    
    // 尝试检测编码
    if (result.contains('\x00') || result.contains('\xFF')) {
        // 包含二进制数据，可能是原始数据
        return result;
    }
    
    // 尝试十六进制解码
    if (result.size() % 2 == 0) {
        bool isHex = true;
        for (char c : result) {
            if (!isxdigit(c)) {
                isHex = false;
                break;
            }
        }
        
        if (isHex) {
            QByteArray decoded;
            for (int i = 0; i < result.size(); i += 2) {
                bool ok;
                char byte = result.mid(i, 2).toInt(&ok, 16);
                if (ok) {
                    decoded.append(byte);
                }
            }
            return decoded;
        }
    }
    
    return result;
}
```

### 5.4 备选方案：Base64 编码

如果二进制数据丢失严重，可以修改发送端使用 Base64 编码：

```
原始方案：二进制帧 → QR编码 → 扫描器 → 二进制解析
备选方案：二进制帧 → Base64编码 → QR编码 → 扫描器 → Base64解码
```

**优点**：
- Base64 只包含 ASCII 字符，扫描器不会丢失数据
- 兼容性更好

**缺点**：
- 数据量增加 33%
- 需要修改发送端和接收端

---

## 6. 里程碑与验收标准

### 6.1 里程碑

| 里程碑 | 目标日期 | 验收标准 |
|--------|---------|---------|
| M1: 基础设施 | 第 2 天 | Qt SerialPort 集成成功，串口读取器可编译 |
| M2: 接收控制器 | 第 5 天 | 扫描器接收控制器可接收数据 |
| M3: 发送端改造 | 第 8 天 | 静态帧模式可正常工作 |
| M4: UI 完成 | 第 11 天 | 扫描器模式 UI 完整可用 |
| M5: 测试通过 | 第 14 天 | 小文件端到端测试通过 |

### 6.2 验收测试用例

| 测试用例 | 输入 | 预期输出 |
|---------|------|---------|
| TC1: 纯英文文本 | "Hello World" | 接收端显示 "Hello World" |
| TC2: 纯中文文本 | "你好世界" | 接收端显示 "你好世界" |
| TC3: 混合文本 | "Hello 你好 123" | 接收端显示 "Hello 你好 123" |
| TC4: 小文件 (1KB) | test-1kb.txt | 文件哈希匹配 |
| TC5: 中等文件 (100KB) | test-100kb.txt | 文件哈希匹配 |
| TC6: 大文件 (1MB) | test-1mb.txt | 文件哈希匹配 |
| TC7: 最大文件 (64MB) | test-64mb.bin | 文件哈希匹配 |

---

## 7. 后续优化

### 7.1 性能优化

- 提高串口波特率（9600 → 115200）
- 优化二维码容量（增大帧数据量）
- 实现批量帧处理

### 7.2 功能增强

- 添加传输进度条
- 支持断点续传
- 添加传输速度显示
- 支持多文件传输

### 7.3 用户体验

- 添加串口自动检测
- 添加扫描器配置向导
- 添加传输历史记录
- 添加错误诊断工具

---

## 8. 附录

### 8.1 参考文档

- [Qt SerialPort 文档](https://doc.qt.io/qt-6/qtserialport-index.html)
- [NLS-FM430 用户手册](https://www.newland.com/)
- [AirGap QR Transfer 协议规范](protocol.md)
- [AirGap QR Transfer 架构设计](architecture.md)

### 8.2 术语表

| 术语 | 说明 |
|------|------|
| UVC | USB Video Class，USB 视频设备标准 |
| HID | Human Interface Device，人机接口设备 |
| CDC | Communications Device Class，通信设备类 |
| QR Code | Quick Response Code，快速响应码 |
| CRC32 | Cyclic Redundancy Check 32-bit，32位循环冗余校验 |
| SHA-256 | Secure Hash Algorithm 256-bit，256位安全哈希算法 |

### 8.3 变更记录

| 版本 | 日期 | 作者 | 说明 |
|------|------|------|------|
| 1.0 | 2026-06-08 | - | 初始版本 |
