#pragma once

#include "qr_adapter.hpp"

#include <QImage>
#include <QMutex>
#include <QQuickImageProvider>

class QrImageProvider final : public QQuickImageProvider {
public:
    QrImageProvider();

    QImage requestImage(const QString& id, QSize* size, const QSize& requestedSize) override;
    void setImage(const aqrt::qr::QrImage& image);
    void setImage(const aqrt::qr::RgbImage& image);
    void clear();

private:
    QImage current_image_;
    QMutex mutex_;
};
