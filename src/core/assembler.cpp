#include "assembler.hpp"

#include "sha256.hpp"

#include <algorithm>

namespace aqrt::core {
namespace {

bool manifest_same_identity_and_shape(const ManifestFrame& left, const ManifestFrame& right)
{
    return left.session_id == right.session_id
        && left.file_id == right.file_id
        && left.manifest_flags == right.manifest_flags
        && left.file_size == right.file_size
        && left.chunk_size == right.chunk_size
        && left.total_chunks == right.total_chunks
        && left.file_sha256 == right.file_sha256
        && left.file_name == right.file_name;
}

} // namespace

AddFrameResult ReceiverAssembler::add_manifest(const ManifestFrame& manifest)
{
    if (manifest_) {
        if (manifest_same_identity_and_shape(*manifest_, manifest)) {
            return {AssemblerError::None, true, true};
        }
        return {AssemblerError::ManifestConflict, false, false};
    }

    manifest_ = manifest;
    chunks_.assign(manifest.total_chunks, {});
    present_.assign(manifest.total_chunks, false);
    received_chunks_ = 0;
    return {AssemblerError::None, true, false};
}

AddFrameResult ReceiverAssembler::add_data(const DataFrame& data)
{
    if (!manifest_) {
        return {AssemblerError::MissingManifest, false, false};
    }
    if (!matches_session_file(data.session_id, data.file_id)) {
        return data.session_id != manifest_->session_id
            ? AddFrameResult{AssemblerError::SessionMismatch, false, false}
            : AddFrameResult{AssemblerError::FileMismatch, false, false};
    }
    if (data.total_chunks != manifest_->total_chunks || data.chunk_index >= manifest_->total_chunks) {
        return {AssemblerError::InvalidChunkIndex, false, false};
    }
    if (data.chunk_offset != expected_offset(data.chunk_index)) {
        return {AssemblerError::InvalidChunkOffset, false, false};
    }
    if (data.data.size() != expected_chunk_size(data.chunk_index)) {
        return {AssemblerError::InvalidChunkSize, false, false};
    }

    if (present_[data.chunk_index]) {
        if (chunks_[data.chunk_index] == data.data) {
            return {AssemblerError::None, true, true};
        }
        return {AssemblerError::DuplicateChunkConflict, false, false};
    }

    chunks_[data.chunk_index] = data.data;
    present_[data.chunk_index] = true;
    ++received_chunks_;
    return {AssemblerError::None, true, false};
}

AddFrameResult ReceiverAssembler::add_end(const EndFrame& end)
{
    if (!manifest_) {
        return {AssemblerError::MissingManifest, false, false};
    }
    if (!matches_session_file(end.session_id, end.file_id)) {
        return end.session_id != manifest_->session_id
            ? AddFrameResult{AssemblerError::SessionMismatch, false, false}
            : AddFrameResult{AssemblerError::FileMismatch, false, false};
    }
    if (end.total_chunks != manifest_->total_chunks
        || end.file_size != manifest_->file_size
        || end.file_sha256 != manifest_->file_sha256) {
        return {AssemblerError::EndConflict, false, false};
    }

    if (end_) {
        return {AssemblerError::None, true, true};
    }

    end_ = end;
    return {AssemblerError::None, true, false};
}

bool ReceiverAssembler::has_manifest() const
{
    return manifest_.has_value();
}

bool ReceiverAssembler::has_end() const
{
    return end_.has_value();
}

bool ReceiverAssembler::complete() const
{
    return manifest_ && received_chunks_ == manifest_->total_chunks;
}

std::uint32_t ReceiverAssembler::received_chunks() const
{
    return received_chunks_;
}

std::uint32_t ReceiverAssembler::total_chunks() const
{
    return manifest_ ? manifest_->total_chunks : 0;
}

std::optional<ManifestFrame> ReceiverAssembler::manifest() const
{
    return manifest_;
}

std::vector<MissingRange> ReceiverAssembler::missing_ranges() const
{
    std::vector<MissingRange> ranges;
    if (!manifest_) {
        return ranges;
    }

    std::uint32_t index = 0;
    while (index < manifest_->total_chunks) {
        while (index < manifest_->total_chunks && present_[index]) {
            ++index;
        }
        if (index >= manifest_->total_chunks) {
            break;
        }

        const std::uint32_t start = index;
        while (index < manifest_->total_chunks && !present_[index]) {
            ++index;
        }
        ranges.push_back(MissingRange{start, index - start});
    }

    return ranges;
}

AssembleResult ReceiverAssembler::assemble() const
{
    if (!manifest_) {
        return {AssemblerError::MissingManifest, {}};
    }
    if (!complete()) {
        return {AssemblerError::Incomplete, {}};
    }

    Bytes out;
    out.reserve(static_cast<std::size_t>(manifest_->file_size));
    for (const auto& chunk : chunks_) {
        out.insert(out.end(), chunk.begin(), chunk.end());
    }

    if (out.size() != manifest_->file_size) {
        return {AssemblerError::SizeMismatch, {}};
    }
    if (sha256(out) != manifest_->file_sha256) {
        return {AssemblerError::Sha256Mismatch, {}};
    }

    return {AssemblerError::None, std::move(out)};
}

bool ReceiverAssembler::matches_session_file(const Id128& session_id, const Id128& file_id) const
{
    return manifest_ && manifest_->session_id == session_id && manifest_->file_id == file_id;
}

std::uint64_t ReceiverAssembler::expected_offset(std::uint32_t chunk_index) const
{
    return static_cast<std::uint64_t>(chunk_index) * manifest_->chunk_size;
}

std::uint32_t ReceiverAssembler::expected_chunk_size(std::uint32_t chunk_index) const
{
    if (chunk_index + 1U < manifest_->total_chunks) {
        return manifest_->chunk_size;
    }

    const auto consumed = static_cast<std::uint64_t>(chunk_index) * manifest_->chunk_size;
    return static_cast<std::uint32_t>(manifest_->file_size - consumed);
}

const char* assembler_error_name(AssemblerError error)
{
    switch (error) {
    case AssemblerError::None:
        return "None";
    case AssemblerError::MissingManifest:
        return "MissingManifest";
    case AssemblerError::SessionMismatch:
        return "SessionMismatch";
    case AssemblerError::FileMismatch:
        return "FileMismatch";
    case AssemblerError::ManifestConflict:
        return "ManifestConflict";
    case AssemblerError::EndConflict:
        return "EndConflict";
    case AssemblerError::InvalidChunkIndex:
        return "InvalidChunkIndex";
    case AssemblerError::InvalidChunkOffset:
        return "InvalidChunkOffset";
    case AssemblerError::InvalidChunkSize:
        return "InvalidChunkSize";
    case AssemblerError::DuplicateChunkConflict:
        return "DuplicateChunkConflict";
    case AssemblerError::Incomplete:
        return "Incomplete";
    case AssemblerError::SizeMismatch:
        return "SizeMismatch";
    case AssemblerError::Sha256Mismatch:
        return "Sha256Mismatch";
    }

    return "Unknown";
}

} // namespace aqrt::core
