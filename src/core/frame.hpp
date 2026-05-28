#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <variant>
#include <vector>

namespace aqrt::core {

using Bytes = std::vector<std::uint8_t>;
using Id128 = std::array<std::uint8_t, 16>;
using Sha256 = std::array<std::uint8_t, 32>;

constexpr std::array<std::uint8_t, 4> kFrameMagic{'A', 'Q', 'R', 'T'};
constexpr std::uint8_t kVersionMajor = 1;
constexpr std::uint8_t kVersionMinor = 0;
constexpr std::uint16_t kFrameHeaderSize = 18;
constexpr std::uint32_t kManifestFlagTextMessage = 0x00000001U;
constexpr std::uint32_t kMissingRequestFlagNeedEnd = 0x00000001U;

enum class FrameType : std::uint8_t {
    Manifest = 1,
    Data = 2,
    End = 3,
    MissingRequest = 4,
};

struct ManifestFrame {
    Id128 session_id{};
    Id128 file_id{};
    std::uint32_t manifest_flags = 0;
    std::uint64_t file_size = 0;
    std::uint32_t chunk_size = 0;
    std::uint32_t total_chunks = 0;
    Sha256 file_sha256{};
    std::string file_name;
};

struct DataFrame {
    Id128 session_id{};
    Id128 file_id{};
    std::uint32_t chunk_index = 0;
    std::uint32_t total_chunks = 0;
    std::uint64_t chunk_offset = 0;
    Bytes data;
};

struct EndFrame {
    Id128 session_id{};
    Id128 file_id{};
    std::uint32_t total_chunks = 0;
    std::uint64_t file_size = 0;
    Sha256 file_sha256{};
};

struct MissingRange {
    std::uint32_t start_index = 0;
    std::uint32_t count = 0;
};

struct MissingRequestFrame {
    Id128 session_id{};
    Id128 file_id{};
    std::uint32_t request_flags = 0;
    std::uint32_t total_chunks = 0;
    std::uint32_t received_chunks = 0;
    std::uint32_t feedback_seq = 0;
    std::vector<MissingRange> ranges;
};

using Frame = std::variant<ManifestFrame, DataFrame, EndFrame, MissingRequestFrame>;

enum class ParseError {
    None,
    TooShort,
    BadMagic,
    UnsupportedVersion,
    UnknownFrameType,
    HeaderSizeTooSmall,
    LengthMismatch,
    CrcMismatch,
    MalformedBody,
    InvalidManifest,
    InvalidData,
    InvalidEnd,
    InvalidMissingRequest,
};

struct ParseResult {
    ParseError error = ParseError::None;
    Frame frame = ManifestFrame{};
    std::string message;

    [[nodiscard]] bool ok() const { return error == ParseError::None; }
};

Bytes encode_frame(const Frame& frame);
ParseResult decode_frame(const Bytes& payload);
ParseResult decode_frame(std::span<const std::uint8_t> payload);

const char* parse_error_name(ParseError error);

} // namespace aqrt::core
