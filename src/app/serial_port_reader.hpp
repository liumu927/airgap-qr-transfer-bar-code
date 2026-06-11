#pragma once

#include <QObject>
#include <QSerialPort>
#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QTimer>

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
    void onBufferTimeout();

private:
    void processBuffer();
    void emitFrame(const QByteArray& frame);

    QSerialPort* port_;
    SerialPortConfig config_;
    QByteArray buffer_;
    QTimer bufferTimer_;
    qint64 lastReadTimeMs_ = 0;
};

} // namespace aqrt::app
