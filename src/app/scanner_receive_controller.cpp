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
#include <cstring>

namespace aqrt::app {

namespace {

constexpr char kBase64Prefix[] = "B64:";

int base64_decode_char(char c)
{
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

core::Bytes base64_decode(const QByteArray& text)
{
    core::Bytes decoded;
    decoded.reserve(static_cast<std::size_t>(text.size() * 3 / 4));

    std::uint32_t accum = 0;
    int bits = 0;
    for (int i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if (c == '=') break;
        const int val = base64_decode_char(c);
        if (val < 0) continue;  // 跳过无效字符
        accum = (accum << 6) | static_cast<std::uint32_t>(val);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            decoded.push_back(static_cast<std::uint8_t>((accum >> bits) & 0xFF));
        }
    }
    return decoded;
}

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

int ScannerReceiveController::missingChunkCount() const
{
    return static_cast<int>(collector_.missing_chunk_count());
}

QString ScannerReceiveController::missingFramesText() const
{
    const auto ranges = collector_.missing_ranges();
    if (ranges.empty()) {
        return {};
    }

    // 显示号 = chunk_index + 1，与发送端播放序号一致，便于用户照抄到发送端重传框
    QString text;
    for (const auto& range : ranges) {
        const quint32 start_no = static_cast<quint32>(range.start_index) + 1U;
        const quint32 end_no = static_cast<quint32>(range.start_index + range.count);
        if (!text.isEmpty()) {
            text += QStringLiteral(", ");
        }
        text += QString::number(start_no);
        if (range.count > 1U) {
            text += QStringLiteral("-") + QString::number(end_no);
        }
    }
    return text;
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
        setLastError(QStringLiteral("未选择串口"));
        setStatus(QStringLiteral("未选择串口"));
        return;
    }

    if (!serialReader_->open(portConfig_)) {
        setLastError(QStringLiteral("无法打开串口"));
        setStatus(QStringLiteral("无法打开串口"));
        return;
    }

    scanning_.store(true);
    setLastError({});
    setStatus(QStringLiteral("等待扫描..."));
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
    setStatus(QStringLiteral("已停止"));
    qInfo() << "ScannerReceiveController: 停止扫描";
    emit stateChanged();
}

void ScannerReceiveController::reset()
{
    collector_.reset();
    resetStats();
    scanning_.store(false);
    serialReader_->close();
    setStatus(QStringLiteral("就绪"));
    emit stateChanged();
}

void ScannerReceiveController::saveToFile(const QUrl& fileUrl)
{
    if (!hasVerifiedPayload()) {
        setLastError(QStringLiteral("没有已验证的文件"));
        setStatus(QStringLiteral("没有已验证的文件"));
        return;
    }

    const QString filePath = fileUrl.toLocalFile();
    if (filePath.isEmpty()) {
        setLastError(QStringLiteral("保存路径无效"));
        setStatus(QStringLiteral("保存路径无效"));
        return;
    }

    QFile out(filePath);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        setLastError(QStringLiteral("无法打开保存路径"));
        setStatus(QStringLiteral("无法打开保存路径"));
        return;
    }

    const auto& bytes = verifiedBytes();
    const auto written = out.write(
        reinterpret_cast<const char*>(bytes.data()),
        static_cast<qint64>(bytes.size()));

    if (written != static_cast<qint64>(bytes.size())) {
        setLastError(QStringLiteral("无法完整写入文件"));
        setStatus(QStringLiteral("无法完整写入文件"));
        return;
    }

    setLastError({});
    lastSavedPath_ = filePath;
    setStatus(QStringLiteral("已保存"));
    emit stateChanged();
}

void ScannerReceiveController::saveToDirectory(const QUrl& directoryUrl)
{
    if (!hasVerifiedPayload()) {
        setLastError(QStringLiteral("没有已验证的文件"));
        setStatus(QStringLiteral("没有已验证的文件"));
        return;
    }

    const QString dirPath = directoryUrl.toLocalFile();
    if (dirPath.isEmpty()) {
        setLastError(QStringLiteral("保存目录无效"));
        setStatus(QStringLiteral("保存目录无效"));
        return;
    }

    QDir dir(dirPath);
    if (!dir.mkpath(QStringLiteral("."))) {
        setLastError(QStringLiteral("无法创建保存目录"));
        setStatus(QStringLiteral("无法创建保存目录"));
        return;
    }

    const QString safeName = QFileInfo(verifiedFileName()).fileName();
    saveToFile(QUrl::fromLocalFile(dir.filePath(safeName)));
}

void ScannerReceiveController::copyReceivedText()
{
    if (receivedText_.isEmpty()) {
        setLastError(QStringLiteral("没有接收到的文本"));
        setStatus(QStringLiteral("没有接收到的文本"));
        return;
    }

    if (auto* clipboard = QGuiApplication::clipboard()) {
        clipboard->setText(receivedText_);
        setLastError({});
        setStatus(QStringLiteral("已复制文本"));
        emit stateChanged();
        return;
    }

    setLastError(QStringLiteral("剪贴板不可用"));
    setStatus(QStringLiteral("剪贴板不可用"));
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

    // 解析扫描器输出并解码（Base64 或原始二进制）
    core::Bytes payload = parseAndDecodePayload(frameData);

    // 尝试添加到收集器
    const auto result = collector_.add_payload(payload);

    if (!result.ok()) {
        ++rejectedFrameCount_;
        const QString error = QStringLiteral("帧被拒绝: %1")
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

        setStatus(QStringLiteral("已接收 %1").arg(QString::fromStdString(result.file_name)));
    } else if (result.total_chunks > 0) {
        setStatus(QStringLiteral("扫描中 %1/%2")
            .arg(result.received_chunks)
            .arg(result.total_chunks));
    } else {
        setStatus(QStringLiteral("扫描中..."));
    }

    qInfo() << "ScannerReceiveController: 帧已接受"
            << acceptedFrameCount_ << "/" << receivedFrameCount_;
    emit stateChanged();
}

void ScannerReceiveController::onSerialError(const QString& error)
{
    setLastError(QStringLiteral("串口错误: %1").arg(error));
    setStatus(QStringLiteral("串口错误: %1").arg(error));

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

core::Bytes ScannerReceiveController::parseAndDecodePayload(const QByteArray& rawData)
{
    // 清理扫描器输出：移除尾部 \r \n
    QByteArray cleaned = rawData;
    while (cleaned.endsWith('\r') || cleaned.endsWith('\n')) {
        cleaned.chop(1);
    }

    qInfo() << "ScannerReceiveController: parseAndDecodePayload, 清理后大小" << cleaned.size()
            << ", 前20字节:" << cleaned.left(20);

    // 查找 Base64 编码前缀 "B64:"（兼容扫描器添加的 CodeID 前缀）
    const QByteArray prefix(kBase64Prefix);
    const int prefixPos = cleaned.indexOf(prefix);
    if (prefixPos >= 0 && cleaned.size() > prefixPos + prefix.size()) {
        qInfo() << "ScannerReceiveController: 在位置" << prefixPos << "找到 B64: 前缀";
        auto decoded = base64_decode(cleaned.mid(prefixPos + prefix.size()));
        qInfo() << "ScannerReceiveController: Base64 解码完成, 解码后大小" << decoded.size();
        return decoded;
    }

    qInfo() << "ScannerReceiveController: 非 Base64 数据, 按原始二进制处理";

    // 原始二进制数据
    core::Bytes payload(static_cast<std::size_t>(cleaned.size()));
    if (!cleaned.isEmpty()) {
        std::memcpy(payload.data(), cleaned.constData(), static_cast<std::size_t>(cleaned.size()));
    }
    return payload;
}

} // namespace aqrt::app
