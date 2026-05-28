#pragma once

#include "qr_adapter.hpp"

#include <cstddef>

namespace aqrt::qr {

class LibQrEncodeAdapter final : public IQrEncoder {
public:
    explicit LibQrEncodeAdapter(std::size_t max_payload_size = 2048);

    EncodeResult encode(const Bytes& payload) const override;
    [[nodiscard]] std::size_t max_payload_size() const override;

private:
    std::size_t max_payload_size_ = 0;
};

} // namespace aqrt::qr
