#include "qr_image_provider.hpp"
#include "receive_controller.hpp"
#include "send_controller.hpp"
#include "transfer_speed.hpp"

#include <QCoreApplication>
#include <QCommandLineParser>
#include <QFile>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QTextStream>
#include <QUrl>

#include <cstddef>

namespace {

int parse_speed_mode(const QString& value)
{
    if (value.isEmpty()) {
        return aqrt::app::kDefaultTransferSpeedMode;
    }

    bool ok = false;
    const int numeric_mode = value.toInt(&ok);
    if (ok) {
        return aqrt::app::normalize_transfer_speed_mode(numeric_mode);
    }

    const QString normalized = value.trimmed().toLower();
    for (int i = 0; i < static_cast<int>(aqrt::app::kTransferSpeedProfiles.size()); ++i) {
        if (normalized == QString::fromLatin1(aqrt::app::kTransferSpeedProfiles[static_cast<std::size_t>(i)].name).toLower()) {
            return i;
        }
    }

    return aqrt::app::kDefaultTransferSpeedMode;
}

} // namespace

int main(int argc, char* argv[])
{
#ifdef Q_OS_ANDROID
    qputenv("QSG_RENDER_LOOP", "basic");
#endif

    QGuiApplication app(argc, argv);
    QGuiApplication::setApplicationName(QStringLiteral("AirGap QR Transfer"));
    QGuiApplication::setOrganizationName(QStringLiteral("AirGapQrTransfer"));
    QQuickStyle::setStyle(QStringLiteral("Fusion"));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("AirGap QR Transfer"));
    parser.addHelpOption();
    parser.addOption({
        QStringLiteral("send-file"),
        QStringLiteral("Prepare a local file for QR sending on startup."),
        QStringLiteral("path"),
    });
    parser.addOption({
        QStringLiteral("send-text"),
        QStringLiteral("Prepare UTF-8 text for QR sending on startup."),
        QStringLiteral("text"),
    });
    parser.addOption({
        QStringLiteral("play"),
        QStringLiteral("Start QR playback after --send-file is prepared."),
    });
    parser.addOption({
        QStringLiteral("fullscreen"),
        QStringLiteral("Open the main window in full screen mode."),
    });
    parser.addOption({
        QStringLiteral("receive"),
        QStringLiteral("Open the Receive tab on startup."),
    });
    parser.addOption({
        QStringLiteral("scan"),
        QStringLiteral("Start camera scanning when the Receive view is ready."),
    });
    parser.addOption({
        QStringLiteral("camera-index"),
        QStringLiteral("Select the receive camera by zero-based index on startup."),
        QStringLiteral("index"),
    });
    parser.addOption({
        QStringLiteral("save-dir"),
        QStringLiteral("Automatically save a verified received file into the given local directory."),
        QStringLiteral("path"),
    });
    parser.addOption({
        QStringLiteral("status-file"),
        QStringLiteral("Write receive diagnostics to the given local text file whenever state changes."),
        QStringLiteral("path"),
    });
    parser.addOption({
        QStringLiteral("speed-mode"),
        QStringLiteral("Select transfer speed: safe, balanced, fast, or cimbar when that backend is enabled."),
        QStringLiteral("mode"),
    });
    parser.process(app);

    QQmlApplicationEngine engine;
    auto* image_provider = new QrImageProvider();
    auto* feedback_image_provider = new QrImageProvider();
    engine.addImageProvider(QStringLiteral("airgapqr"), image_provider);
    engine.addImageProvider(QStringLiteral("airgapfeedback"), feedback_image_provider);

    SendController send_controller(image_provider);
    ReceiveController receive_controller(feedback_image_provider);
    const int startup_speed_mode = parse_speed_mode(parser.value(QStringLiteral("speed-mode")));
    send_controller.setSpeedMode(startup_speed_mode);
    receive_controller.setSpeedMode(startup_speed_mode);
    engine.rootContext()->setContextProperty(QStringLiteral("sendController"), &send_controller);
    engine.rootContext()->setContextProperty(QStringLiteral("receiveController"), &receive_controller);
    engine.rootContext()->setContextProperty(
        QStringLiteral("startupMode"),
        parser.isSet(QStringLiteral("receive")) ? 1 : 0);
    engine.rootContext()->setContextProperty(
        QStringLiteral("startupFullScreen"),
        parser.isSet(QStringLiteral("fullscreen")));
    engine.rootContext()->setContextProperty(
        QStringLiteral("startupAutoScan"),
        parser.isSet(QStringLiteral("scan")));
    bool camera_index_ok = false;
    const int startup_camera_index = parser.value(QStringLiteral("camera-index")).toInt(&camera_index_ok);
    engine.rootContext()->setContextProperty(
        QStringLiteral("startupCameraIndex"),
        camera_index_ok ? startup_camera_index : -1);

    bool auto_saved_received_file = false;
    const QString auto_save_dir = parser.value(QStringLiteral("save-dir"));
    if (!auto_save_dir.isEmpty()) {
        QObject::connect(
            &receive_controller,
            &ReceiveController::stateChanged,
            &receive_controller,
            [&receive_controller, auto_save_dir, &auto_saved_received_file]() {
                if (auto_saved_received_file || !receive_controller.completed()) {
                    return;
                }

                auto_saved_received_file = true;
                receive_controller.saveToDirectory(QUrl::fromLocalFile(auto_save_dir));
            });
    }

    const QString status_file = parser.value(QStringLiteral("status-file"));
    auto write_status_file = [&receive_controller, status_file]() {
        if (status_file.isEmpty()) {
            return;
        }

        QFile file(status_file);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
            return;
        }

        QTextStream out(&file);
        out << "status=" << receive_controller.status() << '\n';
        out << "fileName=" << receive_controller.fileName() << '\n';
        out << "scanning=" << (receive_controller.scanning() ? "true" : "false") << '\n';
        out << "completed=" << (receive_controller.completed() ? "true" : "false") << '\n';
        out << "receivedChunks=" << receive_controller.receivedChunks() << '\n';
        out << "totalChunks=" << receive_controller.totalChunks() << '\n';
        out << "videoFrameCount=" << receive_controller.videoFrameCount() << '\n';
        out << "decodeAttemptCount=" << receive_controller.decodeAttemptCount() << '\n';
        out << "decodeFailureCount=" << receive_controller.decodeFailureCount() << '\n';
        out << "decodedQrFrameCount=" << receive_controller.decodedQrFrameCount() << '\n';
        out << "acceptedQrFrameCount=" << receive_controller.acceptedQrFrameCount() << '\n';
        out << "rejectedQrFrameCount=" << receive_controller.rejectedQrFrameCount() << '\n';
        out << "lastError=" << receive_controller.lastError() << '\n';
        out << "lastSavedPath=" << receive_controller.lastSavedPath() << '\n';
        out << "receivedTextAvailable=" << (receive_controller.receivedTextAvailable() ? "true" : "false") << '\n';
        out << "receivedTextBase64=" << receive_controller.receivedText().toUtf8().toBase64() << '\n';
        out << "selectedCameraIndex=" << receive_controller.selectedCameraIndex() << '\n';
        out << "selectedCameraName=" << receive_controller.selectedCameraName() << '\n';
        out << "availableCameraNames=" << receive_controller.availableCameraNames().join(QStringLiteral("|")) << '\n';
        out << "speedMode=" << receive_controller.speedMode() << '\n';
        out << "speedModeName=" << receive_controller.speedModeName() << '\n';
    };
    if (!status_file.isEmpty()) {
        QObject::connect(
            &receive_controller,
            &ReceiveController::stateChanged,
            &receive_controller,
            write_status_file);
        write_status_file();
    }

    QObject::connect(
        &engine,
        &QQmlApplicationEngine::objectCreationFailed,
        &app,
        []() { QCoreApplication::exit(-1); },
        Qt::QueuedConnection);

    if (parser.isSet(QStringLiteral("send-file"))) {
        send_controller.prepareFile(QUrl::fromLocalFile(parser.value(QStringLiteral("send-file"))));
        if (parser.isSet(QStringLiteral("play"))) {
            send_controller.startPlayback();
        }
    } else if (parser.isSet(QStringLiteral("send-text"))) {
        send_controller.prepareText(parser.value(QStringLiteral("send-text")));
        if (parser.isSet(QStringLiteral("play"))) {
            send_controller.startPlayback();
        }
    }

    engine.loadFromModule(QStringLiteral("AirGapQrTransfer"), QStringLiteral("Main"));

    return QGuiApplication::exec();
}
