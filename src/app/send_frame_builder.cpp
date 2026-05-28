#include "send_frame_builder.hpp"

#include <utility>

namespace aqrt::app {

SendBuildResult build_send_package(
    core::Id128 session_id,
    std::string file_name,
    const core::Bytes& file_bytes,
    std::uint32_t chunk_size,
    const qr::IQrEncoder& encoder,
    std::uint32_t manifest_flags)
{
    const auto manifest_result = core::build_manifest(
        session_id,
        std::move(file_name),
        file_bytes,
        chunk_size,
        core::kDefaultMaxFileSize,
        manifest_flags);
    if (!manifest_result.ok()) {
        return {
            SendBuildError::ManifestInvalid,
            manifest_result.error,
            manifest_result.chunk_plan_error,
            qr::QrError::None,
            core::manifest_build_error_name(manifest_result.error),
            {},
        };
    }

    std::vector<core::Frame> frames;
    frames.reserve(static_cast<std::size_t>(manifest_result.manifest.total_chunks) + 2U);
    frames.emplace_back(manifest_result.manifest);
    for (const auto& data_frame : core::make_data_frames(manifest_result.manifest, file_bytes)) {
        frames.emplace_back(data_frame);
    }
    frames.emplace_back(core::make_end_frame(manifest_result.manifest));

    SendPackage package;
    package.manifest = manifest_result.manifest;
    package.payloads.reserve(frames.size());
    package.qr_frames.reserve(frames.size());

    for (const auto& frame : frames) {
        auto payload = core::encode_frame(frame);
        const auto encoded = encoder.encode(payload);
        if (!encoded.ok()) {
            return {
                SendBuildError::QrEncodeFailed,
                core::ManifestBuildError::None,
                core::ChunkPlanError::None,
                encoded.error,
                encoded.message,
                {},
            };
        }

        package.payloads.push_back(std::move(payload));
        package.qr_frames.push_back(encoded.image);
    }

    return {
        SendBuildError::None,
        core::ManifestBuildError::None,
        core::ChunkPlanError::None,
        qr::QrError::None,
        {},
        std::move(package),
    };
}

const char* send_build_error_name(SendBuildError error)
{
    switch (error) {
    case SendBuildError::None:
        return "None";
    case SendBuildError::ManifestInvalid:
        return "ManifestInvalid";
    case SendBuildError::QrEncodeFailed:
        return "QrEncodeFailed";
    }

    return "Unknown";
}

} // namespace aqrt::app
