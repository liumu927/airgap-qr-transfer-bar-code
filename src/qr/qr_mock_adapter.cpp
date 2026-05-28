#include "qr_mock_adapter.hpp"

#include <algorithm>
#include <array>

namespace aqrt::qr {
namespace {

constexpr std::uint32_t kMockImageWidth = 64;
constexpr std::uint32_t kBitsPerByte = 8;
constexpr std::uint32_t kLengthHeaderBytes = 4;
constexpr std::uint32_t kMockMagicBytes = 4;
constexpr std::array<std::uint8_t, kMockMagicBytes> kMockMagic{'M', 'Q', 'R', '1'};

void set_bit(QrImage& image, std::size_t bit_index, bool value)
{
    image.modules[bit_index] = value;
}

bool get_bit(const QrImage& image, std::size_t bit_index)
{
    return image.modules[bit_index];
}

void write_byte(QrImage& image, std::size_t& bit_index, std::uint8_t value)
{
    for (std::uint32_t bit = 0; bit < kBitsPerByte; ++bit) {
        set_bit(image, bit_index++, ((value >> bit) & 1U) != 0U);
    }
}

std::uint8_t read_byte(const QrImage& image, std::size_t& bit_index)
{
    std::uint8_t value = 0;
    for (std::uint32_t bit = 0; bit < kBitsPerByte; ++bit) {
        if (get_bit(image, bit_index++)) {
            value |= static_cast<std::uint8_t>(1U << bit);
        }
    }
    return value;
}

void write_u32_le(QrImage& image, std::size_t& bit_index, std::uint32_t value)
{
    for (int shift = 0; shift < 32; shift += 8) {
        write_byte(image, bit_index, static_cast<std::uint8_t>((value >> shift) & 0xFFU));
    }
}

std::uint32_t read_u32_le(const QrImage& image, std::size_t& bit_index)
{
    std::uint32_t value = 0;
    for (int shift = 0; shift < 32; shift += 8) {
        value |= static_cast<std::uint32_t>(read_byte(image, bit_index)) << shift;
    }
    return value;
}

} // namespace

MockQrAdapter::MockQrAdapter(std::size_t max_payload_size)
    : max_payload_size_(max_payload_size)
{
}

EncodeResult MockQrAdapter::encode(const Bytes& payload) const
{
    if (payload.empty()) {
        return {QrError::EmptyPayload, {}, "payload is empty"};
    }
    if (payload.size() > max_payload_size_) {
        return {QrError::PayloadTooLarge, {}, "payload exceeds mock QR capacity"};
    }

    QrImage image;
    image.width = kMockImageWidth;
    image.height = kMockImageWidth;
    image.modules.assign(static_cast<std::size_t>(image.width) * image.height, false);

    std::size_t bit_index = 0;
    for (const auto byte : kMockMagic) {
        write_byte(image, bit_index, byte);
    }
    write_u32_le(image, bit_index, static_cast<std::uint32_t>(payload.size()));
    for (const auto byte : payload) {
        write_byte(image, bit_index, byte);
    }

    return {QrError::None, std::move(image), {}};
}

DecodeResult MockQrAdapter::decode(const QrImage& image) const
{
    if (!is_valid_image_shape(image)) {
        return {QrError::InvalidImage, {}, "image is not a valid square module grid"};
    }

    constexpr auto min_bits = (kMockMagicBytes + kLengthHeaderBytes) * kBitsPerByte;
    if (image.modules.size() < min_bits) {
        return {QrError::DecodeFailed, {}, "image is too small for mock payload header"};
    }

    std::size_t bit_index = 0;
    for (const auto expected : kMockMagic) {
        if (read_byte(image, bit_index) != expected) {
            return {QrError::DecodeFailed, {}, "mock QR magic does not match"};
        }
    }

    const auto payload_size = read_u32_le(image, bit_index);
    if (payload_size == 0 || payload_size > max_payload_size_) {
        return {QrError::DecodeFailed, {}, "mock QR payload length is invalid"};
    }
    if (bit_index + static_cast<std::size_t>(payload_size) * kBitsPerByte > image.modules.size()) {
        return {QrError::DecodeFailed, {}, "mock QR payload length exceeds image capacity"};
    }

    Bytes payload;
    payload.reserve(payload_size);
    for (std::uint32_t i = 0; i < payload_size; ++i) {
        payload.push_back(read_byte(image, bit_index));
    }

    return {QrError::None, std::move(payload), {}};
}

std::size_t MockQrAdapter::max_payload_size() const
{
    return max_payload_size_;
}

} // namespace aqrt::qr
