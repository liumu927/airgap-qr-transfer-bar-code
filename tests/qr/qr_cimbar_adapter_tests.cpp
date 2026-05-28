#include "qr_cimbar_adapter.hpp"

#include <cstdlib>
#include <iostream>
#include <string_view>

namespace {

void require(bool condition, std::string_view message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

} // namespace

void run_cimbar_adapter_tests()
{
    if (!aqrt::qr::cimbar_backend_available()) {
        return;
    }

    const std::string text = "airgap cimbar loopback test";
    const aqrt::core::Bytes bytes(text.begin(), text.end());
    const aqrt::qr::CimbarFileEncoder encoder;
    const auto encoded = encoder.encode_bytes("loopback.txt", bytes);
    require(encoded.ok(), encoded.message);
    require(!encoded.frames.empty(), "cimbar encoder should produce at least one frame");

    aqrt::qr::CimbarFileDecoder decoder;
    bool completed = false;
    aqrt::qr::CimbarDecodeResult decoded;
    for (int round = 0; round < 8 && !completed; ++round) {
        for (const auto& frame : encoded.frames) {
            decoded = decoder.decode_rgb(frame);
            require(decoded.ok(), decoded.message);
            if (decoded.completed) {
                completed = true;
                break;
            }
        }
    }

    require(completed, "cimbar decoder should complete from generated frames");
    require(decoded.file_bytes == bytes, "cimbar loopback bytes should match");
}
