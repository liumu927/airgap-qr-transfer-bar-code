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

    bufferTimer_.setSingleShot(true);
    connect(&bufferTimer_, &QTimer::timeout, this, &SerialPortReader::onBufferTimeout);
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

    if (!port_->open(QIODevice::ReadWrite)) {
        emit errorOccurred(QStringLiteral("无法打开串口 %1: %2")
            .arg(config.portName, port_->errorString()));
        return false;
    }

    buffer_.clear();
    lastReadTimeMs_ = 0;
    bufferTimer_.setInterval(config.frameTimeoutMs > 0 ? config.frameTimeoutMs : 200);

    qInfo() << "SerialPortReader: 已打开串口" << config.portName;
    emit portOpened();
    return true;
}

void SerialPortReader::close()
{
    bufferTimer_.stop();
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
            return desc.isEmpty() ? portName : QStringLiteral("%1 (%2)").arg(portName, desc);
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
    qInfo() << "SerialPortReader: onReadyRead, 收到" << newData.size() << "字节, 缓冲区现有" << buffer_.size() << "字节";
    buffer_.append(newData);
    lastReadTimeMs_ = QDateTime::currentMSecsSinceEpoch();

    emit rawDataReceived(newData);
    processBuffer();

    // 如果缓冲区还有数据，重启超时定时器
    if (!buffer_.isEmpty()) {
        bufferTimer_.start();
    }
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

            qInfo() << "SerialPortReader: processBuffer 找到后缀, suffixPos=" << suffixPos;

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

void SerialPortReader::onBufferTimeout()
{
    // 超时：缓冲区有数据但一段时间没有新数据到达
    // 强制发出当前缓冲区内容作为一帧
    if (!buffer_.isEmpty()) {
        QByteArray frame = buffer_;
        buffer_.clear();

        // 移除前缀（如果配置了）
        if (!config_.framePrefix.isEmpty() && frame.startsWith(config_.framePrefix)) {
            frame.remove(0, config_.framePrefix.size());
        }

        // 移除尾部行尾字符
        while (frame.endsWith('\r') || frame.endsWith('\n')) {
            frame.chop(1);
        }

        if (!frame.isEmpty()) {
            emitFrame(frame);
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
