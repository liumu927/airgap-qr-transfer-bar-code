#include "frame.hpp"
#include "qr_mock_adapter.hpp"
#include "receive_frame_collector.hpp"
#include "send_frame_builder.hpp"
#include "session.hpp"

#include <cstdlib>
#include <iostream>
#include <string_view>
#include <utility>
#include <variant>

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

Bytes sample_receive_file()
{
    return {
        'r', 'e', 'c', 'e', 'i', 'v', 'e', '-',
        's', 'i', 'd', 'e', '-', 'f', 'i', 'l', 'e',
    };
}

SendPackage make_package(const Bytes& file, std::uint32_t chunk_size = 6)
{
    const MockQrAdapter encoder(4096);
    auto result = build_send_package(
        generate_session_id(),
        "received.bin",
        file,
        chunk_size,
        encoder);
    require(result.ok(), result.message);
    return std::move(result.package);
}

SendPackage make_text_package(const Bytes& text, std::uint32_t chunk_size = 6)
{
    const MockQrAdapter encoder(4096);
    auto result = build_send_package(
        generate_session_id(),
        "message.txt",
        text,
        chunk_size,
        encoder,
        kManifestFlagTextMessage);
    require(result.ok(), result.message);
    return std::move(result.package);
}

void test_collector_reassembles_after_end_frame()
{
    const auto file = sample_receive_file();
    const auto package = make_package(file, 6);

    ReceiveFrameCollector collector;
    auto result = collector.add_payload(package.payloads[0]);
    require(result.ok(), result.message);
    require(result.total_chunks == 3, "manifest should publish total chunk count");
    require(collector.file_name() == "received.bin", "manifest should publish file name");

    require(collector.add_payload(package.payloads[2]).ok(), "data chunk 1 should be accepted out of order");
    require(collector.add_payload(package.payloads[1]).ok(), "data chunk 0 should be accepted out of order");
    result = collector.add_payload(package.payloads[3]);
    require(result.ok(), result.message);
    require(collector.data_complete(), "all data chunks should be complete before end");
    require(!collector.completed(), "collector should wait for end frame before completion");

    result = collector.add_payload(package.payloads.back());
    require(result.ok(), result.message);
    require(result.completed, "end frame should allow final assembly");
    require(!result.text_message, "normal file transfer should not be marked as a text message");
    require(result.assembled_bytes == file, "assembled bytes should equal source file");
    require(collector.assembled_bytes() == file, "collector should retain assembled bytes");
}

void test_collector_preserves_text_message_flag()
{
    const Bytes text{'c', 'o', 'p', 'y', ' ', 'm', 'e'};
    const auto package = make_text_package(text);

    ReceiveFrameCollector collector;
    ReceiveFrameResult result;
    for (const auto& payload : package.payloads) {
        result = collector.add_payload(payload);
        require(result.ok(), result.message);
    }

    require(result.completed, "text message package should complete");
    require(result.text_message, "text message flag should survive through the collector");
    require(collector.text_message(), "collector should expose text message state");
}

void test_collector_duplicate_data_is_idempotent()
{
    const auto package = make_package(sample_receive_file(), 6);

    ReceiveFrameCollector collector;
    require(collector.add_payload(package.payloads[0]).ok(), "manifest should be accepted");
    auto first = collector.add_payload(package.payloads[1]);
    auto second = collector.add_payload(package.payloads[1]);

    require(first.ok() && first.accepted && !first.duplicate, "first data frame should be new");
    require(second.ok() && second.accepted && second.duplicate, "same data frame should be duplicate");
    require(second.received_chunks == 1, "duplicate data should not increase progress");
}

void test_collector_rejects_bad_payload()
{
    ReceiveFrameCollector collector;
    const Bytes bad_payload{0x00, 0x01, 0x02};
    const auto result = collector.add_payload(bad_payload);

    require(result.error == ReceiveFrameError::DecodeFailed, "bad payload should fail frame decoding");
    require(result.parse_error == ParseError::TooShort, "short payload should report TooShort");
}

void test_collector_rejects_cross_session_data()
{
    const auto package = make_package(sample_receive_file(), 6);
    auto parsed = decode_frame(package.payloads[1]);
    require(parsed.ok(), parsed.message);
    auto data = std::get<DataFrame>(parsed.frame);
    data.session_id = generate_session_id();
    const auto wrong_session_payload = encode_frame(data);

    ReceiveFrameCollector collector;
    require(collector.add_payload(package.payloads[0]).ok(), "manifest should be accepted");
    const auto result = collector.add_payload(wrong_session_payload);

    require(result.error == ReceiveFrameError::AssemblerRejected, "cross-session data should be rejected");
    require(result.assembler_error == AssemblerError::SessionMismatch, "session mismatch should be reported");
}

void test_collector_rejects_end_before_manifest()
{
    const auto package = make_package(sample_receive_file(), 6);

    ReceiveFrameCollector collector;
    const auto result = collector.add_payload(package.payloads.back());

    require(result.error == ReceiveFrameError::AssemblerRejected, "end before manifest should be rejected");
    require(result.assembler_error == AssemblerError::MissingManifest, "missing manifest should be reported");
}

void test_collector_builds_missing_request()
{
    const auto package = make_package(sample_receive_file(), 6);

    ReceiveFrameCollector collector;
    require(collector.add_payload(package.payloads[0]).ok(), "manifest should be accepted");
    require(collector.add_payload(package.payloads[1]).ok(), "data chunk 0 should be accepted");
    require(collector.add_payload(package.payloads[3]).ok(), "data chunk 2 should be accepted");

    require(collector.missing_chunk_count() == 1, "collector should report one missing chunk");
    const auto request = collector.missing_request_frame(7);
    require(request.has_value(), "partial receive state should produce a missing request");
    require(request->request_flags == kMissingRequestFlagNeedEnd, "missing request should ask for end if end is absent");
    require(request->total_chunks == 3, "missing request should keep manifest total chunk count");
    require(request->received_chunks == 2, "missing request should publish received chunk count");
    require(request->feedback_seq == 7, "missing request should keep feedback sequence");
    require(request->ranges.size() == 1, "missing request should compact contiguous ranges");
    require(request->ranges[0].start_index == 1, "missing request should point to the missing chunk");
    require(request->ranges[0].count == 1, "missing request range should contain one chunk");
}

void test_collector_rejects_missing_request_payload()
{
    const auto package = make_package(sample_receive_file(), 6);

    ReceiveFrameCollector collector;
    require(collector.add_payload(package.payloads[0]).ok(), "manifest should be accepted");
    const auto request = collector.missing_request_frame(1);
    require(request.has_value(), "missing request should be available");

    const auto result = collector.add_payload(encode_frame(*request));
    require(result.error == ReceiveFrameError::UnsupportedFrame, "collector should reject feedback frames in data flow");
}

} // namespace

void run_receive_frame_collector_tests()
{
    test_collector_reassembles_after_end_frame();
    test_collector_preserves_text_message_flag();
    test_collector_duplicate_data_is_idempotent();
    test_collector_rejects_bad_payload();
    test_collector_rejects_cross_session_data();
    test_collector_rejects_end_before_manifest();
    test_collector_builds_missing_request();
    test_collector_rejects_missing_request_payload();
}
