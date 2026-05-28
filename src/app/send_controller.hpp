#pragma once

#include "qr_adapter.hpp"

#include <QObject>
#include <QMetaObject>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QUrl>
#include <QVideoFrame>

#include <atomic>
#include <vector>

class QrImageProvider;
class QImage;
class QVideoSink;

class SendController final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString fileName READ fileName NOTIFY stateChanged)
    Q_PROPERTY(QString status READ status NOTIFY stateChanged)
    Q_PROPERTY(int frameCount READ frameCount NOTIFY stateChanged)
    Q_PROPERTY(int currentFrameIndex READ currentFrameIndex NOTIFY stateChanged)
    Q_PROPERTY(int imageRevision READ imageRevision NOTIFY imageChanged)
    Q_PROPERTY(bool playing READ playing NOTIFY stateChanged)
    Q_PROPERTY(bool feedbackScanning READ feedbackScanning NOTIFY stateChanged)
    Q_PROPERTY(bool resendMode READ resendMode NOTIFY stateChanged)
    Q_PROPERTY(int speedMode READ speedMode NOTIFY stateChanged)
    Q_PROPERTY(QString speedModeName READ speedModeName NOTIFY stateChanged)
    Q_PROPERTY(QStringList speedModeLabels READ speedModeLabels CONSTANT)
    Q_PROPERTY(bool cimbarAvailable READ cimbarAvailable CONSTANT)

public:
    explicit SendController(QrImageProvider* image_provider, QObject* parent = nullptr);

    [[nodiscard]] QString fileName() const;
    [[nodiscard]] QString status() const;
    [[nodiscard]] int frameCount() const;
    [[nodiscard]] int currentFrameIndex() const;
    [[nodiscard]] int imageRevision() const;
    [[nodiscard]] bool playing() const;
    [[nodiscard]] bool feedbackScanning() const;
    [[nodiscard]] bool resendMode() const;
    [[nodiscard]] int speedMode() const;
    [[nodiscard]] QString speedModeName() const;
    [[nodiscard]] QStringList speedModeLabels() const;
    [[nodiscard]] bool cimbarAvailable() const;

    Q_INVOKABLE void chooseOpenFile();
    Q_INVOKABLE void prepareFile(const QUrl& file_url);
    Q_INVOKABLE void prepareFileFromString(const QString& file_url);
    Q_INVOKABLE void prepareText(const QString& text);
    Q_INVOKABLE void setSpeedMode(int mode);
    Q_INVOKABLE void nextFrame();
    Q_INVOKABLE void previousFrame();
    Q_INVOKABLE void startPlayback();
    Q_INVOKABLE void stopPlayback();
    Q_INVOKABLE void attachFeedbackVideoSink(QObject* video_sink);
    Q_INVOKABLE void startFeedbackScan();
    Q_INVOKABLE void stopFeedbackScan();
    Q_INVOKABLE void clearResendFilter();

signals:
    void stateChanged();
    void imageChanged();

private:
    void prepareBytes(QString file_name, const QByteArray& data, bool text_message);
    void setStatus(QString status);
    void publishCurrentFrame();
    void clearFrames();
    void captureFeedbackVideoFrame(const QVideoFrame& frame);
    void decodeFeedbackVideoRaster(aqrt::qr::QrRasterImage raster);
    bool applyFeedbackPayload(const aqrt::core::Bytes& payload);

    QrImageProvider* image_provider_ = nullptr;
    QTimer playback_timer_;
    QPointer<QVideoSink> feedback_video_sink_;
    QMetaObject::Connection feedback_video_frame_connection_;
    QString file_name_;
    QString status_ = "Ready";
    std::vector<aqrt::qr::QrImage> qr_frames_;
    std::vector<aqrt::qr::QrImage> all_qr_frames_;
    std::vector<aqrt::qr::RgbImage> cimbar_frames_;
    aqrt::core::ManifestFrame manifest_{};
    int current_frame_index_ = 0;
    int image_revision_ = 0;
    int speed_mode_ = 0;
    std::atomic_bool feedback_scanning_ = false;
    std::atomic_bool feedback_decode_pending_ = false;
    bool playing_ = false;
    bool resend_mode_ = false;
    bool has_package_ = false;
};
