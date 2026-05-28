#pragma once

#include "qr_adapter.hpp"
#include "receive_frame_collector.hpp"

#include <QObject>
#include <QString>
#include <QStringList>
#include <QCameraDevice>
#include <QElapsedTimer>
#include <QMediaDevices>
#include <QMetaObject>
#include <QPointer>
#include <QVideoFrame>
#include <QUrl>

#include <atomic>
#include <cstdint>

class QImage;
class QrImageProvider;
class QVideoSink;

class ReceiveController final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString status READ status NOTIFY stateChanged)
    Q_PROPERTY(QString fileName READ fileName NOTIFY stateChanged)
    Q_PROPERTY(int receivedChunks READ receivedChunks NOTIFY stateChanged)
    Q_PROPERTY(int totalChunks READ totalChunks NOTIFY stateChanged)
    Q_PROPERTY(double progress READ progress NOTIFY stateChanged)
    Q_PROPERTY(bool scanning READ scanning NOTIFY stateChanged)
    Q_PROPERTY(bool completed READ completed NOTIFY stateChanged)
    Q_PROPERTY(bool cameraAvailable READ cameraAvailable NOTIFY cameraAvailabilityChanged)
    Q_PROPERTY(int videoFrameCount READ videoFrameCount NOTIFY stateChanged)
    Q_PROPERTY(int decodeAttemptCount READ decodeAttemptCount NOTIFY stateChanged)
    Q_PROPERTY(int decodeFailureCount READ decodeFailureCount NOTIFY stateChanged)
    Q_PROPERTY(int decodedQrFrameCount READ decodedQrFrameCount NOTIFY stateChanged)
    Q_PROPERTY(int acceptedQrFrameCount READ acceptedQrFrameCount NOTIFY stateChanged)
    Q_PROPERTY(int rejectedQrFrameCount READ rejectedQrFrameCount NOTIFY stateChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY stateChanged)
    Q_PROPERTY(QString lastSavedPath READ lastSavedPath NOTIFY stateChanged)
    Q_PROPERTY(QString receivedText READ receivedText NOTIFY stateChanged)
    Q_PROPERTY(bool receivedTextAvailable READ receivedTextAvailable NOTIFY stateChanged)
    Q_PROPERTY(QString selectedCameraName READ selectedCameraName NOTIFY stateChanged)
    Q_PROPERTY(int selectedCameraIndex READ selectedCameraIndex NOTIFY stateChanged)
    Q_PROPERTY(QStringList availableCameraNames READ availableCameraNames NOTIFY cameraAvailabilityChanged)
    Q_PROPERTY(int speedMode READ speedMode NOTIFY stateChanged)
    Q_PROPERTY(QString speedModeName READ speedModeName NOTIFY stateChanged)
    Q_PROPERTY(bool feedbackAvailable READ feedbackAvailable NOTIFY stateChanged)
    Q_PROPERTY(bool feedbackVisible READ feedbackVisible NOTIFY feedbackChanged)
    Q_PROPERTY(int feedbackImageRevision READ feedbackImageRevision NOTIFY feedbackChanged)
    Q_PROPERTY(int missingChunkCount READ missingChunkCount NOTIFY stateChanged)
    Q_PROPERTY(QString feedbackStatus READ feedbackStatus NOTIFY feedbackChanged)

public:
    explicit ReceiveController(QrImageProvider* feedback_image_provider = nullptr, QObject* parent = nullptr);

    [[nodiscard]] QString status() const;
    [[nodiscard]] QString fileName() const;
    [[nodiscard]] int receivedChunks() const;
    [[nodiscard]] int totalChunks() const;
    [[nodiscard]] double progress() const;
    [[nodiscard]] bool scanning() const;
    [[nodiscard]] bool completed() const;
    [[nodiscard]] bool cameraAvailable() const;
    [[nodiscard]] int videoFrameCount() const;
    [[nodiscard]] int decodeAttemptCount() const;
    [[nodiscard]] int decodeFailureCount() const;
    [[nodiscard]] int decodedQrFrameCount() const;
    [[nodiscard]] int acceptedQrFrameCount() const;
    [[nodiscard]] int rejectedQrFrameCount() const;
    [[nodiscard]] QString lastError() const;
    [[nodiscard]] QString lastSavedPath() const;
    [[nodiscard]] QString receivedText() const;
    [[nodiscard]] bool receivedTextAvailable() const;
    [[nodiscard]] QString selectedCameraName() const;
    [[nodiscard]] int selectedCameraIndex() const;
    [[nodiscard]] QStringList availableCameraNames() const;
    [[nodiscard]] int speedMode() const;
    [[nodiscard]] QString speedModeName() const;
    [[nodiscard]] bool feedbackAvailable() const;
    [[nodiscard]] bool feedbackVisible() const;
    [[nodiscard]] int feedbackImageRevision() const;
    [[nodiscard]] int missingChunkCount() const;
    [[nodiscard]] QString feedbackStatus() const;

    Q_INVOKABLE void startScanning();
    Q_INVOKABLE void stopScanning();
    Q_INVOKABLE void reset();
    Q_INVOKABLE void noteCameraError(const QString& message);
    Q_INVOKABLE void attachVideoSink(QObject* video_sink);
    Q_INVOKABLE void saveToFile(const QUrl& file_url);
    Q_INVOKABLE void saveToDirectory(const QUrl& directory_url);
    Q_INVOKABLE void saveToAppData();
    Q_INVOKABLE void chooseSaveLocation();
    Q_INVOKABLE void copyReceivedText();
    Q_INVOKABLE void showFeedbackQr();
    Q_INVOKABLE void hideFeedbackQr();
    Q_INVOKABLE void setSelectedCameraName(const QString& name);
    Q_INVOKABLE void setSelectedCamera(int index, const QString& name);
    Q_INVOKABLE void setSpeedMode(int mode);

signals:
    void stateChanged();
    void cameraAvailabilityChanged();
    void feedbackChanged();

private:
    void setStatus(QString status);
    void setLastError(QString error);
    void resetStats();
    void clearFeedback();
    void refreshCameraAvailability();
    void captureVideoFrame(const QVideoFrame& frame);
    void decodeVideoRaster(aqrt::qr::QrRasterImage raster);
    void saveToAndroidContentUri(const QString& uri_string);

    aqrt::app::ReceiveFrameCollector collector_;
    QrImageProvider* feedback_image_provider_ = nullptr;
    QMediaDevices media_devices_;
    QPointer<QVideoSink> video_sink_;
    QMetaObject::Connection video_frame_connection_;
    QElapsedTimer decode_timer_;
    QString status_ = "Ready";
    QString last_error_;
    QString last_saved_path_;
    QString received_text_;
    QString feedback_status_;
    QString selected_camera_name_;
    int selected_camera_index_ = -1;
    int speed_mode_ = 0;
    int decode_interval_ms_ = 150;
    int feedback_image_revision_ = 0;
    std::uint32_t feedback_seq_ = 0;
    std::atomic_bool scanning_ = false;
    std::atomic_bool decode_pending_ = false;
    std::atomic<qint64> last_decode_ms_ = 0;
    bool camera_available_ = false;
    std::atomic_int video_frame_count_ = 0;
    int decode_attempt_count_ = 0;
    int decode_failure_count_ = 0;
    int decoded_qr_frame_count_ = 0;
    int accepted_qr_frame_count_ = 0;
    int rejected_qr_frame_count_ = 0;
    bool feedback_visible_ = false;
};
