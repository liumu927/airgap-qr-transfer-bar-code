#pragma once

#include "chunker.hpp"
#include "frame.hpp"
#include "qr_adapter.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace aqrt::app {

constexpr std::uint32_t kDefaultSendChunkSize = 256;
constexpr std::size_t kDefaultQrPayloadLimit = 1024;

enum class SendBuildError {
    None,
    ManifestInvalid,
    QrEncodeFailed,
};

struct SendPackage {
    core::ManifestFrame manifest{};
    std::vector<core::Bytes> payloads;
    std::vector<qr::QrImage> qr_frames;
};

struct SendBuildResult {
    SendBuildError error = SendBuildError::None;
    core::ManifestBuildError manifest_error = core::ManifestBuildError::None;
    core::ChunkPlanError chunk_plan_error = core::ChunkPlanError::None;
    qr::QrError qr_error = qr::QrError::None;
    std::string message;
    SendPackage package{};

    [[nodiscard]] bool ok() const { return error == SendBuildError::None; }
};

SendBuildResult build_send_package(
    core::Id128 session_id,
    std::string file_name,
    const core::Bytes& file_bytes,
    std::uint32_t chunk_size,
    const qr::IQrEncoder& encoder,
    std::uint32_t manifest_flags = 0);

const char* send_build_error_name(SendBuildError error);

} // namespace aqrt::app
