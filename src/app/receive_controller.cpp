#include "receive_controller.hpp"

#include "cimbar_payload.hpp"
#include "qr_image_provider.hpp"
#include "qr_cimbar_adapter.hpp"
#include "qr_libqrencode_adapter.hpp"
#include "qr_zxing_adapter.hpp"
#include "send_frame_builder.hpp"
#include "transfer_speed.hpp"
#include "video_frame_luminance.hpp"

#include <QByteArray>
#include <QClipboard>
#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QGuiApplication>
#include <QImage>
#include <QFile>
#include <QFileInfo>
#include <QIODevice>
#include <QStringDecoder>
#include <QStandardPaths>
#include <QVideoSink>

#ifdef Q_OS_ANDROID
#include <QJniEnvironment>
#include <QJniObject>
#include <QPermissions>
#include <QtCore/qcoreapplication_platform.h>
#include <QtCore/private/qandroidextras_p.h>
#endif

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>

namespace {

#ifdef Q_OS_ANDROID
constexpr int kAndroidCreateDocumentRequestCode = 4101;
constexpr int kAndroidActivityResultOk = -1;
#endif

class AtomicBoolResetGuard final {
public:
    explicit AtomicBoolResetGuard(std::atomic_bool& flag)
        : flag_(flag)
    {
    }

    ~AtomicBoolResetGuard()
    {
        flag_.store(false);
    }

    AtomicBoolResetGuard(const AtomicBoolResetGuard&) = delete;
    AtomicBoolResetGuard& operator=(const AtomicBoolResetGuard&) = delete;

private:
    std::atomic_bool& flag_;
};

QString collector_status(const aqrt::app::ReceiveFrameResult& result)
{
    if (result.completed) {
        return QString("Received %1").arg(QString::fromStdString(result.file_name));
    }
    if (result.total_chunks > 0) {
        return QString("Scanning %1 / %2").arg(result.received_chunks).arg(result.total_chunks);
    }
    return QStringLiteral("Scanning");
}

QString decode_utf8_text(const aqrt::core::Bytes& bytes)
{
    if (bytes.empty()) {
        return {};
    }

    QByteArray raw;
    raw.append(reinterpret_cast<const char*>(bytes.data()), static_cast<qsizetype>(bytes.size()));

    QStringDecoder decoder(QStringDecoder::Utf8);
    const QString text = decoder.decode(raw);
    if (decoder.hasError()) {
        return {};
    }
    return text;
}

aqrt::qr::QrRasterImage centered_crop(
    const aqrt::qr::QrRasterImage& image,
    std::uint32_t width_percent,
    std::uint32_t height_percent)
{
    if (!aqrt::qr::is_valid_raster_image_shape(image)
        || width_percent == 0U
        || height_percent == 0U
        || width_percent >= 100U
        || height_percent >= 100U) {
        return {};
    }

    aqrt::qr::QrRasterImage cropped;
    cropped.width = std::max<std::uint32_t>(64U, image.width * width_percent / 100U);
    cropped.height = std::max<std::uint32_t>(64U, image.height * height_percent / 100U);
    cropped.width = std::min(cropped.width, image.width);
    cropped.height = std::min(cropped.height, image.height);

    const std::uint32_t offset_x = (image.width - cropped.width) / 2U;
    const std::uint32_t offset_y = (image.height - cropped.height) / 2U;
    cropped.luminance.resize(static_cast<std::size_t>(cropped.width) * cropped.height);

    for (std::uint32_t y = 0; y < cropped.height; ++y) {
        const auto source_begin =
            image.luminance.begin()
            + static_cast<std::ptrdiff_t>(static_cast<std::size_t>(offset_y + y) * image.width + offset_x);
        const auto target_begin =
            cropped.luminance.begin()
            + static_cast<std::ptrdiff_t>(static_cast<std::size_t>(y) * cropped.width);
        std::copy(source_begin, source_begin + cropped.width, target_begin);
    }

    return cropped;
}

aqrt::qr::DecodeResult decode_qr_with_fallback_crops(const aqrt::qr::QrRasterImage& image)
{
    const aqrt::qr::ZxingQrDecoderAdapter decoder;
    auto decoded = decoder.decode(image);
    if (decoded.ok()) {
        return decoded;
    }

    const std::pair<std::uint32_t, std::uint32_t> crop_windows[] = {
        {85U, 85U},
        {70U, 70U},
        {55U, 55U},
        {45U, 70U},
    };

    for (const auto& crop : crop_windows) {
        const auto cropped = centered_crop(image, crop.first, crop.second);
        decoded = decoder.decode(cropped);
        if (decoded.ok()) {
            return decoded;
        }
    }

    return decoded;
}

#ifdef Q_OS_ANDROID
QJniObject make_java_string(const QString& value)
{
    return QJniObject::fromString(value);
}
#endif

} // namespace

ReceiveController::ReceiveController(QrImageProvider* feedback_image_provider, QObject* parent)
    : QObject(parent)
    , feedback_image_provider_(feedback_image_provider)
    , decode_interval_ms_(aqrt::app::transfer_speed_profile(speed_mode_).decode_interval_ms)
{
    decode_timer_.start();
    refreshCameraAvailability();
    connect(
        &media_devices_,
        &QMediaDevices::videoInputsChanged,
        this,
        [this]() {
            const bool previous = camera_available_;
            refreshCameraAvailability();
            if (previous != camera_available_) {
                emit cameraAvailabilityChanged();
                emit stateChanged();
            }
        });
}

ReceiveController::~ReceiveController() = default;

QString ReceiveController::status() const
{
    return status_;
}

QString ReceiveController::fileName() const
{
    if (cimbar_completed_) {
        return cimbar_file_name_;
    }
    return QString::fromStdString(collector_.file_name());
}

int ReceiveController::receivedChunks() const
{
    return static_cast<int>(collector_.received_chunks());
}

int ReceiveController::totalChunks() const
{
    return static_cast<int>(collector_.total_chunks());
}

double ReceiveController::progress() const
{
    if (aqrt::app::transfer_speed_uses_cimbar(speed_mode_)) {
        return cimbar_completed_ ? 1.0 : 0.0;
    }
    const int total = totalChunks();
    if (total <= 0) {
        return 0.0;
    }
    return std::clamp(static_cast<double>(receivedChunks()) / static_cast<double>(total), 0.0, 1.0);
}

bool ReceiveController::scanning() const
{
    return scanning_.load();
}

bool ReceiveController::completed() const
{
    return collector_.completed() || cimbar_completed_;
}

bool ReceiveController::cameraAvailable() const
{
    return camera_available_;
}

int ReceiveController::videoFrameCount() const
{
    return video_frame_count_.load();
}

int ReceiveController::decodeAttemptCount() const
{
    return decode_attempt_count_;
}

int ReceiveController::decodeFailureCount() const
{
    return decode_failure_count_;
}

int ReceiveController::decodedQrFrameCount() const
{
    return decoded_qr_frame_count_;
}

int ReceiveController::acceptedQrFrameCount() const
{
    return accepted_qr_frame_count_;
}

int ReceiveController::rejectedQrFrameCount() const
{
    return rejected_qr_frame_count_;
}

QString ReceiveController::lastError() const
{
    return last_error_;
}

QString ReceiveController::lastSavedPath() const
{
    return last_saved_path_;
}

QString ReceiveController::receivedText() const
{
    return received_text_;
}

bool ReceiveController::receivedTextAvailable() const
{
    return !received_text_.isEmpty();
}

QString ReceiveController::selectedCameraName() const
{
    return selected_camera_name_;
}

int ReceiveController::selectedCameraIndex() const
{
    return selected_camera_index_;
}

QStringList ReceiveController::availableCameraNames() const
{
    QStringList names;
    const auto inputs = QMediaDevices::videoInputs();
    for (int i = 0; i < inputs.size(); ++i) {
        const QString description = inputs[i].description();
        names.push_back(description.isEmpty() ? QString("Camera %1").arg(i + 1) : description);
    }
    return names;
}

int ReceiveController::speedMode() const
{
    return speed_mode_;
}

QString ReceiveController::speedModeName() const
{
    return QString::fromLatin1(aqrt::app::transfer_speed_profile(speed_mode_).name);
}

bool ReceiveController::feedbackAvailable() const
{
    if (aqrt::app::transfer_speed_uses_cimbar(speed_mode_)) {
        return false;
    }
    return !collector_.completed()
        && collector_.has_manifest()
        && (collector_.missing_chunk_count() > 0 || !collector_.has_end());
}

bool ReceiveController::feedbackVisible() const
{
    return feedback_visible_;
}

int ReceiveController::feedbackImageRevision() const
{
    return feedback_image_revision_;
}

int ReceiveController::missingChunkCount() const
{
    return static_cast<int>(collector_.missing_chunk_count());
}

QString ReceiveController::feedbackStatus() const
{
    return feedback_status_;
}

void ReceiveController::startScanning()
{
    qInfo() << "AirGapReceive startScanning requested";
#ifdef Q_OS_ANDROID
    if (auto* app = QCoreApplication::instance()) {
        const QCameraPermission camera_permission;
        const auto permission_status = app->checkPermission(camera_permission);
        qInfo() << "AirGapReceive camera permission status" << static_cast<int>(permission_status);
        if (permission_status == Qt::PermissionStatus::Undetermined) {
            setStatus("Waiting for camera permission");
            app->requestPermission(
                camera_permission,
                this,
                [this](const QPermission& permission) {
                    if (permission.status() == Qt::PermissionStatus::Granted) {
                        startScanning();
                        return;
                    }

                    scanning_.store(false);
                    setLastError("Camera permission denied");
                    setStatus("Camera permission denied");
                });
            return;
        }

        if (permission_status == Qt::PermissionStatus::Denied) {
            scanning_.store(false);
            setLastError("Camera permission denied");
            setStatus("Camera permission denied");
            return;
        }
    }
#endif

    refreshCameraAvailability();
    qInfo() << "AirGapReceive camera available" << camera_available_;
    if (!camera_available_) {
        scanning_.store(false);
        setLastError("No camera available");
        setStatus("No camera available");
        return;
    }

    if (scanning_.load()) {
        return;
    }

    decode_pending_.store(false);
    last_decode_ms_.store(0);
    scanning_.store(true);
    qInfo() << "AirGapReceive scanning started";
    setLastError({});
    setStatus("Scanning");
}

void ReceiveController::stopScanning()
{
    if (!scanning_.load()) {
        return;
    }

    scanning_.store(false);
    decode_pending_.store(false);
    qInfo() << "AirGapReceive scanning stopped";
    setStatus("Stopped");
}

void ReceiveController::reset()
{
    collector_.reset();
    if (cimbar_decoder_) {
        cimbar_decoder_->reset();
    }
    cimbar_file_bytes_.clear();
    cimbar_file_name_.clear();
    cimbar_completed_ = false;
    cimbar_text_message_ = false;
    resetStats();
    clearFeedback();
    scanning_.store(false);
    decode_pending_.store(false);
    setStatus("Ready");
}

void ReceiveController::noteCameraError(const QString& message)
{
    scanning_.store(false);
    decode_pending_.store(false);
    const QString error = message.isEmpty() ? QStringLiteral("Camera error") : message;
    setLastError(error);
    setStatus(error);
}

void ReceiveController::attachVideoSink(QObject* video_sink)
{
    if (video_frame_connection_) {
        disconnect(video_frame_connection_);
        video_frame_connection_ = {};
    }

    video_sink_ = qobject_cast<QVideoSink*>(video_sink);
    if (video_sink_ == nullptr) {
        setLastError("Video sink unavailable");
        setStatus("Video sink unavailable");
        return;
    }

    qInfo() << "AirGapReceive video sink attached";
    video_frame_connection_ = connect(
        video_sink_,
        &QVideoSink::videoFrameChanged,
        this,
        &ReceiveController::captureVideoFrame,
        Qt::DirectConnection);
}

void ReceiveController::saveToFile(const QUrl& file_url)
{
    if (!hasVerifiedPayload()) {
        setLastError("No verified file to save");
        setStatus("No verified file to save");
        return;
    }

    const QString file_path = file_url.toLocalFile();
    if (file_path.isEmpty()) {
        setLastError("Save path is not local");
        setStatus("Save path is not local");
        return;
    }

    QFile out(file_path);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        setLastError("Could not open save path");
        setStatus("Could not open save path");
        return;
    }

    const auto& bytes = verifiedBytes();
    const auto written = out.write(
        reinterpret_cast<const char*>(bytes.data()),
        static_cast<qint64>(bytes.size()));
    if (written != static_cast<qint64>(bytes.size())) {
        setLastError("Could not write complete file");
        setStatus("Could not write complete file");
        return;
    }

    setLastError({});
    last_saved_path_ = file_path;
    setStatus("Saved");
}

void ReceiveController::saveToDirectory(const QUrl& directory_url)
{
    if (!hasVerifiedPayload()) {
        setLastError("No verified file to save");
        setStatus("No verified file to save");
        return;
    }

    const QString directory_path = directory_url.toLocalFile();
    if (directory_path.isEmpty()) {
        setLastError("Save directory is not local");
        setStatus("Save directory is not local");
        return;
    }

    QDir dir(directory_path);
    if (!dir.mkpath(QStringLiteral("."))) {
        setLastError("Could not create save directory");
        setStatus("Could not create save directory");
        return;
    }

    const QString safe_name = QFileInfo(verifiedFileName()).fileName();
    saveToFile(QUrl::fromLocalFile(dir.filePath(safe_name)));
}

void ReceiveController::saveToAppData()
{
    if (!hasVerifiedPayload()) {
        setLastError("No verified file to save");
        setStatus("No verified file to save");
        return;
    }

    const QString app_data = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (app_data.isEmpty()) {
        setLastError("App data path unavailable");
        setStatus("App data path unavailable");
        return;
    }

    QDir dir(app_data);
    if (!dir.mkpath(QStringLiteral("received"))) {
        setLastError("Could not create app receive directory");
        setStatus("Could not create app receive directory");
        return;
    }

    const QString safe_name = QFileInfo(verifiedFileName()).fileName();
    const QString file_path = dir.filePath(QStringLiteral("received/") + safe_name);
    saveToFile(QUrl::fromLocalFile(file_path));
}

void ReceiveController::chooseSaveLocation()
{
    if (!hasVerifiedPayload()) {
        setLastError("No verified file to save");
        setStatus("No verified file to save");
        return;
    }

#ifdef Q_OS_ANDROID
    const QString safe_name = QFileInfo(verifiedFileName()).fileName();
    QJniObject intent(
        "android/content/Intent",
        "(Ljava/lang/String;)V",
        make_java_string(QStringLiteral("android.intent.action.CREATE_DOCUMENT")).object<jstring>());
    if (!intent.isValid()) {
        setLastError("Could not create Android save intent");
        setStatus("Could not create Android save intent");
        return;
    }

    intent.callObjectMethod(
        "addCategory",
        "(Ljava/lang/String;)Landroid/content/Intent;",
        make_java_string(QStringLiteral("android.intent.category.OPENABLE")).object<jstring>());
    intent.callObjectMethod(
        "setType",
        "(Ljava/lang/String;)Landroid/content/Intent;",
        make_java_string(QStringLiteral("application/octet-stream")).object<jstring>());
    intent.callObjectMethod(
        "putExtra",
        "(Ljava/lang/String;Ljava/lang/String;)Landroid/content/Intent;",
        make_java_string(QStringLiteral("android.intent.extra.TITLE")).object<jstring>(),
        make_java_string(safe_name).object<jstring>());

    setLastError({});
    setStatus("Choose save location");
    QtAndroidPrivate::startActivity(
        intent,
        kAndroidCreateDocumentRequestCode,
        [this](int request_code, int result_code, const QJniObject& data) {
            if (request_code != kAndroidCreateDocumentRequestCode) {
                return;
            }
            if (result_code != kAndroidActivityResultOk || !data.isValid()) {
                setStatus("Save canceled");
                return;
            }

            const QJniObject uri = data.callObjectMethod("getData", "()Landroid/net/Uri;");
            if (!uri.isValid()) {
                setLastError("Android save URI unavailable");
                setStatus("Android save URI unavailable");
                return;
            }

            const QString uri_string = uri.callObjectMethod("toString", "()Ljava/lang/String;").toString();
            saveToAndroidContentUri(uri_string);
        });
#else
    setLastError("Android save picker is unavailable on this platform");
    setStatus("Android save picker is unavailable on this platform");
#endif
}

void ReceiveController::copyReceivedText()
{
    if (received_text_.isEmpty()) {
        setLastError("No received text to copy");
        setStatus("No received text to copy");
        return;
    }

    if (auto* clipboard = QGuiApplication::clipboard()) {
        clipboard->setText(received_text_);
        setLastError({});
        setStatus("Copied text");
        return;
    }

    setLastError("Clipboard unavailable");
    setStatus("Clipboard unavailable");
}

void ReceiveController::showFeedbackQr()
{
    if (aqrt::app::transfer_speed_uses_cimbar(speed_mode_)) {
        feedback_status_ = "Cimbar mode does not use missing-frame feedback";
        setLastError(feedback_status_);
        emit feedbackChanged();
        setStatus(feedback_status_);
        return;
    }
    if (feedback_image_provider_ == nullptr) {
        feedback_status_ = "Feedback QR unavailable";
        setLastError(feedback_status_);
        emit feedbackChanged();
        setStatus(feedback_status_);
        return;
    }

    const auto request = collector_.missing_request_frame(++feedback_seq_);
    if (!request.has_value()) {
        feedback_status_ = "No missing frames to request";
        setLastError(feedback_status_);
        emit feedbackChanged();
        setStatus(feedback_status_);
        return;
    }

    const auto payload = aqrt::core::encode_frame(*request);
    const aqrt::qr::LibQrEncodeAdapter encoder(aqrt::app::kDefaultQrPayloadLimit);
    const auto encoded = encoder.encode(payload);
    if (!encoded.ok()) {
        feedback_status_ = QString("Feedback QR failed: %1").arg(QString::fromStdString(encoded.message));
        setLastError(feedback_status_);
        emit feedbackChanged();
        setStatus(feedback_status_);
        return;
    }

    stopScanning();
    feedback_image_provider_->setImage(encoded.image);
    feedback_visible_ = true;
    ++feedback_image_revision_;
    feedback_status_ = QString("Requesting %1 missing chunk(s)%2")
                           .arg(missingChunkCount())
                           .arg(collector_.has_end() ? QString{} : QStringLiteral(" + end"));
    setLastError({});
    emit feedbackChanged();
    setStatus(feedback_status_);
}

void ReceiveController::hideFeedbackQr()
{
    clearFeedback();
    setStatus("Stopped");
}

void ReceiveController::saveToAndroidContentUri(const QString& uri_string)
{
#ifdef Q_OS_ANDROID
    if (!hasVerifiedPayload()) {
        setLastError("No verified file to save");
        setStatus("No verified file to save");
        return;
    }

    QJniObject uri = QJniObject::callStaticObjectMethod(
        "android/net/Uri",
        "parse",
        "(Ljava/lang/String;)Landroid/net/Uri;",
        make_java_string(uri_string).object<jstring>());
    if (!uri.isValid()) {
        setLastError("Android save URI is invalid");
        setStatus("Android save URI is invalid");
        return;
    }

    const QJniObject context(QNativeInterface::QAndroidApplication::context());
    const QJniObject resolver = context.callObjectMethod(
        "getContentResolver",
        "()Landroid/content/ContentResolver;");
    if (!resolver.isValid()) {
        setLastError("Android content resolver unavailable");
        setStatus("Android content resolver unavailable");
        return;
    }

    const QJniObject output_stream = resolver.callObjectMethod(
        "openOutputStream",
        "(Landroid/net/Uri;)Ljava/io/OutputStream;",
        uri.object<jobject>());
    if (!output_stream.isValid()) {
        setLastError("Could not open Android save location");
        setStatus("Could not open Android save location");
        return;
    }

    QJniEnvironment env;
    const auto& bytes = verifiedBytes();
    constexpr qsizetype kChunkSize = 64 * 1024;
    bool ok = true;
    for (qsizetype offset = 0; offset < static_cast<qsizetype>(bytes.size()); offset += kChunkSize) {
        const auto remaining = static_cast<qsizetype>(bytes.size()) - offset;
        const auto chunk_size = std::min(kChunkSize, remaining);
        jbyteArray array = env->NewByteArray(static_cast<jsize>(chunk_size));
        if (array == nullptr) {
            ok = false;
            break;
        }

        env->SetByteArrayRegion(
            array,
            0,
            static_cast<jsize>(chunk_size),
            reinterpret_cast<const jbyte*>(bytes.data() + offset));
        output_stream.callMethod<void>("write", "([B)V", array);
        env->DeleteLocalRef(array);
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
            ok = false;
            break;
        }
    }

    output_stream.callMethod<void>("flush", "()V");
    output_stream.callMethod<void>("close", "()V");
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        ok = false;
    }

    if (!ok) {
        setLastError("Could not write Android save file");
        setStatus("Could not write Android save file");
        return;
    }

    setLastError({});
    last_saved_path_ = uri_string;
    setStatus("Saved");
#else
    Q_UNUSED(uri_string);
#endif
}

void ReceiveController::setSelectedCameraName(const QString& name)
{
    setSelectedCamera(selected_camera_index_, name);
}

void ReceiveController::setSelectedCamera(int index, const QString& name)
{
    if (selected_camera_index_ == index && selected_camera_name_ == name) {
        return;
    }

    selected_camera_index_ = index;
    selected_camera_name_ = name;
    emit stateChanged();
}

void ReceiveController::setSpeedMode(int mode)
{
    const int normalized = aqrt::app::normalize_transfer_speed_mode(mode);
    if (speed_mode_ == normalized) {
        return;
    }

    speed_mode_ = normalized;
    decode_interval_ms_ = aqrt::app::transfer_speed_profile(speed_mode_).decode_interval_ms;
    if (cimbar_decoder_) {
        cimbar_decoder_->reset();
    }
    collector_.reset();
    cimbar_file_bytes_.clear();
    cimbar_file_name_.clear();
    cimbar_completed_ = false;
    cimbar_text_message_ = false;
    received_text_.clear();
    clearFeedback();
    emit stateChanged();
}

void ReceiveController::setStatus(QString status)
{
    status_ = std::move(status);
    emit stateChanged();
}

void ReceiveController::setLastError(QString error)
{
    last_error_ = std::move(error);
}

void ReceiveController::resetStats()
{
    last_error_.clear();
    last_saved_path_.clear();
    received_text_.clear();
    video_frame_count_.store(0);
    decode_pending_.store(false);
    last_decode_ms_.store(0);
    decode_attempt_count_ = 0;
    decode_failure_count_ = 0;
    decoded_qr_frame_count_ = 0;
    accepted_qr_frame_count_ = 0;
    rejected_qr_frame_count_ = 0;
}

void ReceiveController::clearFeedback()
{
    feedback_visible_ = false;
    feedback_status_.clear();
    if (feedback_image_provider_ != nullptr) {
        feedback_image_provider_->clear();
    }
    ++feedback_image_revision_;
    emit feedbackChanged();
}

void ReceiveController::refreshCameraAvailability()
{
    camera_available_ = !QMediaDevices::videoInputs().isEmpty();
}

bool ReceiveController::hasVerifiedPayload() const
{
    return collector_.completed() || cimbar_completed_;
}

QString ReceiveController::verifiedFileName() const
{
    if (cimbar_completed_) {
        return cimbar_file_name_;
    }
    return QString::fromStdString(collector_.file_name());
}

const aqrt::core::Bytes& ReceiveController::verifiedBytes() const
{
    if (cimbar_completed_) {
        return cimbar_file_bytes_;
    }
    return collector_.assembled_bytes();
}

void ReceiveController::captureVideoFrame(const QVideoFrame& frame)
{
    if (!scanning_.load()) {
        return;
    }

    const int frame_count = video_frame_count_.fetch_add(1) + 1;
    if (frame_count <= 3 || frame_count % 30 == 0) {
        qInfo() << "AirGapReceive video frame" << frame_count;
    }

    if (decode_pending_.load()) {
        return;
    }

    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const qint64 last_decode = last_decode_ms_.load();
    if (last_decode > 0 && now - last_decode < decode_interval_ms_) {
        return;
    }
    if (decode_pending_.exchange(true)) {
        return;
    }
    last_decode_ms_.store(now);

    if (aqrt::app::transfer_speed_uses_cimbar(speed_mode_)) {
        auto image = aqrt::app::video_frame_to_rgb(frame);
        QMetaObject::invokeMethod(
            this,
            [this, image = std::move(image)]() mutable {
                decodeVideoRgb(std::move(image));
            },
            Qt::QueuedConnection);
    } else {
        auto raster = aqrt::app::video_frame_to_luminance(frame);
        QMetaObject::invokeMethod(
            this,
            [this, raster = std::move(raster)]() mutable {
                decodeVideoRaster(std::move(raster));
            },
            Qt::QueuedConnection);
    }
}

void ReceiveController::decodeVideoRaster(aqrt::qr::QrRasterImage raster)
{
    const AtomicBoolResetGuard clear_pending(decode_pending_);

    if (!scanning_.load() || collector_.completed()) {
        return;
    }

    decode_timer_.restart();
    ++decode_attempt_count_;

    if (!aqrt::qr::is_valid_raster_image_shape(raster)) {
        ++decode_failure_count_;
        setLastError("Invalid video frame");
        emit stateChanged();
        return;
    }

    const auto decoded = decode_qr_with_fallback_crops(raster);
    if (!decoded.ok()) {
        ++decode_failure_count_;
        emit stateChanged();
        return;
    }
    ++decoded_qr_frame_count_;

    const auto received = collector_.add_payload(decoded.payload);
    if (!received.ok()) {
        ++rejected_qr_frame_count_;
        const QString error = QString("%1: %2")
                                  .arg(aqrt::app::receive_frame_error_name(received.error))
                                  .arg(QString::fromStdString(received.message));
        setLastError(error);
        setStatus(error);
        return;
    }
    ++accepted_qr_frame_count_;

    if (received.completed) {
        scanning_.store(false);
        clearFeedback();
        received_text_ = received.text_message ? decode_utf8_text(received.assembled_bytes) : QString{};
    }
    setStatus(collector_status(received));
}

void ReceiveController::decodeVideoRgb(aqrt::qr::RgbImage image)
{
    const AtomicBoolResetGuard clear_pending(decode_pending_);

    if (!scanning_.load() || cimbar_completed_) {
        return;
    }

    decode_timer_.restart();
    ++decode_attempt_count_;

    if (!aqrt::qr::is_valid_rgb_image_shape(image)) {
        ++decode_failure_count_;
        setLastError("Invalid RGB video frame");
        emit stateChanged();
        return;
    }

    if (!aqrt::qr::cimbar_backend_available()) {
        ++decode_failure_count_;
        setLastError("Cimbar backend is not enabled in this build");
        setStatus("Cimbar backend is not enabled in this build");
        return;
    }

    if (!cimbar_decoder_) {
        cimbar_decoder_ = std::make_unique<aqrt::qr::CimbarFileDecoder>();
    }

    const auto decoded = cimbar_decoder_->decode_rgb(image);
    if (!decoded.ok()) {
        ++decode_failure_count_;
        setLastError(QString("Cimbar decode failed: %1").arg(QString::fromStdString(decoded.message)));
        emit stateChanged();
        return;
    }
    if (!decoded.completed) {
        ++decode_failure_count_;
        emit stateChanged();
        return;
    }

    const auto payload = aqrt::app::decode_cimbar_payload(decoded.file_bytes);
    if (!payload.ok()) {
        ++rejected_qr_frame_count_;
        const QString error = QString("Cimbar payload rejected: %1")
                                  .arg(aqrt::app::cimbar_payload_error_name(payload.error));
        setLastError(error);
        setStatus(error);
        return;
    }

    ++decoded_qr_frame_count_;
    ++accepted_qr_frame_count_;
    cimbar_file_name_ = QString::fromStdString(payload.payload.file_name);
    cimbar_file_bytes_ = std::move(payload.payload.file_bytes);
    cimbar_text_message_ = payload.payload.text_message;
    cimbar_completed_ = true;
    scanning_.store(false);
    clearFeedback();
    received_text_ = cimbar_text_message_ ? decode_utf8_text(cimbar_file_bytes_) : QString{};
    setStatus(QString("Received %1").arg(cimbar_file_name_));
}
