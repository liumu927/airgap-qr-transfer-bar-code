#pragma once

#include "frame.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace aqrt::qr {

using aqrt::core::Bytes;

struct QrImage {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::vector<bool> modules;
};

struct QrRasterImage {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::vector<std::uint8_t> luminance;
};

enum class QrError {
    None,
    EmptyPayload,
    PayloadTooLarge,
    InvalidImage,
    EncodeFailed,
    DecodeFailed,
};

struct EncodeResult {
    QrError error = QrError::None;
    QrImage image{};
    std::string message;

    [[nodiscard]] bool ok() const { return error == QrError::None; }
};

struct DecodeResult {
    QrError error = QrError::None;
    Bytes payload;
    std::string message;

    [[nodiscard]] bool ok() const { return error == QrError::None; }
};

class IQrEncoder {
public:
    virtual ~IQrEncoder() = default;
    virtual EncodeResult encode(const Bytes& payload) const = 0;
    [[nodiscard]] virtual std::size_t max_payload_size() const = 0;
};

class IQrDecoder {
public:
    virtual ~IQrDecoder() = default;
    virtual DecodeResult decode(const QrImage& image) const = 0;
};

class IQrRasterDecoder {
public:
    virtual ~IQrRasterDecoder() = default;
    virtual DecodeResult decode(const QrRasterImage& image) const = 0;
};

const char* qr_error_name(QrError error);
bool is_valid_image_shape(const QrImage& image);
bool is_valid_raster_image_shape(const QrRasterImage& image);
QrRasterImage render_qr_to_luminance(
    const QrImage& image,
    std::uint32_t pixels_per_module = 8,
    std::uint32_t quiet_zone_modules = 4);

} // namespace aqrt::qr
