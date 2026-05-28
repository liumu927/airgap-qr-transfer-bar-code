#include "chunker.hpp"

#include "session.hpp"
#include "sha256.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>

namespace aqrt::core {

bool is_safe_file_name(std::string_view file_name)
{
    if (file_name.empty() || file_name.size() > kMaxFileNameBytes) {
        return false;
    }

    for (const unsigned char ch : file_name) {
        if (ch < 0x20U || ch == '/' || ch == '\\' || ch == ':') {
            return false;
        }
    }
    return true;
}

ChunkPlanResult plan_chunks(std::uint64_t file_size, std::uint32_t chunk_size, std::uint64_t max_file_size)
{
    if (file_size == 0) {
        return {ChunkPlanError::EmptyFile, {}};
    }
    if (chunk_size == 0) {
        return {ChunkPlanError::ZeroChunkSize, {}};
    }
    if (file_size > max_file_size) {
        return {ChunkPlanError::FileTooLarge, {}};
    }

    const auto total = (file_size + chunk_size - 1U) / chunk_size;
    if (total > std::numeric_limits<std::uint32_t>::max()) {
        return {ChunkPlanError::TooManyChunks, {}};
    }

    return {ChunkPlanError::None, ChunkPlan{file_size, chunk_size, static_cast<std::uint32_t>(total)}};
}

ManifestBuildResult build_manifest(
    Id128 session_id,
    std::string file_name,
    const Bytes& file_bytes,
    std::uint32_t chunk_size,
    std::uint64_t max_file_size,
    std::uint32_t manifest_flags)
{
    const auto plan = plan_chunks(file_bytes.size(), chunk_size, max_file_size);
    if (!plan.ok()) {
        return {ManifestBuildError::ChunkPlanInvalid, plan.error, {}};
    }
    if (file_name.empty()) {
        return {ManifestBuildError::EmptyFileName, ChunkPlanError::None, {}};
    }
    if (file_name.size() > kMaxFileNameBytes) {
        return {ManifestBuildError::FileNameTooLong, ChunkPlanError::None, {}};
    }
    if (!is_safe_file_name(file_name)) {
        return {ManifestBuildError::UnsafeFileName, ChunkPlanError::None, {}};
    }

    ManifestFrame manifest;
    manifest.session_id = session_id;
    manifest.manifest_flags = manifest_flags;
    manifest.file_size = plan.plan.file_size;
    manifest.chunk_size = plan.plan.chunk_size;
    manifest.total_chunks = plan.plan.total_chunks;
    manifest.file_sha256 = sha256(file_bytes);
    manifest.file_name = std::move(file_name);
    manifest.file_id = derive_file_id(
        manifest.session_id,
        manifest.file_size,
        manifest.file_sha256,
        manifest.file_name);
    return {ManifestBuildError::None, ChunkPlanError::None, std::move(manifest)};
}

ManifestFrame make_manifest(
    Id128 session_id,
    Id128 file_id,
    std::string file_name,
    const Bytes& file_bytes,
    std::uint32_t chunk_size,
    std::uint32_t manifest_flags)
{
    const auto plan = plan_chunks(file_bytes.size(), chunk_size);
    if (!plan.ok()) {
        throw std::invalid_argument(chunk_plan_error_name(plan.error));
    }

    ManifestFrame manifest;
    manifest.session_id = session_id;
    manifest.file_id = file_id;
    manifest.manifest_flags = manifest_flags;
    manifest.file_size = plan.plan.file_size;
    manifest.chunk_size = plan.plan.chunk_size;
    manifest.total_chunks = plan.plan.total_chunks;
    manifest.file_sha256 = sha256(file_bytes);
    manifest.file_name = std::move(file_name);
    return manifest;
}

std::vector<DataFrame> make_data_frames(const ManifestFrame& manifest, const Bytes& file_bytes)
{
    std::vector<DataFrame> frames;
    frames.reserve(manifest.total_chunks);

    for (std::uint32_t index = 0; index < manifest.total_chunks; ++index) {
        const auto offset = static_cast<std::uint64_t>(index) * manifest.chunk_size;
        const auto remaining = static_cast<std::uint64_t>(file_bytes.size()) - offset;
        const auto size = static_cast<std::size_t>(std::min<std::uint64_t>(manifest.chunk_size, remaining));

        DataFrame frame;
        frame.session_id = manifest.session_id;
        frame.file_id = manifest.file_id;
        frame.chunk_index = index;
        frame.total_chunks = manifest.total_chunks;
        frame.chunk_offset = offset;
        frame.data.assign(file_bytes.begin() + static_cast<std::ptrdiff_t>(offset),
            file_bytes.begin() + static_cast<std::ptrdiff_t>(offset + size));
        frames.push_back(std::move(frame));
    }

    return frames;
}

EndFrame make_end_frame(const ManifestFrame& manifest)
{
    EndFrame end;
    end.session_id = manifest.session_id;
    end.file_id = manifest.file_id;
    end.total_chunks = manifest.total_chunks;
    end.file_size = manifest.file_size;
    end.file_sha256 = manifest.file_sha256;
    return end;
}

const char* chunk_plan_error_name(ChunkPlanError error)
{
    switch (error) {
    case ChunkPlanError::None:
        return "None";
    case ChunkPlanError::EmptyFile:
        return "EmptyFile";
    case ChunkPlanError::ZeroChunkSize:
        return "ZeroChunkSize";
    case ChunkPlanError::FileTooLarge:
        return "FileTooLarge";
    case ChunkPlanError::TooManyChunks:
        return "TooManyChunks";
    }

    return "Unknown";
}

const char* manifest_build_error_name(ManifestBuildError error)
{
    switch (error) {
    case ManifestBuildError::None:
        return "None";
    case ManifestBuildError::ChunkPlanInvalid:
        return "ChunkPlanInvalid";
    case ManifestBuildError::EmptyFileName:
        return "EmptyFileName";
    case ManifestBuildError::FileNameTooLong:
        return "FileNameTooLong";
    case ManifestBuildError::UnsafeFileName:
        return "UnsafeFileName";
    }

    return "Unknown";
}

} // namespace aqrt::core
