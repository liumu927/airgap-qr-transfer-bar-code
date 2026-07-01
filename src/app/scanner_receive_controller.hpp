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
    Q_PROPERTY(int missingChunkCount READ missingChunkCount NOTIFY stateChanged)
    Q_PROPERTY(QString missingFramesText READ missingFramesText NOTIFY stateChanged)
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
    [[nodiscard]] int missingChunkCount() const;
    [[nodiscard]] QString missingFramesText() const;
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

    // 解析扫描器输出并解码 Base64
    core::Bytes parseAndDecodePayload(const QByteArray& rawData);

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
