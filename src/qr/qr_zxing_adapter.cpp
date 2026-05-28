#include "qr_zxing_adapter.hpp"

#if AIRGAP_HAS_ZXING
#include <ZXing/Barcode.h>
#include <ZXing/BarcodeFormat.h>
#include <ZXing/ImageView.h>
#include <ZXing/ReadBarcode.h>
#include <ZXing/ReaderOptions.h>
#endif

#include <exception>
#include <limits>
#include <utility>

namespace aqrt::qr {

DecodeResult ZxingQrDecoderAdapter::decode(const QrRasterImage& image) const
{
    if (!is_valid_raster_image_shape(image)) {
        return {QrError::InvalidImage, {}, "raster image is empty or has inconsistent dimensions"};
    }

    if (image.width > static_cast<std::uint32_t>(std::numeric_limits<int>::max())
        || image.height > static_cast<std::uint32_t>(std::numeric_limits<int>::max())) {
        return {QrError::InvalidImage, {}, "raster image dimensions exceed decoder limits"};
    }

#if AIRGAP_HAS_ZXING
    try {
        const ZXing::ImageView view(
            image.luminance.data(),
            static_cast<int>(image.width),
            static_cast<int>(image.height),
            ZXing::ImageFormat::Lum);

        auto decode_with_mode = [&view](bool pure_image) {
            ZXing::ReaderOptions options;
            options.setFormats(ZXing::BarcodeFormat::QRCode);
            options.setIsPure(pure_image);
            options.setTryHarder(true);
            return ZXing::ReadBarcode(view, options);
        };

        auto barcode = decode_with_mode(false);
        if (!barcode.isValid()) {
            barcode = decode_with_mode(true);
        }
        if (!barcode.isValid()) {
            return {QrError::DecodeFailed, {}, "ZXing did not find a valid QR code"};
        }

        const auto& decoded_bytes = barcode.bytes();
        if (decoded_bytes.empty()) {
            return {QrError::DecodeFailed, {}, "ZXing decoded an empty payload"};
        }

        Bytes payload(decoded_bytes.begin(), decoded_bytes.end());
        return {QrError::None, std::move(payload), {}};
    } catch (const std::exception& error) {
        return {QrError::DecodeFailed, {}, error.what()};
    }
#else
    (void)image;
    return {QrError::DecodeFailed, {}, "ZXing support is not enabled in this build"};
#endif
}

} // namespace aqrt::qr
