#include "chunker.hpp"
#include "frame.hpp"
#include "qr_libqrencode_adapter.hpp"
#include "qr_mock_adapter.hpp"
#include "qr_zxing_adapter.hpp"
#include "session.hpp"

#include <cstdlib>
#include <iostream>
#include <string_view>

namespace {

using namespace aqrt::core;
using namespace aqrt::qr;

void require(bool condition, std::string_view message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

Bytes sample_payload()
{
    const Bytes file{'q', 'r', '-', 'p', 'a', 'y', 'l', 'o', 'a', 'd'};
    const auto session_id = generate_session_id();
    const auto manifest = build_manifest(session_id, "sample.txt", file, 4).manifest;
    return encode_frame(Frame{manifest});
}

void test_mock_adapter_round_trips_protocol_payload()
{
    const auto payload = sample_payload();
    const MockQrAdapter adapter(512);

    const auto encoded = adapter.encode(payload);
    require(encoded.ok(), encoded.message);
    require(is_valid_image_shape(encoded.image), "encoded mock image should be square and complete");

    const auto decoded = adapter.decode(encoded.image);
    require(decoded.ok(), decoded.message);
    require(decoded.payload == payload, "decoded payload should equal original protocol frame");

    const auto parsed_frame = decode_frame(decoded.payload);
    require(parsed_frame.ok(), parsed_frame.message);
    require(std::holds_alternative<ManifestFrame>(parsed_frame.frame), "decoded protocol frame should be manifest");
}

void test_mock_adapter_rejects_empty_payload()
{
    const MockQrAdapter adapter(512);
    const auto encoded = adapter.encode(Bytes{});
    require(encoded.error == QrError::EmptyPayload, "empty payload should be rejected");
}

void test_mock_adapter_rejects_oversized_payload()
{
    const MockQrAdapter adapter(8);
    const Bytes payload(9, 0x42);
    const auto encoded = adapter.encode(payload);
    require(encoded.error == QrError::PayloadTooLarge, "oversized payload should be rejected");
}

void test_mock_decoder_rejects_invalid_image_shape()
{
    const MockQrAdapter adapter(512);
    QrImage image;
    image.width = 2;
    image.height = 3;
    image.modules.assign(6, false);

    const auto decoded = adapter.decode(image);
    require(decoded.error == QrError::InvalidImage, "non-square image should be rejected");
}

void test_mock_decoder_rejects_corrupt_magic()
{
    const MockQrAdapter adapter(512);
    auto image = adapter.encode(sample_payload()).image;
    image.modules[0] = !image.modules[0];

    const auto decoded = adapter.decode(image);
    require(decoded.error == QrError::DecodeFailed, "corrupt mock magic should fail decode");
}

void test_qr_render_to_luminance_adds_quiet_zone()
{
    QrImage image;
    image.width = 2;
    image.height = 2;
    image.modules = {true, false, false, true};

    const auto raster = render_qr_to_luminance(image, 2, 1);
    require(is_valid_raster_image_shape(raster), "rendered raster should have valid dimensions");
    require(raster.width == 8 && raster.height == 8, "rendered raster should include quiet zone and scale");
    require(raster.luminance[0] == 0xffU, "quiet zone should remain white");

    const auto first_dark_pixel = static_cast<std::size_t>(2) * raster.width + 2;
    const auto second_dark_pixel = static_cast<std::size_t>(4) * raster.width + 4;
    require(raster.luminance[first_dark_pixel] == 0x00U, "dark module should render as black");
    require(raster.luminance[second_dark_pixel] == 0x00U, "second dark module should render as black");
}

void test_qr_render_to_luminance_rejects_invalid_inputs()
{
    QrImage image;
    image.width = 1;
    image.height = 1;
    image.modules = {true};

    require(!is_valid_raster_image_shape(render_qr_to_luminance({}, 4, 4)), "invalid module image should not render");
    require(!is_valid_raster_image_shape(render_qr_to_luminance(image, 0, 4)), "zero scale should not render");
}

void test_libqrencode_encoder_produces_module_grid_when_available()
{
#if AIRGAP_HAS_LIBQRENCODE
    const auto payload = sample_payload();
    const LibQrEncodeAdapter encoder(512);

    const auto encoded = encoder.encode(payload);
    require(encoded.ok(), encoded.message);
    require(is_valid_image_shape(encoded.image), "libqrencode image should be square and complete");
    require(encoded.image.width >= 21, "QR version 1 or higher should have at least 21 modules");
#endif
}

void test_libqrencode_and_zxing_round_trip_protocol_payload_when_available()
{
#if AIRGAP_HAS_LIBQRENCODE && AIRGAP_HAS_ZXING
    const auto payload = sample_payload();
    const LibQrEncodeAdapter encoder(512);
    const ZxingQrDecoderAdapter decoder;

    const auto encoded = encoder.encode(payload);
    require(encoded.ok(), encoded.message);

    const auto raster = render_qr_to_luminance(encoded.image, 8, 4);
    require(is_valid_raster_image_shape(raster), "libqrencode image should render to a valid raster");

    const auto decoded = decoder.decode(raster);
    require(decoded.ok(), decoded.message);
    require(decoded.payload == payload, "ZXing decoded payload should equal original protocol frame");

    const auto parsed_frame = decode_frame(decoded.payload);
    require(parsed_frame.ok(), parsed_frame.message);
    require(std::holds_alternative<ManifestFrame>(parsed_frame.frame), "ZXing decoded protocol frame should parse as manifest");
#endif
}

void test_zxing_decodes_qr_inside_larger_camera_like_frame_when_available()
{
#if AIRGAP_HAS_LIBQRENCODE && AIRGAP_HAS_ZXING
    const auto payload = sample_payload();
    const LibQrEncodeAdapter encoder(512);
    const ZxingQrDecoderAdapter decoder;

    const auto encoded = encoder.encode(payload);
    require(encoded.ok(), encoded.message);

    const auto qr_raster = render_qr_to_luminance(encoded.image, 8, 4);
    require(is_valid_raster_image_shape(qr_raster), "rendered QR should be valid");

    QrRasterImage camera_frame;
    camera_frame.width = qr_raster.width + 160;
    camera_frame.height = qr_raster.height + 120;
    camera_frame.luminance.assign(
        static_cast<std::size_t>(camera_frame.width) * camera_frame.height,
        0xffU);

    const std::uint32_t offset_x = 80;
    const std::uint32_t offset_y = 60;
    for (std::uint32_t y = 0; y < qr_raster.height; ++y) {
        for (std::uint32_t x = 0; x < qr_raster.width; ++x) {
            const auto source_index = static_cast<std::size_t>(y) * qr_raster.width + x;
            const auto target_index =
                static_cast<std::size_t>(offset_y + y) * camera_frame.width + offset_x + x;
            camera_frame.luminance[target_index] = qr_raster.luminance[source_index];
        }
    }

    const auto decoded = decoder.decode(camera_frame);
    require(decoded.ok(), decoded.message);
    require(decoded.payload == payload, "ZXing should decode QR inside a larger camera-like frame");
#endif
}

} // namespace

void run_cimbar_adapter_tests();

int main()
{
    test_mock_adapter_round_trips_protocol_payload();
    test_mock_adapter_rejects_empty_payload();
    test_mock_adapter_rejects_oversized_payload();
    test_mock_decoder_rejects_invalid_image_shape();
    test_mock_decoder_rejects_corrupt_magic();
    test_qr_render_to_luminance_adds_quiet_zone();
    test_qr_render_to_luminance_rejects_invalid_inputs();
    test_libqrencode_encoder_produces_module_grid_when_available();
    test_libqrencode_and_zxing_round_trip_protocol_payload_when_available();
    test_zxing_decodes_qr_inside_larger_camera_like_frame_when_available();
    run_cimbar_adapter_tests();

    std::cout << "airgap_qr_tests passed\n";
    return 0;
}
