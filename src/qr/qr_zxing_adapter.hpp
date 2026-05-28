#pragma once

#include "qr_adapter.hpp"

namespace aqrt::qr {

class ZxingQrDecoderAdapter final : public IQrRasterDecoder {
public:
    DecodeResult decode(const QrRasterImage& image) const override;
};

} // namespace aqrt::qr
