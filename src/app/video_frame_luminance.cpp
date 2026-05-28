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

std::uint8_t clamp_rgb(int value)
{
    return static_cast<std::uint8_t>(std::clamp(value, 0, 255));
}

void set_rgb_from_yuv(qr::RgbImage& image, std::size_t offset, int y, int u, int v)
{
    const int c = y - 16;
    const int d = u - 128;
    const int e = v - 128;
    image.rgb[offset] = clamp_rgb((298 * c + 409 * e + 128) >> 8);
    image.rgb[offset + 1U] = clamp_rgb((298 * c - 100 * d - 208 * e + 128) >> 8);
    image.rgb[offset + 2U] = clamp_rgb((298 * c + 516 * d + 128) >> 8);
}

bool copy_packed_rgb(const QVideoFrame& frame, qr::RgbImage& image)
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

    image.width = static_cast<std::uint32_t>(width);
    image.height = static_cast<std::uint32_t>(height);
    image.rgb.resize(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 3U);

    const auto* source = frame.bits(0);
    const bool bottom_to_top = frame.surfaceFormat().scanLineDirection() == QVideoFrameFormat::BottomToTop;
    for (int y = 0; y < height; ++y) {
        const int source_y = bottom_to_top ? (height - 1 - y) : y;
        const auto* row = source + static_cast<std::size_t>(source_y) * static_cast<std::size_t>(stride);
        const auto output_offset = static_cast<std::size_t>(y) * static_cast<std::size_t>(width) * 3U;
        for (int x = 0; x < width; ++x) {
            const auto* pixel = row + static_cast<std::size_t>(x) * static_cast<std::size_t>(bytes_per_pixel);
            const auto rgb_offset = output_offset + static_cast<std::size_t>(x) * 3U;
            switch (layout) {
            case Layout::Rgba32:
                image.rgb[rgb_offset] = pixel[0];
                image.rgb[rgb_offset + 1U] = pixel[1];
                image.rgb[rgb_offset + 2U] = pixel[2];
                break;
            case Layout::Bgra32:
                image.rgb[rgb_offset] = pixel[2];
                image.rgb[rgb_offset + 1U] = pixel[1];
                image.rgb[rgb_offset + 2U] = pixel[0];
                break;
            }
        }
    }
    return true;
}

bool copy_yuv420_planar(const QVideoFrame& frame, qr::RgbImage& image, bool yv12)
{
    if (frame.planeCount() < 3 || frame.bits(0) == nullptr || frame.bits(1) == nullptr
        || frame.bits(2) == nullptr) {
        return false;
    }

    const int width = frame.width();
    const int height = frame.height();
    const int y_stride = frame.bytesPerLine(0);
    const int first_chroma_stride = frame.bytesPerLine(1);
    const int second_chroma_stride = frame.bytesPerLine(2);
    if (width <= 0 || height <= 0 || y_stride < width || first_chroma_stride < (width + 1) / 2
        || second_chroma_stride < (width + 1) / 2) {
        return false;
    }

    const auto* y_plane = frame.bits(0);
    const auto* first_chroma = frame.bits(1);
    const auto* second_chroma = frame.bits(2);
    const auto* u_plane = yv12 ? second_chroma : first_chroma;
    const auto* v_plane = yv12 ? first_chroma : second_chroma;
    const int u_stride = yv12 ? second_chroma_stride : first_chroma_stride;
    const int v_stride = yv12 ? first_chroma_stride : second_chroma_stride;

    image.width = static_cast<std::uint32_t>(width);
    image.height = static_cast<std::uint32_t>(height);
    image.rgb.resize(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 3U);

    for (int y = 0; y < height; ++y) {
        const auto* y_row = y_plane + static_cast<std::size_t>(y) * static_cast<std::size_t>(y_stride);
        const auto* u_row = u_plane + static_cast<std::size_t>(y / 2) * static_cast<std::size_t>(u_stride);
        const auto* v_row = v_plane + static_cast<std::size_t>(y / 2) * static_cast<std::size_t>(v_stride);
        for (int x = 0; x < width; ++x) {
            const auto rgb_offset = (static_cast<std::size_t>(y) * static_cast<std::size_t>(width)
                                        + static_cast<std::size_t>(x))
                * 3U;
            set_rgb_from_yuv(image, rgb_offset, y_row[x], u_row[x / 2], v_row[x / 2]);
        }
    }
    return true;
}

bool copy_yuv420_semiplanar(const QVideoFrame& frame, qr::RgbImage& image, bool nv21)
{
    if (frame.planeCount() < 2 || frame.bits(0) == nullptr || frame.bits(1) == nullptr) {
        return false;
    }

    const int width = frame.width();
    const int height = frame.height();
    const int y_stride = frame.bytesPerLine(0);
    const int uv_stride = frame.bytesPerLine(1);
    if (width <= 0 || height <= 0 || y_stride < width || uv_stride < width) {
        return false;
    }

    const auto* y_plane = frame.bits(0);
    const auto* uv_plane = frame.bits(1);
    image.width = static_cast<std::uint32_t>(width);
    image.height = static_cast<std::uint32_t>(height);
    image.rgb.resize(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 3U);

    for (int y = 0; y < height; ++y) {
        const auto* y_row = y_plane + static_cast<std::size_t>(y) * static_cast<std::size_t>(y_stride);
        const auto* uv_row = uv_plane + static_cast<std::size_t>(y / 2) * static_cast<std::size_t>(uv_stride);
        for (int x = 0; x < width; ++x) {
            const auto* uv = uv_row + static_cast<std::size_t>(x / 2) * 2U;
            const int u = nv21 ? uv[1] : uv[0];
            const int v = nv21 ? uv[0] : uv[1];
            const auto rgb_offset = (static_cast<std::size_t>(y) * static_cast<std::size_t>(width)
                                        + static_cast<std::size_t>(x))
                * 3U;
            set_rgb_from_yuv(image, rgb_offset, y_row[x], u, v);
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

qr::RgbImage video_frame_to_rgb(QVideoFrame frame)
{
    if (!frame.isValid() || frame.width() <= 0 || frame.height() <= 0) {
        return {};
    }

    if (!frame.map(QVideoFrame::ReadOnly)) {
        return {};
    }
    const FrameUnmapGuard unmap(frame);

    qr::RgbImage image;
    if (copy_packed_rgb(frame, image)) {
        return image;
    }

    switch (frame.pixelFormat()) {
    case QVideoFrameFormat::Format_YUV420P:
        if (copy_yuv420_planar(frame, image, false)) {
            return image;
        }
        break;
    case QVideoFrameFormat::Format_YV12:
        if (copy_yuv420_planar(frame, image, true)) {
            return image;
        }
        break;
    case QVideoFrameFormat::Format_NV12:
        if (copy_yuv420_semiplanar(frame, image, false)) {
            return image;
        }
        break;
    case QVideoFrameFormat::Format_NV21:
        if (copy_yuv420_semiplanar(frame, image, true)) {
            return image;
        }
        break;
    default:
        break;
    }

    return {};
}

} // namespace aqrt::app
