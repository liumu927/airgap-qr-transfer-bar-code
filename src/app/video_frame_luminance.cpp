#include "video_frame_luminance.hpp"

#include <QSize>
#include <QVideoFrameFormat>

#include <algorithm>
#include <cstdint>

namespace aqrt::app {
namespace {

class FrameUnmapGuard final {
public:
    explicit FrameUnmapGuard(QVideoFrame& frame)
        : frame_(frame)
    {
    }

    ~FrameUnmapGuard()
    {
        if (frame_.isMapped()) {
            frame_.unmap();
        }
    }

    FrameUnmapGuard(const FrameUnmapGuard&) = delete;
    FrameUnmapGuard& operator=(const FrameUnmapGuard&) = delete;

private:
    QVideoFrame& frame_;
};

bool copy_luma_plane(const QVideoFrame& frame, qr::QrRasterImage& raster)
{
    if (frame.planeCount() <= 0 || frame.bits(0) == nullptr) {
        return false;
    }

    const int width = frame.width();
    const int height = frame.height();
    const int stride = frame.bytesPerLine(0);
    if (width <= 0 || height <= 0 || stride < width) {
        return false;
    }

    raster.width = static_cast<std::uint32_t>(width);
    raster.height = static_cast<std::uint32_t>(height);
    raster.luminance.resize(static_cast<std::size_t>(width) * static_cast<std::size_t>(height));

    const auto* source = frame.bits(0);
    for (int y = 0; y < height; ++y) {
        const auto* row = source + static_cast<std::size_t>(y) * static_cast<std::size_t>(stride);
        const auto offset = static_cast<std::size_t>(y) * static_cast<std::size_t>(width);
        std::copy(row, row + width, raster.luminance.begin() + static_cast<std::ptrdiff_t>(offset));
    }
    return true;
}

bool is_luma_first_format(QVideoFrameFormat::PixelFormat format)
{
    switch (format) {
    case QVideoFrameFormat::Format_Y8:
    case QVideoFrameFormat::Format_Y16:
    case QVideoFrameFormat::Format_YUV420P:
    case QVideoFrameFormat::Format_YUV422P:
    case QVideoFrameFormat::Format_YV12:
    case QVideoFrameFormat::Format_NV12:
    case QVideoFrameFormat::Format_NV21:
    case QVideoFrameFormat::Format_IMC1:
    case QVideoFrameFormat::Format_IMC2:
    case QVideoFrameFormat::Format_IMC3:
    case QVideoFrameFormat::Format_IMC4:
    case QVideoFrameFormat::Format_P010:
    case QVideoFrameFormat::Format_P016:
    case QVideoFrameFormat::Format_YUV420P10:
        return true;
    default:
        return false;
    }
}

bool copy_packed_rgb(const QVideoFrame& frame, qr::QrRasterImage& raster)
{
    if (frame.planeCount() <= 0 || frame.bits(0) == nullptr) {
        return false;
    }

    const int width = frame.width();
    const int height = frame.height();
    const int stride = frame.bytesPerLine(0);
    if (width <= 0 || height <= 0 || stride <= 0) {
        return false;
    }

    enum class Layout {
        Rgba32,
        Bgra32,
    };

    Layout layout{};
    int bytes_per_pixel = 0;
    switch (frame.pixelFormat()) {
    case QVideoFrameFormat::Format_XRGB8888:
    case QVideoFrameFormat::Format_ARGB8888:
    case QVideoFrameFormat::Format_ARGB8888_Premultiplied:
    case QVideoFrameFormat::Format_RGBA8888:
    case QVideoFrameFormat::Format_RGBX8888:
        layout = Layout::Rgba32;
        bytes_per_pixel = 4;
        break;
    case QVideoFrameFormat::Format_XBGR8888:
    case QVideoFrameFormat::Format_ABGR8888:
    case QVideoFrameFormat::Format_BGRA8888:
    case QVideoFrameFormat::Format_BGRA8888_Premultiplied:
    case QVideoFrameFormat::Format_BGRX8888:
        layout = Layout::Bgra32;
        bytes_per_pixel = 4;
        break;
    default:
        return false;
    }

    if (stride < width * bytes_per_pixel) {
        return false;
    }

    raster.width = static_cast<std::uint32_t>(width);
    raster.height = static_cast<std::uint32_t>(height);
    raster.luminance.resize(static_cast<std::size_t>(width) * static_cast<std::size_t>(height));

    const auto* source = frame.bits(0);
    const bool bottom_to_top = frame.surfaceFormat().scanLineDirection() == QVideoFrameFormat::BottomToTop;
    for (int y = 0; y < height; ++y) {
        const int source_y = bottom_to_top ? (height - 1 - y) : y;
        const auto* row = source + static_cast<std::size_t>(source_y) * static_cast<std::size_t>(stride);
        const auto output_offset = static_cast<std::size_t>(y) * static_cast<std::size_t>(width);
        for (int x = 0; x < width; ++x) {
            const auto* pixel = row + static_cast<std::size_t>(x) * static_cast<std::size_t>(bytes_per_pixel);
            std::uint8_t r = 0;
            std::uint8_t g = 0;
            std::uint8_t b = 0;
            switch (layout) {
            case Layout::Rgba32:
                r = pixel[0];
                g = pixel[1];
                b = pixel[2];
                break;
            case Layout::Bgra32:
                b = pixel[0];
                g = pixel[1];
                r = pixel[2];
                break;
            }
            raster.luminance[output_offset + static_cast<std::size_t>(x)] =
                static_cast<std::uint8_t>((77U * r + 150U * g + 29U * b) >> 8U);
        }
    }
    return true;
}

} // namespace

qr::QrRasterImage video_frame_to_luminance(QVideoFrame frame)
{
    if (!frame.isValid() || frame.width() <= 0 || frame.height() <= 0) {
        return {};
    }

    if (!frame.map(QVideoFrame::ReadOnly)) {
        return {};
    }
    const FrameUnmapGuard unmap(frame);

    const auto pixel_format = frame.pixelFormat();
    if (is_luma_first_format(pixel_format)) {
        qr::QrRasterImage raster;
        if (copy_luma_plane(frame, raster)) {
            return raster;
        }
        return {};
    }

    qr::QrRasterImage raster;
    if (copy_packed_rgb(frame, raster)) {
        return raster;
    }

    return {};
}

} // namespace aqrt::app
