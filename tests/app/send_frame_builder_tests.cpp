#include "frame.hpp"
#include "cimbar_payload.hpp"
#include "qr_adapter.hpp"
#include "qr_mock_adapter.hpp"
#include "receive_frame_collector.hpp"
#include "send_frame_builder.hpp"
#include "session.hpp"
#include "sha256.hpp"
#include "transfer_speed.hpp"

#include <cstdlib>
#include <iostream>
#include <limits>
#include <string_view>
#include <variant>

namespace {

using namespace aqrt::app;
using namespace aqrt::core;
using namespace aqrt::qr;

class LimitOnlyQrEncoder final : public IQrEncoder {
public:
    explicit LimitOnlyQrEncoder(std::size_t max_payload_size)
        : max_payload_size_(max_payload_size)
    {
    }

    EncodeResult encode(const Bytes& payload) const override
    {
        if (payload.empty()) {
            return {QrError::EmptyPayload, {}, "payload is empty"};
        }
        if (payload.size() > max_payload_size_) {
            return {QrError::PayloadTooLarge, {}, "payload exceeds test QR capacity"};
        }
        return {QrError::None, {}, {}};
    }

    [[nodiscard]] std::size_t max_payload_size() const override
    {
        return max_payload_size_;
    }

private:
    std::size_t max_payload_size_;
};

void require(bool condition, std::string_view message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

Bytes sample_file_bytes()
{
    return {'a', 'i', 'r', 'g', 'a', 'p', '-', 'q', 'r', '-', 't', 'e', 's', 't'};
}

Bytes repeated_file_bytes(std::size_t size)
{
    Bytes bytes(size);
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        bytes[i] = static_cast<std::uint8_t>(i % 251);
    }
    return bytes;
}

void test_build_send_package_generates_manifest_data_and_end_frames()
{
    const MockQrAdapter encoder(2048);
    const auto result = build_send_package(
        generate_session_id(),
        "sample.bin",
        sample_file_bytes(),
        5,
        encoder);

    require(result.ok(), result.message);
    require(result.package.manifest.total_chunks == 3, "14 bytes with chunk size 5 should produce 3 chunks");
    require(result.package.payloads.size() == 5, "frame sequence should contain manifest, 3 data frames, and end");
    require(result.package.qr_frames.size() == result.package.payloads.size(), "each payload should have a QR image");

    auto parsed = decode_frame(result.package.payloads[0]);
    require(parsed.ok(), parsed.message);
    require(std::holds_alternative<ManifestFrame>(parsed.frame), "first frame should be manifest");

    for (std::size_t index = 1; index <= 3; ++index) {
        parsed = decode_frame(result.package.payloads[index]);
        require(parsed.ok(), parsed.message);
        require(std::holds_alternative<DataFrame>(parsed.frame), "middle frames should be data");
        const auto& data_frame = std::get<DataFrame>(parsed.frame);
        require(data_frame.chunk_index == index - 1, "data frame chunk_index should be sequential");
        require(data_frame.total_chunks == 3, "data frame total_chunks should match manifest");
    }

    parsed = decode_frame(result.package.payloads.back());
    require(parsed.ok(), parsed.message);
    require(std::holds_alternative<EndFrame>(parsed.frame), "last frame should be end");
}

void test_build_send_package_rejects_unsafe_file_name()
{
    const MockQrAdapter encoder(2048);
    const auto result = build_send_package(
        generate_session_id(),
        "../escape.txt",
        sample_file_bytes(),
        5,
        encoder);

    require(result.error == SendBuildError::ManifestInvalid, "unsafe file name should fail manifest build");
    require(result.manifest_error == ManifestBuildError::UnsafeFileName, "unsafe file name should be reported");
}

void test_build_send_package_rejects_empty_file()
{
    const MockQrAdapter encoder(2048);
    const auto result = build_send_package(
        generate_session_id(),
        "empty.bin",
        Bytes{},
        5,
        encoder);

    require(result.error == SendBuildError::ManifestInvalid, "empty file should fail manifest build");
    require(result.chunk_plan_error == ChunkPlanError::EmptyFile, "empty file should report chunk planning error");
}

void test_build_send_package_reports_qr_capacity_errors()
{
    const MockQrAdapter encoder(8);
    const auto result = build_send_package(
        generate_session_id(),
        "sample.bin",
        sample_file_bytes(),
        5,
        encoder);

    require(result.error == SendBuildError::QrEncodeFailed, "small QR capacity should fail encoding");
    require(result.qr_error == QrError::PayloadTooLarge, "capacity failure should preserve QR error");
}

void test_transfer_speed_profiles_are_ordered_and_safe_by_default()
{
    require(kDefaultTransferSpeedMode == 0, "default speed mode should remain Safe");
    require(kTransferSpeedProfiles.size() >= 3, "speed mode list should contain Safe, Balanced, and Fast");

    const auto& safe = transfer_speed_profile(0);
    const auto& balanced = transfer_speed_profile(1);
    const auto& fast = transfer_speed_profile(2);

    require(safe.chunk_size == 512, "Safe mode should keep the original chunk size");
    require(safe.playback_interval_ms == 250, "Safe mode should keep the original playback interval");
    require(safe.decode_interval_ms == 150, "Safe mode should keep the original decode interval");
    require(balanced.chunk_size > safe.chunk_size, "Balanced should carry more data than Safe");
    require(fast.chunk_size > balanced.chunk_size, "Fast should carry more data than Balanced");
    require(balanced.playback_interval_ms < safe.playback_interval_ms, "Balanced should play faster than Safe");
    require(fast.playback_interval_ms < balanced.playback_interval_ms, "Fast should play faster than Balanced");
    require(normalize_transfer_speed_mode(-1) == kDefaultTransferSpeedMode, "negative modes should fall back to Safe");
    require(normalize_transfer_speed_mode(99) == kDefaultTransferSpeedMode, "out of range modes should fall back to Safe");
}

void test_transfer_speed_profiles_fit_default_qr_payload_limit()
{
    const LimitOnlyQrEncoder encoder(kDefaultQrPayloadLimit);
    const auto file_bytes = repeated_file_bytes(10 * 1024);

    for (int i = 0; i < static_cast<int>(kTransferSpeedProfiles.size()); ++i) {
        const auto& profile = transfer_speed_profile(i);
        if (profile.codec != VisualTransferCodec::Qr) {
            continue;
        }
        const auto result = build_send_package(
            generate_session_id(),
            "profile-fit.bin",
            file_bytes,
            profile.chunk_size,
            encoder);

        require(result.ok(), result.message);
    }
}

void test_cimbar_payload_roundtrip_verifies_hash()
{
    CimbarPayload payload;
    payload.session_id = generate_session_id();
    payload.file_name = "message.txt";
    payload.file_bytes = sample_file_bytes();
    payload.file_id = derive_file_id(
        payload.session_id,
        static_cast<std::uint64_t>(payload.file_bytes.size()),
        sha256(payload.file_bytes),
        payload.file_name);
    payload.text_message = true;

    const auto encoded = encode_cimbar_payload(payload);
    require(encoded.ok(), encoded.message);

    const auto decoded = decode_cimbar_payload(encoded.bytes);
    require(decoded.ok(), decoded.message);
    require(decoded.payload.file_name == payload.file_name, "cimbar payload should preserve the file name");
    require(decoded.payload.file_bytes == payload.file_bytes, "cimbar payload should preserve file bytes");
    require(decoded.payload.text_message, "cimbar payload should preserve text flag");
    require(decoded.payload.session_id == payload.session_id, "cimbar payload should preserve session_id");
    require(decoded.payload.file_id == payload.file_id, "cimbar payload should preserve file_id");
}

void test_cimbar_payload_rejects_corruption()
{
    CimbarPayload payload;
    payload.session_id = generate_session_id();
    payload.file_name = "sample.bin";
    payload.file_bytes = sample_file_bytes();
    payload.file_id = derive_file_id(
        payload.session_id,
        static_cast<std::uint64_t>(payload.file_bytes.size()),
        sha256(payload.file_bytes),
        payload.file_name);

    auto encoded = encode_cimbar_payload(payload);
    require(encoded.ok(), encoded.message);
    require(encoded.bytes.size() > 12, "encoded cimbar payload should be large enough to corrupt");
    encoded.bytes[encoded.bytes.size() - 8U] ^= 0x55U;

    const auto decoded = decode_cimbar_payload(encoded.bytes);
    require(decoded.error == CimbarPayloadError::Malformed, "cimbar payload should reject CRC corruption");
}

void test_cimbar_payload_rejects_name_that_exceeds_u16()
{
    CimbarPayload payload;
    payload.session_id = generate_session_id();
    payload.file_name.assign(static_cast<std::size_t>(std::numeric_limits<std::uint16_t>::max()) + 1U, 'a');
    payload.file_bytes = sample_file_bytes();
    payload.file_id = derive_file_id(
        payload.session_id,
        static_cast<std::uint64_t>(payload.file_bytes.size()),
        sha256(payload.file_bytes),
        payload.file_name);

    const auto encoded = encode_cimbar_payload(payload);
    require(encoded.error == CimbarPayloadError::InvalidName, "cimbar payload should reject file names above u16");
}

void test_text_payload_reuses_file_transfer_flow()
{
    const std::string text = "hello text transfer\ncopy me";
    const Bytes text_bytes(text.begin(), text.end());
    const LimitOnlyQrEncoder encoder(kDefaultQrPayloadLimit);
    const auto result = build_send_package(
        generate_session_id(),
        "message.txt",
        text_bytes,
        transfer_speed_profile(0).chunk_size,
        encoder,
        kManifestFlagTextMessage);

    require(result.ok(), result.message);
    require(result.package.manifest.file_name == "message.txt", "text transfer should use message.txt file name");
    require(
        (result.package.manifest.manifest_flags & kManifestFlagTextMessage) != 0U,
        "text transfer should set the manifest text flag");

    ReceiveFrameCollector collector;
    ReceiveFrameResult received;
    for (const auto& payload : result.package.payloads) {
        received = collector.add_payload(payload);
        require(received.ok(), received.message);
    }

    require(received.completed, "text transfer should complete through the normal receive collector");
    require(received.text_message, "received text transfer should preserve the text flag");
    require(received.assembled_bytes == text_bytes, "received text bytes should match sent UTF-8 bytes");
}

} // namespace

void run_receive_frame_collector_tests();
void run_qr_loopback_tests();

int main()
{
    test_build_send_package_generates_manifest_data_and_end_frames();
    test_build_send_package_rejects_unsafe_file_name();
    test_build_send_package_rejects_empty_file();
    test_build_send_package_reports_qr_capacity_errors();
    test_transfer_speed_profiles_are_ordered_and_safe_by_default();
    test_transfer_speed_profiles_fit_default_qr_payload_limit();
    test_cimbar_payload_roundtrip_verifies_hash();
    test_cimbar_payload_rejects_corruption();
    test_cimbar_payload_rejects_name_that_exceeds_u16();
    test_text_payload_reuses_file_transfer_flow();
    run_receive_frame_collector_tests();
    run_qr_loopback_tests();

    std::cout << "airgap_app_tests passed\n";
    return 0;
}
