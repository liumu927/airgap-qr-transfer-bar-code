#pragma once

#include "frame.hpp"

#include <string>
#include <string_view>

namespace aqrt::core {

constexpr std::uint64_t kDefaultMaxFileSize = 64ULL * 1024ULL * 1024ULL;
constexpr std::uint16_t kMaxFileNameBytes = 255;

struct ChunkPlan {
    std::uint64_t file_size = 0;
    std::uint32_t chunk_size = 0;
    std::uint32_t total_chunks = 0;
};

enum class ChunkPlanError {
    None,
    EmptyFile,
    ZeroChunkSize,
    FileTooLarge,
    TooManyChunks,
};

struct ChunkPlanResult {
    ChunkPlanError error = ChunkPlanError::None;
    ChunkPlan plan{};

    [[nodiscard]] bool ok() const { return error == ChunkPlanError::None; }
};

enum class ManifestBuildError {
    None,
    ChunkPlanInvalid,
    EmptyFileName,
    FileNameTooLong,
    UnsafeFileName,
};

struct ManifestBuildResult {
    ManifestBuildError error = ManifestBuildError::None;
    ChunkPlanError chunk_plan_error = ChunkPlanError::None;
    ManifestFrame manifest{};

    [[nodiscard]] bool ok() const { return error == ManifestBuildError::None; }
};

bool is_safe_file_name(std::string_view file_name);
ChunkPlanResult plan_chunks(
    std::uint64_t file_size,
    std::uint32_t chunk_size,
    std::uint64_t max_file_size = kDefaultMaxFileSize);
ManifestBuildResult build_manifest(
    Id128 session_id,
    std::string file_name,
    const Bytes& file_bytes,
    std::uint32_t chunk_size,
    std::uint64_t max_file_size = kDefaultMaxFileSize,
    std::uint32_t manifest_flags = 0);
ManifestFrame make_manifest(
    Id128 session_id,
    Id128 file_id,
    std::string file_name,
    const Bytes& file_bytes,
    std::uint32_t chunk_size,
    std::uint32_t manifest_flags = 0);
std::vector<DataFrame> make_data_frames(const ManifestFrame& manifest, const Bytes& file_bytes);
EndFrame make_end_frame(const ManifestFrame& manifest);

const char* chunk_plan_error_name(ChunkPlanError error);
const char* manifest_build_error_name(ManifestBuildError error);

} // namespace aqrt::core
