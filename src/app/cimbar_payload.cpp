#include "cimbar_payload.hpp"

#include "crc32.hpp"
#include "sha256.hpp"

#include <algorithm>
#include <array>
#include <limits>

namespace aqrt::app {
namespace {

constexpr std::array<std::uint8_t, 8> kMagic{{'A', 'Q', 'T', 'C', 'I', 'M', '1', 0}};
constexpr std::uint8_t kVersion = 1;
constexpr std::uint32_t kFlagTextMessage = 0x00000001U;
constexpr std::size_t kMaxFileNameBytes = std::numeric_limits<std::uint16_t>::max();

void append_u16(core::Bytes& out, std::uint16_t value)
{
    out.push_back(static_cast<std::uint8_t>(value & 0xffU));
    out.push_back(static_cast<std::uint8_t>((value >> 8U) & 0xffU));
}

void append_u32(core::Bytes& out, std::uint32_t value)
{
    for (int shift = 0; shift < 32; shift += 8) {
        out.push_back(static_cast<std::uint8_t>((value >> shift) & 0xffU));
    }
}

void append_u64(core::Bytes& out, std::uint64_t value)
{
    for (int shift = 0; shift < 64; shift += 8) {
        out.push_back(static_cast<std::uint8_t>((value >> shift) & 0xffU));
    }
}

void append_id(core::Bytes& out, const core::Id128& id)
{
    out.insert(out.end(), id.begin(), id.end());
}

bool read_u8(const core::Bytes& bytes, std::size_t& offset, std::uint8_t& value)
{
    if (offset >= bytes.size()) {
        return false;
    }
    value = bytes[offset++];
    return true;
}

bool read_u16(const core::Bytes& bytes, std::size_t& offset, std::uint16_t& value)
{
    if (offset + 2U > bytes.size()) {
        return false;
    }
    value = static_cast<std::uint16_t>(bytes[offset])
        | (static_cast<std::uint16_t>(bytes[offset + 1U]) << 8U);
    offset += 2U;
    return true;
}

bool read_u32(const core::Bytes& bytes, std::size_t& offset, std::uint32_t& value)
{
    if (offset + 4U > bytes.size()) {
        return false;
    }
    value = 0;
    for (int shift = 0; shift < 32; shift += 8) {
        value |= static_cast<std::uint32_t>(bytes[offset++]) << shift;
    }
    return true;
}

bool read_u64(const core::Bytes& bytes, std::size_t& offset, std::uint64_t& value)
{
    if (offset + 8U > bytes.size()) {
        return false;
    }
    value = 0;
    for (int shift = 0; shift < 64; shift += 8) {
        value |= static_cast<std::uint64_t>(bytes[offset++]) << shift;
    }
    return true;
}

bool read_id(const core::Bytes& bytes, std::size_t& offset, core::Id128& id)
{
    if (offset + id.size() > bytes.size()) {
        return false;
    }
    std::copy_n(bytes.begin() + static_cast<std::ptrdiff_t>(offset), id.size(), id.begin());
    offset += id.size();
    return true;
}

bool read_bytes(const core::Bytes& bytes, std::size_t& offset, std::size_t count, core::Bytes& out)
{
    if (count > bytes.size() || offset > bytes.size() - count) {
        return false;
    }
    out.assign(
        bytes.begin() + static_cast<std::ptrdiff_t>(offset),
        bytes.begin() + static_cast<std::ptrdiff_t>(offset + count));
    offset += count;
    return true;
}

} // namespace

CimbarPayloadEncodeResult encode_cimbar_payload(const CimbarPayload& payload)
{
    if (payload.file_name.empty() || payload.file_name.size() > kMaxFileNameBytes) {
        return {CimbarPayloadError::InvalidName, {}, "file name is empty or too long"};
    }
    if (payload.file_bytes.size() > std::numeric_limits<std::uint64_t>::max()) {
        return {CimbarPayloadError::PayloadTooLarge, {}, "payload is too large"};
    }

    core::Bytes out;
    out.reserve(kMagic.size() + 1U + 4U + 32U + 32U + 8U + 2U + payload.file_name.size()
        + payload.file_bytes.size() + 4U);
    out.insert(out.end(), kMagic.begin(), kMagic.end());
    out.push_back(kVersion);
    append_u32(out, payload.text_message ? kFlagTextMessage : 0U);
    append_id(out, payload.session_id);
    append_id(out, payload.file_id);
    const auto digest = core::sha256(payload.file_bytes);
    out.insert(out.end(), digest.begin(), digest.end());
    append_u64(out, static_cast<std::uint64_t>(payload.file_bytes.size()));
    append_u16(out, static_cast<std::uint16_t>(payload.file_name.size()));
    out.insert(out.end(), payload.file_name.begin(), payload.file_name.end());
    out.insert(out.end(), payload.file_bytes.begin(), payload.file_bytes.end());
    append_u32(out, core::crc32(out));
    return {CimbarPayloadError::None, std::move(out), {}};
}

CimbarPayloadDecodeResult decode_cimbar_payload(const core::Bytes& bytes)
{
    if (bytes.size() < kMagic.size() + 1U + 4U + 32U + 32U + 8U + 2U + 4U) {
        return {CimbarPayloadError::Malformed, {}, "payload is too short"};
    }
    if (!std::equal(kMagic.begin(), kMagic.end(), bytes.begin())) {
        return {CimbarPayloadError::Malformed, {}, "payload magic does not match"};
    }

    const auto stored_crc_offset = bytes.size() - 4U;
    std::size_t crc_offset = stored_crc_offset;
    std::uint32_t stored_crc = 0;
    if (!read_u32(bytes, crc_offset, stored_crc)) {
        return {CimbarPayloadError::Malformed, {}, "payload crc is missing"};
    }

    core::Bytes crc_input(bytes.begin(), bytes.begin() + static_cast<std::ptrdiff_t>(stored_crc_offset));
    if (core::crc32(crc_input) != stored_crc) {
        return {CimbarPayloadError::Malformed, {}, "payload crc mismatch"};
    }

    std::size_t offset = kMagic.size();
    std::uint8_t version = 0;
    std::uint32_t flags = 0;
    CimbarPayload payload;
    core::Sha256 expected_hash{};
    std::uint64_t file_size = 0;
    std::uint16_t file_name_size = 0;

    if (!read_u8(bytes, offset, version) || !read_u32(bytes, offset, flags)
        || !read_id(bytes, offset, payload.session_id) || !read_id(bytes, offset, payload.file_id)) {
        return {CimbarPayloadError::Malformed, {}, "payload header is incomplete"};
    }
    if (version != kVersion) {
        return {CimbarPayloadError::UnsupportedVersion, {}, "unsupported cimbar payload version"};
    }

    if (offset + expected_hash.size() > bytes.size()) {
        return {CimbarPayloadError::Malformed, {}, "payload hash is incomplete"};
    }
    std::copy_n(
        bytes.begin() + static_cast<std::ptrdiff_t>(offset),
        expected_hash.size(),
        expected_hash.begin());
    offset += expected_hash.size();

    if (!read_u64(bytes, offset, file_size) || !read_u16(bytes, offset, file_name_size)) {
        return {CimbarPayloadError::Malformed, {}, "payload size fields are incomplete"};
    }
    if (file_name_size == 0 || file_name_size > kMaxFileNameBytes) {
        return {CimbarPayloadError::InvalidName, {}, "file name length is invalid"};
    }
    if (file_size > static_cast<std::uint64_t>(bytes.size())) {
        return {CimbarPayloadError::Malformed, {}, "declared file size is invalid"};
    }

    core::Bytes name_bytes;
    if (!read_bytes(bytes, offset, file_name_size, name_bytes)) {
        return {CimbarPayloadError::Malformed, {}, "file name is incomplete"};
    }
    payload.file_name.assign(name_bytes.begin(), name_bytes.end());
    payload.text_message = (flags & kFlagTextMessage) != 0U;

    if (!read_bytes(bytes, offset, static_cast<std::size_t>(file_size), payload.file_bytes)) {
        return {CimbarPayloadError::Malformed, {}, "file bytes are incomplete"};
    }
    if (offset != stored_crc_offset) {
        return {CimbarPayloadError::Malformed, {}, "payload has trailing bytes"};
    }
    if (core::sha256(payload.file_bytes) != expected_hash) {
        return {CimbarPayloadError::HashMismatch, {}, "SHA-256 mismatch"};
    }

    return {CimbarPayloadError::None, std::move(payload), {}};
}

const char* cimbar_payload_error_name(CimbarPayloadError error)
{
    switch (error) {
    case CimbarPayloadError::None:
        return "None";
    case CimbarPayloadError::InvalidName:
        return "InvalidName";
    case CimbarPayloadError::PayloadTooLarge:
        return "PayloadTooLarge";
    case CimbarPayloadError::Malformed:
        return "Malformed";
    case CimbarPayloadError::UnsupportedVersion:
        return "UnsupportedVersion";
    case CimbarPayloadError::HashMismatch:
        return "HashMismatch";
    }

    return "Unknown";
}

} // namespace aqrt::app
