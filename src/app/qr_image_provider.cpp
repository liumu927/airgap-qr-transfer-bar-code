#include "qr_image_provider.hpp"

#include <QMutexLocker>
#include <QSize>
#include <QString>

namespace {

QImage empty_image()
{
    QImage image(1, 1, QImage::Format_RGB32);
    image.fill(Qt::white);
    return image;
}

QImage render_qr_image(const aqrt::qr::QrImage& image)
{
    const auto raster = aqrt::qr::render_qr_to_luminance(image, 4, 4);
    if (!aqrt::qr::is_valid_raster_image_shape(raster)) {
        return empty_image();
    }

    QImage rendered(
        static_cast<int>(raster.width),
        static_cast<int>(raster.height),
        QImage::Format_RGB32);
    for (int y = 0; y < rendered.height(); ++y) {
        auto* line = reinterpret_cast<QRgb*>(rendered.scanLine(y));
        const auto row_offset = static_cast<std::size_t>(y) * raster.width;
        for (int x = 0; x < rendered.width(); ++x) {
            const auto luminance = raster.luminance[row_offset + static_cast<std::size_t>(x)];
            line[x] = qRgb(luminance, luminance, luminance);
        }
    }
    return rendered;
}

QImage render_rgb_image(const aqrt::qr::RgbImage& image)
{
    if (!aqrt::qr::is_valid_rgb_image_shape(image)) {
        return empty_image();
    }

    QImage rendered(
        static_cast<int>(image.width),
        static_cast<int>(image.height),
        QImage::Format_RGB32);
    for (int y = 0; y < rendered.height(); ++y) {
        auto* line = reinterpret_cast<QRgb*>(rendered.scanLine(y));
        const auto row_offset = static_cast<std::size_t>(y) * image.width * 3U;
        for (int x = 0; x < rendered.width(); ++x) {
            const auto pixel_offset = row_offset + static_cast<std::size_t>(x) * 3U;
            line[x] = qRgb(
                image.rgb[pixel_offset],
                image.rgb[pixel_offset + 1U],
                image.rgb[pixel_offset + 2U]);
        }
    }
    return rendered;
}

} // namespace

QrImageProvider::QrImageProvider()
    : QQuickImageProvider(QQuickImageProvider::Image)
{
}

QImage QrImageProvider::requestImage(const QString& id, QSize* size, const QSize& requestedSize)
{
    Q_UNUSED(id)
    Q_UNUSED(requestedSize)

    QMutexLocker locker(&mutex_);
    QImage image = current_image_.isNull() ? empty_image() : current_image_;
    locker.unlock();

    if (size != nullptr) {
        *size = image.size();
    }
    return image;
}

void QrImageProvider::setImage(const aqrt::qr::QrImage& image)
{
    QMutexLocker locker(&mutex_);
    current_image_ = render_qr_image(image);
}

void QrImageProvider::setImage(const aqrt::qr::RgbImage& image)
{
    QMutexLocker locker(&mutex_);
    current_image_ = render_rgb_image(image);
}

void QrImageProvider::clear()
{
    QMutexLocker locker(&mutex_);
    current_image_ = {};
}
