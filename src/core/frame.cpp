#include "frame.hpp"

#include "crc32.hpp"

#include <algorithm>
#include <limits>
#include <type_traits>
#include <utility>

namespace aqrt::core {
namespace {

constexpr std::size_t kCrcOffset = 14;

void append_u8(Bytes& out, std::uint8_t value)
{
    out.push_back(value);
}

void append_u16(Bytes& out, std::uint16_t value)
{
    out.push_back(static_cast<std::uint8_t>(value & 0xFFU));
    out.push_back(static_cast<std::uint8_t>((value >> 8U) & 0xFFU));
}

void append_u32(Bytes& out, std::uint32_t value)
{
    for (int shift = 0; shift < 32; shift += 8) {
        out.push_back(static_cast<std::uint8_t>((value >> shift) & 0xFFU));
    }
}

void append_u64(Bytes& out, std::uint64_t value)
{
    for (int shift = 0; shift < 64; shift += 8) {
        out.push_back(static_cast<std::uint8_t>((value >> shift) & 0xFFU));
    }
}

template <std::size_t N>
void append_array(Bytes& out, const std::array<std::uint8_t, N>& value)
{
    out.insert(out.end(), value.begin(), value.end());
}

void append_bytes(Bytes& out, std::span<const std::uint8_t> value)
{
    out.insert(out.end(), value.begin(), value.end());
}

void append_bytes(Bytes& out, const Bytes& value)
{
    append_bytes(out, std::span<const std::uint8_t>(value.data(), value.size()));
}

class Reader {
public:
    explicit Reader(std::span<const std::uint8_t> bytes)
        : bytes_(bytes)
    {
    }

    [[nodiscard]] std::size_t remaining() const
    {
        return bytes_.size() - offset_;
    }

    bool read_u8(std::uint8_t& value)
    {
        if (remaining() < 1) {
            return false;
        }
        value = bytes_[offset_++];
        return true;
    }

    bool read_u16(std::uint16_t& value)
    {
        if (remaining() < 2) {
            return false;
        }
        value = static_cast<std::uint16_t>(bytes_[offset_])
            | (static_cast<std::uint16_t>(bytes_[offset_ + 1]) << 8U);
        offset_ += 2;
        return true;
    }

    bool read_u32(std::uint32_t& value)
    {
        if (remaining() < 4) {
            return false;
        }
        value = 0;
        for (int shift = 0; shift < 32; shift += 8) {
            value |= static_cast<std::uint32_t>(bytes_[offset_++]) << shift;
        }
        return true;
    }

    bool read_u64(std::uint64_t& value)
    {
        if (remaining() < 8) {
            return false;
        }
        value = 0;
        for (int shift = 0; shift < 64; shift += 8) {
            value |= static_cast<std::uint64_t>(bytes_[offset_++]) << shift;
        }
        return true;
    }

    template <std::size_t N>
    bool read_array(std::array<std::uint8_t, N>& value)
    {
        if (remaining() < N) {
            return false;
        }
        std::copy_n(bytes_.begin() + static_cast<std::ptrdiff_t>(offset_), N, value.begin());
        offset_ += N;
        return true;
    }

    bool read_bytes(Bytes& value, std::size_t size)
    {
        if (remaining() < size) {
            return false;
        }
        value.assign(bytes_.begin() + static_cast<std::ptrdiff_t>(offset_),
            bytes_.begin() + static_cast<std::ptrdiff_t>(offset_ + size));
        offset_ += size;
        return true;
    }

    bool read_string(std::string& value, std::size_t size)
    {
        if (remaining() < size) {
            return false;
        }
        value.assign(reinterpret_cast<const char*>(bytes_.data() + offset_), size);
        offset_ += size;
        return true;
    }

private:
    std::span<const std::uint8_t> bytes_;
    std::size_t offset_ = 0;
};

ParseResult fail(ParseError error, std::string message)
{
    return ParseResult{error, ManifestFrame{}, std::move(message)};
}

bool has_invalid_file_name_characters(const std::string& file_name)
{
    if (file_name.empty()) {
        return true;
    }
    for (const unsigned char ch : file_name) {
        if (ch < 0x20U || ch == '/' || ch == '\\' || ch == ':') {
            return true;
        }
    }
    return false;
}

std::uint64_t ceil_div(std::uint64_t value, std::uint32_t divisor)
{
    return (value + divisor - 1U) / divisor;
}

Bytes encode_manifest_body(const ManifestFrame& frame)
{
    Bytes body;
    append_array(body, frame.session_id);
    append_array(body, frame.file_id);
    append_u32(body, frame.manifest_flags);
    append_u64(body, frame.file_size);
    append_u32(body, frame.chunk_size);
    append_u32(body, frame.total_chunks);
    append_array(body, frame.file_sha256);
    append_u16(body, static_cast<std::uint16_t>(frame.file_name.size()));
    body.insert(body.end(), frame.file_name.begin(), frame.file_name.end());
    return body;
}

Bytes encode_data_body(const DataFrame& frame)
{
    Bytes body;
    append_array(body, frame.session_id);
    append_array(body, frame.file_id);
    append_u32(body, frame.chunk_index);
    append_u32(body, frame.total_chunks);
    append_u64(body, frame.chunk_offset);
    append_u32(body, static_cast<std::uint32_t>(frame.data.size()));
    append_bytes(body, frame.data);
    return body;
}

Bytes encode_end_body(const EndFrame& frame)
{
    Bytes body;
    append_array(body, frame.session_id);
    append_array(body, frame.file_id);
    append_u32(body, frame.total_chunks);
    append_u64(body, frame.file_size);
    append_array(body, frame.file_sha256);
    return body;
}

Bytes encode_missing_request_body(const MissingRequestFrame& frame)
{
    Bytes body;
    append_array(body, frame.session_id);
    append_array(body, frame.file_id);
    append_u32(body, frame.request_flags);
    append_u32(body, frame.total_chunks);
    append_u32(body, frame.received_chunks);
    append_u32(body, frame.feedback_seq);
    append_u16(body, static_cast<std::uint16_t>(frame.ranges.size()));
    for (const auto& range : frame.ranges) {
        append_u32(body, range.start_index);
        append_u32(body, range.count);
    }
    return body;
}

ParseResult parse_manifest(std::span<const std::uint8_t> body)
{
    Reader reader(body);
    ManifestFrame frame;
    std::uint16_t file_name_size = 0;

    if (!reader.read_array(frame.session_id)
        || !reader.read_array(frame.file_id)
        || !reader.read_u32(frame.manifest_flags)
        || !reader.read_u64(frame.file_size)
        || !reader.read_u32(frame.chunk_size)
        || !reader.read_u32(frame.total_chunks)
        || !reader.read_array(frame.file_sha256)
        || !reader.read_u16(file_name_size)
        || !reader.read_string(frame.file_name, file_name_size)
        || reader.remaining() != 0) {
        return fail(ParseError::MalformedBody, "manifest body is truncated or has trailing bytes");
    }

    if (frame.file_size == 0 || frame.chunk_size == 0 || frame.total_chunks == 0) {
        return fail(ParseError::InvalidManifest, "manifest contains zero file size, chunk size, or total chunks");
    }
    if (ceil_div(frame.file_size, frame.chunk_size) != frame.total_chunks) {
        return fail(ParseError::InvalidManifest, "manifest chunk count does not match file size and chunk size");
    }
    if (has_invalid_file_name_characters(frame.file_name)) {
        return fail(ParseError::InvalidManifest, "manifest file name is empty or unsafe");
    }

    return ParseResult{ParseError::None, frame, {}};
}

ParseResult parse_data(std::span<const std::uint8_t> body)
{
    Reader reader(body);
    DataFrame frame;
    std::uint32_t data_size = 0;

    if (!reader.read_array(frame.session_id)
        || !reader.read_array(frame.file_id)
        || !reader.read_u32(frame.chunk_index)
        || !reader.read_u32(frame.total_chunks)
        || !reader.read_u64(frame.chunk_offset)
        || !reader.read_u32(data_size)
        || !reader.read_bytes(frame.data, data_size)
        || reader.remaining() != 0) {
        return fail(ParseError::MalformedBody, "data body is truncated or has trailing bytes");
    }

    if (frame.total_chunks == 0 || frame.chunk_index >= frame.total_chunks || frame.data.empty()) {
        return fail(ParseError::InvalidData, "data frame has invalid chunk index, total chunks, or empty data");
    }

    return ParseResult{ParseError::None, frame, {}};
}

ParseResult parse_end(std::span<const std::uint8_t> body)
{
    Reader reader(body);
    EndFrame frame;

    if (!reader.read_array(frame.session_id)
        || !reader.read_array(frame.file_id)
        || !reader.read_u32(frame.total_chunks)
        || !reader.read_u64(frame.file_size)
        || !reader.read_array(frame.file_sha256)
        || reader.remaining() != 0) {
        return fail(ParseError::MalformedBody, "end body is truncated or has trailing bytes");
    }

    if (frame.total_chunks == 0 || frame.file_size == 0) {
        return fail(ParseError::InvalidEnd, "end frame contains zero file size or total chunks");
    }

    return ParseResult{ParseError::None, frame, {}};
}

ParseResult parse_missing_request(std::span<const std::uint8_t> body)
{
    Reader reader(body);
    MissingRequestFrame frame;
    std::uint16_t range_count = 0;

    if (!reader.read_array(frame.session_id)
        || !reader.read_array(frame.file_id)
        || !reader.read_u32(frame.request_flags)
        || !reader.read_u32(frame.total_chunks)
        || !reader.read_u32(frame.received_chunks)
        || !reader.read_u32(frame.feedback_seq)
        || !reader.read_u16(range_count)) {
        return fail(ParseError::MalformedBody, "missing request body is truncated");
    }

    frame.ranges.reserve(range_count);
    for (std::uint16_t i = 0; i < range_count; ++i) {
        MissingRange range;
        if (!reader.read_u32(range.start_index) || !reader.read_u32(range.count)) {
            return fail(ParseError::MalformedBody, "missing request range is truncated");
        }
        frame.ranges.push_back(range);
    }
    if (reader.remaining() != 0) {
        return fail(ParseError::MalformedBody, "missing request body has trailing bytes");
    }

    if (frame.total_chunks == 0 || frame.received_chunks > frame.total_chunks) {
        return fail(ParseError::InvalidMissingRequest, "missing request has invalid chunk counts");
    }
    for (const auto& range : frame.ranges) {
        if (range.count == 0 || range.start_index >= frame.total_chunks) {
            return fail(ParseError::InvalidMissingRequest, "missing request range has invalid start or count");
        }
        if (range.count > frame.total_chunks - range.start_index) {
            return fail(ParseError::InvalidMissingRequest, "missing request range exceeds total chunks");
        }
    }

    return ParseResult{ParseError::None, frame, {}};
}

Bytes encode_with_header(FrameType type, Bytes body)
{
    Bytes payload;
    append_array(payload, kFrameMagic);
    append_u8(payload, kVersionMajor);
    append_u8(payload, kVersionMinor);
    append_u8(payload, static_cast<std::uint8_t>(type));
    append_u8(payload, 0);
    append_u16(payload, kFrameHeaderSize);
    append_u32(payload, static_cast<std::uint32_t>(body.size()));

    Bytes crc_input = payload;
    append_bytes(crc_input, body);
    const auto checksum = crc32(crc_input);
    append_u32(payload, checksum);
    append_bytes(payload, body);
    return payload;
}

} // namespace

Bytes encode_frame(const Frame& frame)
{
    return std::visit(
        [](const auto& typed_frame) -> Bytes {
            using T = std::decay_t<decltype(typed_frame)>;
            if constexpr (std::is_same_v<T, ManifestFrame>) {
                return encode_with_header(FrameType::Manifest, encode_manifest_body(typed_frame));
            } else if constexpr (std::is_same_v<T, DataFrame>) {
                return encode_with_header(FrameType::Data, encode_data_body(typed_frame));
            } else if constexpr (std::is_same_v<T, EndFrame>) {
                return encode_with_header(FrameType::End, encode_end_body(typed_frame));
            } else {
                return encode_with_header(FrameType::MissingRequest, encode_missing_request_body(typed_frame));
            }
        },
        frame);
}

ParseResult decode_frame(std::span<const std::uint8_t> payload)
{
    if (payload.size() < kFrameHeaderSize) {
        return fail(ParseError::TooShort, "payload is shorter than the frame header");
    }

    Reader reader(payload);
    std::array<std::uint8_t, 4> magic{};
    std::uint8_t version_major = 0;
    std::uint8_t version_minor = 0;
    std::uint8_t frame_type = 0;
    std::uint8_t header_flags = 0;
    std::uint16_t header_size = 0;
    std::uint32_t body_size = 0;
    std::uint32_t expected_crc = 0;

    if (!reader.read_array(magic)
        || !reader.read_u8(version_major)
        || !reader.read_u8(version_minor)
        || !reader.read_u8(frame_type)
        || !reader.read_u8(header_flags)
        || !reader.read_u16(header_size)
        || !reader.read_u32(body_size)
        || !reader.read_u32(expected_crc)) {
        return fail(ParseError::TooShort, "payload header is truncated");
    }

    (void)version_minor;
    (void)header_flags;

    if (magic != kFrameMagic) {
        return fail(ParseError::BadMagic, "frame magic does not match AQRT");
    }
    if (version_major != kVersionMajor) {
        return fail(ParseError::UnsupportedVersion, "frame major version is not supported");
    }
    if (header_size < kFrameHeaderSize) {
        return fail(ParseError::HeaderSizeTooSmall, "frame header size is smaller than the v1 header");
    }
    if (frame_type < static_cast<std::uint8_t>(FrameType::Manifest)
        || frame_type > static_cast<std::uint8_t>(FrameType::MissingRequest)) {
        return fail(ParseError::UnknownFrameType, "frame type is unknown");
    }
    if (payload.size() < header_size) {
        return fail(ParseError::LengthMismatch, "payload is shorter than declared header size");
    }
    if (body_size > std::numeric_limits<std::uint32_t>::max() - header_size
        || payload.size() != static_cast<std::size_t>(header_size) + body_size) {
        return fail(ParseError::LengthMismatch, "payload length does not match declared body size");
    }

    Bytes crc_input;
    append_bytes(crc_input, payload.first(kCrcOffset));
    append_bytes(crc_input, payload.subspan(header_size, body_size));
    if (crc32(crc_input) != expected_crc) {
        return fail(ParseError::CrcMismatch, "frame CRC32 does not match");
    }

    const auto body = payload.subspan(header_size, body_size);
    switch (static_cast<FrameType>(frame_type)) {
    case FrameType::Manifest:
        return parse_manifest(body);
    case FrameType::Data:
        return parse_data(body);
    case FrameType::End:
        return parse_end(body);
    case FrameType::MissingRequest:
        return parse_missing_request(body);
    }

    return fail(ParseError::UnknownFrameType, "frame type is unknown");
}

ParseResult decode_frame(const Bytes& payload)
{
    return decode_frame(std::span<const std::uint8_t>(payload.data(), payload.size()));
}

const char* parse_error_name(ParseError error)
{
    switch (error) {
    case ParseError::None:
        return "None";
    case ParseError::TooShort:
        return "TooShort";
    case ParseError::BadMagic:
        return "BadMagic";
    case ParseError::UnsupportedVersion:
        return "UnsupportedVersion";
    case ParseError::UnknownFrameType:
        return "UnknownFrameType";
    case ParseError::HeaderSizeTooSmall:
        return "HeaderSizeTooSmall";
    case ParseError::LengthMismatch:
        return "LengthMismatch";
    case ParseError::CrcMismatch:
        return "CrcMismatch";
    case ParseError::MalformedBody:
        return "MalformedBody";
    case ParseError::InvalidManifest:
        return "InvalidManifest";
    case ParseError::InvalidData:
        return "InvalidData";
    case ParseError::InvalidEnd:
        return "InvalidEnd";
    case ParseError::InvalidMissingRequest:
        return "InvalidMissingRequest";
    }

    return "Unknown";
}

} // namespace aqrt::core
