#include "qr_adapter.hpp"

#include <limits>

namespace aqrt::qr {

const char* qr_error_name(QrError error)
{
    switch (error) {
    case QrError::None:
        return "None";
    case QrError::EmptyPayload:
        return "EmptyPayload";
    case QrError::PayloadTooLarge:
        return "PayloadTooLarge";
    case QrError::InvalidImage:
        return "InvalidImage";
    case QrError::EncodeFailed:
        return "EncodeFailed";
    case QrError::DecodeFailed:
        return "DecodeFailed";
    }

    return "Unknown";
}

bool is_valid_image_shape(const QrImage& image)
{
    if (image.width == 0 || image.height == 0 || image.width != image.height) {
        return false;
    }
    return image.modules.size() == static_cast<std::size_t>(image.width) * image.height;
}

bool is_valid_raster_image_shape(const QrRasterImage& image)
{
    if (image.width == 0 || image.height == 0) {
        return false;
    }
    return image.luminance.size() == static_cast<std::size_t>(image.width) * image.height;
}

bool is_valid_rgb_image_shape(const RgbImage& image)
{
    if (image.width == 0 || image.height == 0) {
        return false;
    }
    constexpr std::size_t kChannels = 3;
    return image.rgb.size() == static_cast<std::size_t>(image.width) * image.height * kChannels;
}

QrRasterImage render_qr_to_luminance(
    const QrImage& image,
    std::uint32_t pixels_per_module,
    std::uint32_t quiet_zone_modules)
{
    if (!is_valid_image_shape(image) || pixels_per_module == 0) {
        return {};
    }

    const auto module_width = static_cast<std::uint64_t>(image.width) + (2ULL * quiet_zone_modules);
    const auto pixel_width = module_width * pixels_per_module;
    if (pixel_width == 0 || pixel_width > std::numeric_limits<std::uint32_t>::max()) {
        return {};
    }

    QrRasterImage raster;
    raster.width = static_cast<std::uint32_t>(pixel_width);
    raster.height = raster.width;
    raster.luminance.assign(static_cast<std::size_t>(raster.width) * raster.height, 0xffU);

    for (std::uint32_t module_y = 0; module_y < image.height; ++module_y) {
        for (std::uint32_t module_x = 0; module_x < image.width; ++module_x) {
            const auto module_index = static_cast<std::size_t>(module_y) * image.width + module_x;
            if (!image.modules[module_index]) {
                continue;
            }

            const auto start_x = (module_x + quiet_zone_modules) * pixels_per_module;
            const auto start_y = (module_y + quiet_zone_modules) * pixels_per_module;
            for (std::uint32_t dy = 0; dy < pixels_per_module; ++dy) {
                const auto row = static_cast<std::size_t>(start_y + dy) * raster.width;
                for (std::uint32_t dx = 0; dx < pixels_per_module; ++dx) {
                    raster.luminance[row + start_x + dx] = 0x00U;
                }
            }
        }
    }

    return raster;
}

} // namespace aqrt::qr
