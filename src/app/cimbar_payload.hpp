#pragma once

#include "frame.hpp"

#include <cstdint>
#include <string>

namespace aqrt::app {

enum class CimbarPayloadError {
    None,
    InvalidName,
    PayloadTooLarge,
    Malformed,
    UnsupportedVersion,
    HashMismatch,
};

struct CimbarPayload {
    core::Id128 session_id{};
    core::Id128 file_id{};
    std::string file_name;
    bool text_message = false;
    core::Bytes file_bytes;
};

struct CimbarPayloadEncodeResult {
    CimbarPayloadError error = CimbarPayloadError::None;
    core::Bytes bytes;
    std::string message;

    [[nodiscard]] bool ok() const { return error == CimbarPayloadError::None; }
};

struct CimbarPayloadDecodeResult {
    CimbarPayloadError error = CimbarPayloadError::None;
    CimbarPayload payload{};
    std::string message;

    [[nodiscard]] bool ok() const { return error == CimbarPayloadError::None; }
};

CimbarPayloadEncodeResult encode_cimbar_payload(const CimbarPayload& payload);
CimbarPayloadDecodeResult decode_cimbar_payload(const core::Bytes& bytes);
const char* cimbar_payload_error_name(CimbarPayloadError error);

} // namespace aqrt::app
