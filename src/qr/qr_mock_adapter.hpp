#pragma once

#include "qr_adapter.hpp"

#include <cstddef>

namespace aqrt::qr {

class MockQrAdapter final : public IQrEncoder, public IQrDecoder {
public:
    explicit MockQrAdapter(std::size_t max_payload_size = 512);

    EncodeResult encode(const Bytes& payload) const override;
    DecodeResult decode(const QrImage& image) const override;
    [[nodiscard]] std::size_t max_payload_size() const override;

private:
    std::size_t max_payload_size_ = 0;
};

} // namespace aqrt::qr
