#include "send_controller.hpp"

#include "qr_image_provider.hpp"
#include "qr_libqrencode_adapter.hpp"
#include "qr_zxing_adapter.hpp"
#include "session.hpp"
#include "send_frame_builder.hpp"
#include "transfer_speed.hpp"
#include "video_frame_luminance.hpp"

#include <QByteArray>
#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QIODevice>
#include <QPermissions>
#include <QUrl>
#include <QVideoSink>

#ifdef Q_OS_ANDROID
#include <QJniEnvironment>
#include <QJniObject>
#include <QtCore/qcoreapplication_platform.h>
#include <QtCore/private/qandroidextras_p.h>
#endif

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <string>
#include <utility>
#include <variant>

namespace {

#ifdef Q_OS_ANDROID
constexpr int kAndroidOpenDocumentRequestCode = 4201;
constexpr int kAndroidActivityResultOk = -1;
#endif

struct SelectedFile {
    QByteArray data;
    QString file_name;
    QString error;
    bool ok = false;
};

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

aqrt::core::Bytes to_bytes(const QByteArray& data)
{
    aqrt::core::Bytes bytes(static_cast<std::size_t>(data.size()));
    std::transform(data.begin(), data.end(), bytes.begin(), [](char value) {
        return static_cast<std::uint8_t>(value);
    });
    return bytes;
}

QUrl normalize_selected_url(const QString& raw_url)
{
    QUrl url(raw_url);
    if (url.isLocalFile()) {
        QString local_path = url.toLocalFile();
        if (local_path.startsWith(QStringLiteral("/content:"))) {
            local_path.remove(0, 1);
        }
        if (local_path.startsWith(QStringLiteral("content:"), Qt::CaseInsensitive)) {
            return QUrl(local_path);
        }
    }

    if (url.scheme().isEmpty() && raw_url.startsWith(QStringLiteral("content:"), Qt::CaseInsensitive)) {
        return QUrl(raw_url);
    }

    return url;
}

#ifdef Q_OS_ANDROID
QJniObject make_java_string(const QString& value)
{
    return QJniObject::fromString(value);
}

QJniObject android_uri_from_url(const QUrl& url)
{
    return QJniObject::callStaticObjectMethod(
        "android/net/Uri",
        "parse",
        "(Ljava/lang/String;)Landroid/net/Uri;",
        make_java_string(url.toString()).object<jstring>());
}

QString android_display_name(const QJniObject& resolver, const QJniObject& uri)
{
    QJniObject cursor = resolver.callObjectMethod(
        "query",
        "(Landroid/net/Uri;[Ljava/lang/String;Ljava/lang/String;[Ljava/lang/String;Ljava/lang/String;)Landroid/database/Cursor;",
        uri.object<jobject>(),
        nullptr,
        nullptr,
        nullptr,
        nullptr);
    if (!cursor.isValid()) {
        return {};
    }

    QString name;
    const bool has_row = cursor.callMethod<jboolean>("moveToFirst", "()Z");
    if (has_row) {
        const jint name_column = cursor.callMethod<jint>(
            "getColumnIndex",
            "(Ljava/lang/String;)I",
            make_java_string(QStringLiteral("_display_name")).object<jstring>());
        if (name_column >= 0) {
            name = cursor
                       .callObjectMethod(
                           "getString",
                           "(I)Ljava/lang/String;",
                           name_column)
                       .toString();
        }
    }
    cursor.callMethod<void>("close", "()V");
    return name;
}

SelectedFile read_android_content_file(const QUrl& file_url)
{
    SelectedFile result;
    QJniObject uri = android_uri_from_url(file_url);
    if (!uri.isValid()) {
        result.error = QStringLiteral("Selected file URI is invalid");
        return result;
    }

    const QJniObject context(QNativeInterface::QAndroidApplication::context());
    const QJniObject resolver = context.callObjectMethod(
        "getContentResolver",
        "()Landroid/content/ContentResolver;");
    if (!resolver.isValid()) {
        result.error = QStringLiteral("Android content resolver unavailable");
        return result;
    }

    QJniObject input_stream = resolver.callObjectMethod(
        "openInputStream",
        "(Landroid/net/Uri;)Ljava/io/InputStream;",
        uri.object<jobject>());
    if (!input_stream.isValid()) {
        result.error = QStringLiteral("Could not open selected file");
        return result;
    }

    result.file_name = android_display_name(resolver, uri);
    if (result.file_name.isEmpty()) {
        result.file_name = QFileInfo(file_url.path()).fileName();
    }
    if (result.file_name.isEmpty()) {
        result.file_name = QStringLiteral("selected-file");
    }

    QJniEnvironment env;
    constexpr qsizetype kBufferSize = 64 * 1024;
    jbyteArray buffer = env->NewByteArray(static_cast<jsize>(kBufferSize));
    if (buffer == nullptr) {
        input_stream.callMethod<void>("close", "()V");
        result.error = QStringLiteral("Could not allocate file read buffer");
        return result;
    }

    QByteArray chunk;
    chunk.resize(kBufferSize);
    bool ok = true;
    while (true) {
        const jint read = input_stream.callMethod<jint>("read", "([B)I", buffer);
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
            ok = false;
            break;
        }
        if (read < 0) {
            break;
        }
        if (read == 0) {
            continue;
        }

        env->GetByteArrayRegion(buffer, 0, read, reinterpret_cast<jbyte*>(chunk.data()));
        result.data.append(chunk.constData(), read);
    }

    env->DeleteLocalRef(buffer);
    input_stream.callMethod<void>("close", "()V");
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        ok = false;
    }

    if (!ok) {
        result.data.clear();
        result.error = QStringLiteral("Could not read selected file");
        return result;
    }

    result.ok = true;
    return result;
}
#endif

SelectedFile read_selected_file(const QUrl& file_url)
{
    const QString file_path = file_url.toLocalFile();
    if (!file_path.isEmpty()) {
#ifdef Q_OS_ANDROID
        if (file_path.startsWith(QStringLiteral("content:"), Qt::CaseInsensitive)
            || file_path.startsWith(QStringLiteral("/content:"), Qt::CaseInsensitive)) {
            QString content_url = file_path;
            if (content_url.startsWith(QStringLiteral("/content:"), Qt::CaseInsensitive)) {
                content_url.remove(0, 1);
            }
            return read_android_content_file(QUrl(content_url));
        }
#endif

        QFile file(file_path);
        if (!file.open(QIODevice::ReadOnly)) {
            return {{}, {}, QStringLiteral("Could not open file"), false};
        }

        return {file.readAll(), QFileInfo(file_path).fileName(), {}, true};
    }

#ifdef Q_OS_ANDROID
    if (file_url.scheme().compare(QStringLiteral("content"), Qt::CaseInsensitive) == 0) {
        return read_android_content_file(file_url);
    }
#endif

    return {{},
            {},
            QStringLiteral("Selected file is not local (scheme: %1)")
                .arg(file_url.scheme().isEmpty() ? QStringLiteral("<empty>") : file_url.scheme()),
            false};
}

} // namespace

SendController::SendController(QrImageProvider* image_provider, QObject* parent)
    : QObject(parent)
    , image_provider_(image_provider)
{
    playback_timer_.setInterval(aqrt::app::transfer_speed_profile(speed_mode_).playback_interval_ms);
    connect(&playback_timer_, &QTimer::timeout, this, &SendController::nextFrame);
}

QString SendController::fileName() const
{
    return file_name_;
}

QString SendController::status() const
{
    return status_;
}

int SendController::frameCount() const
{
    return static_cast<int>(qr_frames_.size());
}

int SendController::currentFrameIndex() const
{
    return current_frame_index_;
}

int SendController::imageRevision() const
{
    return image_revision_;
}

bool SendController::playing() const
{
    return playing_;
}

bool SendController::feedbackScanning() const
{
    return feedback_scanning_.load();
}

bool SendController::resendMode() const
{
    return resend_mode_;
}

int SendController::speedMode() const
{
    return speed_mode_;
}

QString SendController::speedModeName() const
{
    return QString::fromLatin1(aqrt::app::transfer_speed_profile(speed_mode_).name);
}

void SendController::chooseOpenFile()
{
#ifdef Q_OS_ANDROID
    QJniObject intent(
        "android/content/Intent",
        "(Ljava/lang/String;)V",
        make_java_string(QStringLiteral("android.intent.action.OPEN_DOCUMENT")).object<jstring>());
    if (!intent.isValid()) {
        setStatus("Could not create Android open intent");
        return;
    }

    intent.callObjectMethod(
        "addCategory",
        "(Ljava/lang/String;)Landroid/content/Intent;",
        make_java_string(QStringLiteral("android.intent.category.OPENABLE")).object<jstring>());
    intent.callObjectMethod(
        "setType",
        "(Ljava/lang/String;)Landroid/content/Intent;",
        make_java_string(QStringLiteral("*/*")).object<jstring>());

    setStatus("Choose file");
    QtAndroidPrivate::startActivity(
        intent,
        kAndroidOpenDocumentRequestCode,
        [this](int request_code, int result_code, const QJniObject& data) {
            if (request_code != kAndroidOpenDocumentRequestCode) {
                return;
            }
            if (result_code != kAndroidActivityResultOk || !data.isValid()) {
                setStatus("Open canceled");
                return;
            }

            const QJniObject uri = data.callObjectMethod("getData", "()Landroid/net/Uri;");
            if (!uri.isValid()) {
                setStatus("Android file URI unavailable");
                return;
            }

            prepareFileFromString(uri.callObjectMethod("toString", "()Ljava/lang/String;").toString());
        });
#else
    setStatus("Android open picker is unavailable on this platform");
#endif
}

void SendController::prepareFile(const QUrl& file_url)
{
    stopPlayback();
    clearFrames();

    const SelectedFile selected_file = read_selected_file(file_url);
    if (!selected_file.ok) {
        setStatus(selected_file.error);
        return;
    }

    prepareBytes(selected_file.file_name, selected_file.data, false);
}

void SendController::prepareFileFromString(const QString& file_url)
{
    prepareFile(normalize_selected_url(file_url));
}

void SendController::prepareText(const QString& text)
{
    stopPlayback();
    clearFrames();

    if (text.isEmpty()) {
        setStatus("Text is empty");
        return;
    }

    prepareBytes(QStringLiteral("message.txt"), text.toUtf8(), true);
}

void SendController::prepareBytes(QString file_name, const QByteArray& data, bool text_message)
{
    const auto file_bytes = to_bytes(data);
    file_name_ = std::move(file_name);
    const QByteArray file_name_utf8 = file_name_.toUtf8();

    const auto session_id = aqrt::core::generate_session_id();
    const auto& speed_profile = aqrt::app::transfer_speed_profile(speed_mode_);
    const aqrt::qr::LibQrEncodeAdapter encoder(aqrt::app::kDefaultQrPayloadLimit);
    auto build_result = aqrt::app::build_send_package(
        session_id,
        std::string(file_name_utf8.constData(), static_cast<std::size_t>(file_name_utf8.size())),
        file_bytes,
        speed_profile.chunk_size,
        encoder,
        text_message ? aqrt::core::kManifestFlagTextMessage : 0U);
    if (!build_result.ok()) {
        clearFrames();
        setStatus(QString("%1: %2")
                      .arg(aqrt::app::send_build_error_name(build_result.error))
                      .arg(QString::fromStdString(build_result.message)));
        return;
    }

    manifest_ = build_result.package.manifest;
    all_qr_frames_ = std::move(build_result.package.qr_frames);
    qr_frames_ = all_qr_frames_;
    has_package_ = true;
    resend_mode_ = false;
    current_frame_index_ = 0;
    publishCurrentFrame();
    setStatus(QString("Prepared %1 frame(s), %2 mode").arg(frameCount()).arg(speedModeName()));
}

void SendController::setSpeedMode(int mode)
{
    const int normalized = aqrt::app::normalize_transfer_speed_mode(mode);
    if (speed_mode_ == normalized) {
        return;
    }

    speed_mode_ = normalized;
    playback_timer_.setInterval(aqrt::app::transfer_speed_profile(speed_mode_).playback_interval_ms);
    emit stateChanged();
}

void SendController::nextFrame()
{
    if (qr_frames_.empty()) {
        return;
    }
    current_frame_index_ = (current_frame_index_ + 1) % frameCount();
    publishCurrentFrame();
    emit stateChanged();
}

void SendController::previousFrame()
{
    if (qr_frames_.empty()) {
        return;
    }
    current_frame_index_ = (current_frame_index_ + frameCount() - 1) % frameCount();
    publishCurrentFrame();
    emit stateChanged();
}

void SendController::startPlayback()
{
    if (qr_frames_.empty() || playing_) {
        return;
    }
    playing_ = true;
    playback_timer_.start();
    emit stateChanged();
}

void SendController::stopPlayback()
{
    if (!playing_) {
        return;
    }
    playback_timer_.stop();
    playing_ = false;
    emit stateChanged();
}

void SendController::attachFeedbackVideoSink(QObject* video_sink)
{
    if (feedback_video_frame_connection_) {
        disconnect(feedback_video_frame_connection_);
        feedback_video_frame_connection_ = {};
    }

    feedback_video_sink_ = qobject_cast<QVideoSink*>(video_sink);
    if (feedback_video_sink_ == nullptr) {
        setStatus("Feedback camera sink unavailable");
        return;
    }

    feedback_video_frame_connection_ = connect(
        feedback_video_sink_,
        &QVideoSink::videoFrameChanged,
        this,
        &SendController::captureFeedbackVideoFrame,
        Qt::DirectConnection);
}

void SendController::startFeedbackScan()
{
    if (!has_package_) {
        setStatus("Prepare a file or text before scanning feedback");
        return;
    }

    stopPlayback();
    if (auto* app = QCoreApplication::instance()) {
        const QCameraPermission camera_permission;
        const auto permission_status = app->checkPermission(camera_permission);
        if (permission_status == Qt::PermissionStatus::Undetermined) {
            setStatus("Waiting for camera permission");
            app->requestPermission(
                camera_permission,
                this,
                [this](const QPermission& permission) {
                    if (permission.status() == Qt::PermissionStatus::Granted) {
                        startFeedbackScan();
                        return;
                    }
                    feedback_scanning_.store(false);
                    setStatus("Camera permission denied");
                });
            return;
        }
        if (permission_status == Qt::PermissionStatus::Denied) {
            feedback_scanning_.store(false);
            setStatus("Camera permission denied");
            return;
        }
    }

    feedback_decode_pending_.store(false);
    feedback_scanning_.store(true);
    setStatus("Scan receiver feedback QR");
}

void SendController::stopFeedbackScan()
{
    if (!feedback_scanning_.load()) {
        return;
    }
    feedback_scanning_.store(false);
    feedback_decode_pending_.store(false);
    setStatus("Feedback scan stopped");
}

void SendController::clearResendFilter()
{
    if (!has_package_ || all_qr_frames_.empty()) {
        return;
    }

    qr_frames_ = all_qr_frames_;
    current_frame_index_ = 0;
    resend_mode_ = false;
    publishCurrentFrame();
    setStatus(QString("Restored full loop: %1 frame(s)").arg(frameCount()));
}

void SendController::setStatus(QString status)
{
    status_ = std::move(status);
    emit stateChanged();
}

void SendController::publishCurrentFrame()
{
    if (image_provider_ == nullptr || qr_frames_.empty()) {
        return;
    }
    image_provider_->setImage(qr_frames_[static_cast<std::size_t>(current_frame_index_)]);
    ++image_revision_;
    emit imageChanged();
}

void SendController::clearFrames()
{
    qr_frames_.clear();
    all_qr_frames_.clear();
    manifest_ = {};
    current_frame_index_ = 0;
    resend_mode_ = false;
    has_package_ = false;
    feedback_scanning_.store(false);
    feedback_decode_pending_.store(false);
    if (image_provider_ != nullptr) {
        image_provider_->clear();
    }
    ++image_revision_;
    emit imageChanged();
    emit stateChanged();
}

void SendController::captureFeedbackVideoFrame(const QVideoFrame& frame)
{
    if (!feedback_scanning_.load() || feedback_decode_pending_.exchange(true)) {
        return;
    }

    auto raster = aqrt::app::video_frame_to_luminance(frame);
    QMetaObject::invokeMethod(
        this,
        [this, raster = std::move(raster)]() mutable {
            decodeFeedbackVideoRaster(std::move(raster));
        },
        Qt::QueuedConnection);
}

void SendController::decodeFeedbackVideoRaster(aqrt::qr::QrRasterImage raster)
{
    const AtomicBoolResetGuard clear_pending(feedback_decode_pending_);
    if (!feedback_scanning_.load()) {
        return;
    }

    if (!aqrt::qr::is_valid_raster_image_shape(raster)) {
        return;
    }

    const aqrt::qr::ZxingQrDecoderAdapter decoder;
    const auto decoded = decoder.decode(raster);
    if (!decoded.ok()) {
        return;
    }

    if (applyFeedbackPayload(decoded.payload)) {
        feedback_scanning_.store(false);
        feedback_decode_pending_.store(false);
    }
}

bool SendController::applyFeedbackPayload(const aqrt::core::Bytes& payload)
{
    const auto parsed = aqrt::core::decode_frame(payload);
    if (!parsed.ok()) {
        setStatus(QString("Feedback decode rejected: %1").arg(aqrt::core::parse_error_name(parsed.error)));
        return false;
    }
    if (!std::holds_alternative<aqrt::core::MissingRequestFrame>(parsed.frame)) {
        setStatus("Feedback QR is not a missing request");
        return false;
    }

    const auto& request = std::get<aqrt::core::MissingRequestFrame>(parsed.frame);
    if (!has_package_ || all_qr_frames_.empty()) {
        setStatus("No prepared transfer to match feedback");
        return false;
    }
    if (request.session_id != manifest_.session_id || request.file_id != manifest_.file_id) {
        setStatus("Feedback belongs to another transfer");
        return false;
    }
    if (request.total_chunks != manifest_.total_chunks) {
        setStatus("Feedback chunk count does not match");
        return false;
    }

    std::vector<aqrt::qr::QrImage> resend_frames;
    resend_frames.reserve(2U + request.ranges.size());
    resend_frames.push_back(all_qr_frames_.front());

    std::uint64_t missing_count = 0;
    for (const auto& range : request.ranges) {
        if (range.count == 0 || range.start_index >= manifest_.total_chunks) {
            setStatus("Feedback contains an invalid range");
            return false;
        }
        if (range.count > manifest_.total_chunks - range.start_index) {
            setStatus("Feedback range exceeds transfer");
            return false;
        }

        for (std::uint32_t offset = 0; offset < range.count; ++offset) {
            const auto chunk_index = range.start_index + offset;
            const auto frame_index = static_cast<std::size_t>(chunk_index) + 1U;
            if (frame_index >= all_qr_frames_.size() - 1U) {
                setStatus("Feedback references a missing local frame");
                return false;
            }
            resend_frames.push_back(all_qr_frames_[frame_index]);
            ++missing_count;
        }
    }

    const bool need_end = (request.request_flags & aqrt::core::kMissingRequestFlagNeedEnd) != 0U;
    if (need_end || !request.ranges.empty()) {
        resend_frames.push_back(all_qr_frames_.back());
    }
    if (resend_frames.size() <= 1U) {
        setStatus("Feedback did not request any frames");
        return false;
    }

    qr_frames_ = std::move(resend_frames);
    current_frame_index_ = 0;
    resend_mode_ = true;
    publishCurrentFrame();
    setStatus(QString("Resend loop: %1 missing chunk(s), %2 frame(s)")
                  .arg(static_cast<qulonglong>(missing_count))
                  .arg(frameCount()));
    return true;
}
