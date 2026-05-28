#include "qr_adapter.hpp"
#include "qr_libqrencode_adapter.hpp"
#include "qr_zxing_adapter.hpp"
#include "receive_frame_collector.hpp"
#include "send_frame_builder.hpp"
#include "session.hpp"

#include <cstdlib>
#include <iostream>
#include <string_view>

namespace {

using namespace aqrt::app;
using namespace aqrt::core;
using namespace aqrt::qr;

void require(bool condition, std::string_view message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

Bytes sample_loopback_file()
{
    return {
        'l', 'o', 'o', 'p', 'b', 'a', 'c', 'k', '-',
        'q', 'r', '-', 'c', 'a', 'm', 'e', 'r', 'a', '-',
        'f', 'r', 'a', 'm', 'e',
    };
}

QrRasterImage center_in_camera_like_frame(const QrRasterImage& qr_raster)
{
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

    return camera_frame;
}

void test_real_qr_loopback_reassembles_file_when_available()
{
#if AIRGAP_HAS_LIBQRENCODE && AIRGAP_HAS_ZXING
    const auto file = sample_loopback_file();
    const LibQrEncodeAdapter encoder(4096);
    const auto build_result = build_send_package(
        generate_session_id(),
        "loopback.bin",
        file,
        7,
        encoder);
    require(build_result.ok(), build_result.message);

    const ZxingQrDecoderAdapter decoder;
    ReceiveFrameCollector collector;

    for (const auto& qr_image : build_result.package.qr_frames) {
        const auto qr_raster = render_qr_to_luminance(qr_image, 8, 4);
        require(is_valid_raster_image_shape(qr_raster), "QR frame should render to luminance");

        const auto camera_frame = center_in_camera_like_frame(qr_raster);
        const auto decoded = decoder.decode(camera_frame);
        require(decoded.ok(), decoded.message);

        const auto received = collector.add_payload(decoded.payload);
        require(received.ok(), received.message);
    }

    require(collector.completed(), "collector should complete after decoded QR frame loopback");
    require(collector.file_name() == "loopback.bin", "collector should preserve loopback file name");
    require(collector.assembled_bytes() == file, "loopback assembled bytes should equal original file");
#endif
}

} // namespace

void run_qr_loopback_tests()
{
    test_real_qr_loopback_reassembles_file_when_available();
}
