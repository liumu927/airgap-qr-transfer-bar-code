#pragma once

#include "frame.hpp"

#include <optional>

namespace aqrt::core {

enum class AssemblerError {
    None,
    MissingManifest,
    SessionMismatch,
    FileMismatch,
    ManifestConflict,
    EndConflict,
    InvalidChunkIndex,
    InvalidChunkOffset,
    InvalidChunkSize,
    DuplicateChunkConflict,
    Incomplete,
    SizeMismatch,
    Sha256Mismatch,
};

struct AddFrameResult {
    AssemblerError error = AssemblerError::None;
    bool accepted = false;
    bool duplicate = false;

    [[nodiscard]] bool ok() const { return error == AssemblerError::None; }
};

struct AssembleResult {
    AssemblerError error = AssemblerError::None;
    Bytes data;

    [[nodiscard]] bool ok() const { return error == AssemblerError::None; }
};

class ReceiverAssembler {
public:
    AddFrameResult add_manifest(const ManifestFrame& manifest);
    AddFrameResult add_data(const DataFrame& data);
    AddFrameResult add_end(const EndFrame& end);

    [[nodiscard]] bool has_manifest() const;
    [[nodiscard]] bool has_end() const;
    [[nodiscard]] bool complete() const;
    [[nodiscard]] std::uint32_t received_chunks() const;
    [[nodiscard]] std::uint32_t total_chunks() const;
    [[nodiscard]] std::optional<ManifestFrame> manifest() const;
    [[nodiscard]] std::vector<MissingRange> missing_ranges() const;

    AssembleResult assemble() const;

private:
    bool matches_session_file(const Id128& session_id, const Id128& file_id) const;
    std::uint64_t expected_offset(std::uint32_t chunk_index) const;
    std::uint32_t expected_chunk_size(std::uint32_t chunk_index) const;

    std::optional<ManifestFrame> manifest_;
    std::optional<EndFrame> end_;
    std::vector<Bytes> chunks_;
    std::vector<bool> present_;
    std::uint32_t received_chunks_ = 0;
};

const char* assembler_error_name(AssemblerError error);

} // namespace aqrt::core
