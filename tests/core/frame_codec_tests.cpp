#include "crc32.hpp"
#include "frame.hpp"
#include "test_support.hpp"

#include <span>
#include <string_view>

namespace {

using namespace aqrt::core;
using namespace aqrt::core::test;

void test_crc32_known_vector()
{
    const std::string_view input = "123456789";
    const auto bytes = std::span<const std::uint8_t>(
        reinterpret_cast<const std::uint8_t*>(input.data()), input.size());
    require(crc32(bytes) == 0xCBF43926U, "CRC32 known vector should match");
}

void test_manifest_round_trip()
{
    ManifestFrame manifest;
    manifest.session_id = id_from_seed(1);
    manifest.file_id = id_from_seed(33);
    manifest.file_size = 1025;
    manifest.chunk_size = 256;
    manifest.total_chunks = 5;
    manifest.file_sha256 = sha_from_seed(9);
    manifest.file_name = "sample.bin";

    const auto encoded = encode_frame(Frame{manifest});
    const auto decoded = decode_frame(encoded);

    require(decoded.ok(), decoded.message);
    require(std::holds_alternative<ManifestFrame>(decoded.frame), "decoded frame should be manifest");
    const auto& parsed = std::get<ManifestFrame>(decoded.frame);
    require(parsed.session_id == manifest.session_id, "manifest session_id should round trip");
    require(parsed.file_id == manifest.file_id, "manifest file_id should round trip");
    require(parsed.file_size == manifest.file_size, "manifest file_size should round trip");
    require(parsed.chunk_size == manifest.chunk_size, "manifest chunk_size should round trip");
    require(parsed.total_chunks == manifest.total_chunks, "manifest total_chunks should round trip");
    require(parsed.file_sha256 == manifest.file_sha256, "manifest sha256 should round trip");
    require(parsed.file_name == manifest.file_name, "manifest file_name should round trip");
}

void test_data_round_trip()
{
    DataFrame data;
    data.session_id = id_from_seed(2);
    data.file_id = id_from_seed(44);
    data.chunk_index = 3;
    data.total_chunks = 8;
    data.chunk_offset = 768;
    data.data = {0, 1, 2, 3, 4, 5};

    const auto encoded = encode_frame(Frame{data});
    const auto decoded = decode_frame(encoded);

    require(decoded.ok(), decoded.message);
    require(std::holds_alternative<DataFrame>(decoded.frame), "decoded frame should be data");
    const auto& parsed = std::get<DataFrame>(decoded.frame);
    require(parsed.session_id == data.session_id, "data session_id should round trip");
    require(parsed.file_id == data.file_id, "data file_id should round trip");
    require(parsed.chunk_index == data.chunk_index, "data chunk_index should round trip");
    require(parsed.total_chunks == data.total_chunks, "data total_chunks should round trip");
    require(parsed.chunk_offset == data.chunk_offset, "data chunk_offset should round trip");
    require(parsed.data == data.data, "data payload should round trip");
}

void test_end_round_trip()
{
    EndFrame end;
    end.session_id = id_from_seed(3);
    end.file_id = id_from_seed(55);
    end.total_chunks = 4;
    end.file_size = 999;
    end.file_sha256 = sha_from_seed(12);

    const auto encoded = encode_frame(Frame{end});
    const auto decoded = decode_frame(encoded);

    require(decoded.ok(), decoded.message);
    require(std::holds_alternative<EndFrame>(decoded.frame), "decoded frame should be end");
    const auto& parsed = std::get<EndFrame>(decoded.frame);
    require(parsed.session_id == end.session_id, "end session_id should round trip");
    require(parsed.file_id == end.file_id, "end file_id should round trip");
    require(parsed.total_chunks == end.total_chunks, "end total_chunks should round trip");
    require(parsed.file_size == end.file_size, "end file_size should round trip");
    require(parsed.file_sha256 == end.file_sha256, "end sha256 should round trip");
}

void test_missing_request_round_trip()
{
    MissingRequestFrame request;
    request.session_id = id_from_seed(13);
    request.file_id = id_from_seed(66);
    request.request_flags = kMissingRequestFlagNeedEnd;
    request.total_chunks = 100;
    request.received_chunks = 94;
    request.feedback_seq = 7;
    request.ranges = {
        MissingRange{3, 2},
        MissingRange{50, 4},
    };

    const auto encoded = encode_frame(Frame{request});
    const auto decoded = decode_frame(encoded);

    require(decoded.ok(), decoded.message);
    require(std::holds_alternative<MissingRequestFrame>(decoded.frame), "decoded frame should be missing request");
    const auto& parsed = std::get<MissingRequestFrame>(decoded.frame);
    require(parsed.session_id == request.session_id, "missing request session_id should round trip");
    require(parsed.file_id == request.file_id, "missing request file_id should round trip");
    require(parsed.request_flags == request.request_flags, "missing request flags should round trip");
    require(parsed.total_chunks == request.total_chunks, "missing request total_chunks should round trip");
    require(parsed.received_chunks == request.received_chunks, "missing request received_chunks should round trip");
    require(parsed.feedback_seq == request.feedback_seq, "missing request feedback_seq should round trip");
    require(parsed.ranges.size() == 2, "missing request ranges should round trip");
    require(parsed.ranges[0].start_index == 3 && parsed.ranges[0].count == 2, "first missing range should round trip");
    require(parsed.ranges[1].start_index == 50 && parsed.ranges[1].count == 4, "second missing range should round trip");
}

void test_crc_error_is_rejected()
{
    DataFrame data;
    data.session_id = id_from_seed(4);
    data.file_id = id_from_seed(77);
    data.chunk_index = 0;
    data.total_chunks = 1;
    data.data = {42};

    auto encoded = encode_frame(Frame{data});
    encoded.back() ^= 0xFFU;

    const auto decoded = decode_frame(encoded);
    require(decoded.error == ParseError::CrcMismatch, "corrupt payload should fail CRC32");
}

void test_bad_version_is_rejected()
{
    EndFrame end;
    end.session_id = id_from_seed(5);
    end.file_id = id_from_seed(88);
    end.total_chunks = 1;
    end.file_size = 1;

    auto encoded = encode_frame(Frame{end});
    encoded[4] = 99;

    const auto decoded = decode_frame(encoded);
    require(decoded.error == ParseError::UnsupportedVersion, "unsupported major version should be rejected");
}

void test_length_mismatch_is_rejected()
{
    EndFrame end;
    end.session_id = id_from_seed(6);
    end.file_id = id_from_seed(99);
    end.total_chunks = 1;
    end.file_size = 1;

    auto encoded = encode_frame(Frame{end});
    encoded.pop_back();

    const auto decoded = decode_frame(encoded);
    require(decoded.error == ParseError::LengthMismatch, "truncated payload should fail length check");
}

void test_unknown_frame_type_is_rejected()
{
    EndFrame end;
    end.session_id = id_from_seed(7);
    end.file_id = id_from_seed(111);
    end.total_chunks = 1;
    end.file_size = 1;

    auto encoded = encode_frame(Frame{end});
    encoded[6] = 99;

    const auto decoded = decode_frame(encoded);
    require(decoded.error == ParseError::UnknownFrameType, "unknown frame type should be rejected");
}

void test_invalid_manifest_is_rejected()
{
    ManifestFrame manifest;
    manifest.session_id = id_from_seed(8);
    manifest.file_id = id_from_seed(120);
    manifest.file_size = 1024;
    manifest.chunk_size = 256;
    manifest.total_chunks = 3;
    manifest.file_name = "bad.bin";

    const auto decoded = decode_frame(encode_frame(Frame{manifest}));
    require(decoded.error == ParseError::InvalidManifest, "manifest with wrong chunk count should be rejected");
}

void test_invalid_data_index_is_rejected()
{
    DataFrame data;
    data.session_id = id_from_seed(9);
    data.file_id = id_from_seed(130);
    data.chunk_index = 2;
    data.total_chunks = 2;
    data.data = {1, 2, 3};

    const auto decoded = decode_frame(encode_frame(Frame{data}));
    require(decoded.error == ParseError::InvalidData, "data chunk_index must be less than total_chunks");
}

void test_invalid_missing_request_range_is_rejected()
{
    MissingRequestFrame request;
    request.session_id = id_from_seed(14);
    request.file_id = id_from_seed(67);
    request.total_chunks = 10;
    request.received_chunks = 8;
    request.ranges = {MissingRange{9, 2}};

    const auto decoded = decode_frame(encode_frame(Frame{request}));
    require(
        decoded.error == ParseError::InvalidMissingRequest,
        "missing request range beyond total_chunks should be rejected");
}

} // namespace

void run_frame_codec_tests()
{
    test_crc32_known_vector();
    test_manifest_round_trip();
    test_data_round_trip();
    test_end_round_trip();
    test_missing_request_round_trip();
    test_crc_error_is_rejected();
    test_bad_version_is_rejected();
    test_length_mismatch_is_rejected();
    test_unknown_frame_type_is_rejected();
    test_invalid_manifest_is_rejected();
    test_invalid_data_index_is_rejected();
    test_invalid_missing_request_range_is_rejected();
}
