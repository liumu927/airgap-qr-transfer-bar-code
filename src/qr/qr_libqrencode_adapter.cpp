#include "qr_libqrencode_adapter.hpp"

#if AIRGAP_HAS_LIBQRENCODE
#include <qrencode.h>
#endif

#include <utility>

namespace aqrt::qr {

LibQrEncodeAdapter::LibQrEncodeAdapter(std::size_t max_payload_size)
    : max_payload_size_(max_payload_size)
{
}

EncodeResult LibQrEncodeAdapter::encode(const Bytes& payload) const
{
    if (payload.empty()) {
        return {QrError::EmptyPayload, {}, "payload is empty"};
    }
    if (payload.size() > max_payload_size_) {
        return {QrError::PayloadTooLarge, {}, "payload exceeds configured QR encoder capacity"};
    }

#if AIRGAP_HAS_LIBQRENCODE
    auto* code = QRcode_encodeData(
        static_cast<int>(payload.size()),
        payload.data(),
        0,
        QR_ECLEVEL_Q);

    if (code == nullptr) {
        return {QrError::EncodeFailed, {}, "libqrencode failed to encode payload"};
    }
    if (code->width <= 0 || code->data == nullptr) {
        QRcode_free(code);
        return {QrError::EncodeFailed, {}, "libqrencode returned an invalid QR symbol"};
    }

    QrImage image;
    image.width = static_cast<std::uint32_t>(code->width);
    image.height = static_cast<std::uint32_t>(code->width);
    image.modules.assign(static_cast<std::size_t>(image.width) * image.height, false);

    for (std::uint32_t y = 0; y < image.height; ++y) {
        for (std::uint32_t x = 0; x < image.width; ++x) {
            const auto index = static_cast<std::size_t>(y) * image.width + x;
            image.modules[index] = (code->data[index] & 0x01U) != 0U;
        }
    }

    QRcode_free(code);
    return {QrError::None, std::move(image), {}};
#else
    (void)payload;
    return {QrError::EncodeFailed, {}, "libqrencode support is not enabled in this build"};
#endif
}

std::size_t LibQrEncodeAdapter::max_payload_size() const
{
    return max_payload_size_;
}

} // namespace aqrt::qr
