#pragma once

#include "chunker.hpp"
#include "frame.hpp"
#include "qr_adapter.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace aqrt::app {

constexpr std::uint32_t kDefaultSendChunkSize = 128;
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

// 解析缺失帧号规格，如 "3,7,15" 或 "3-5,9"（1-based 播放序号，逗号分隔，允许空格）。
// data_frame_count 为数据帧总数（发送端 all_qr_frames_ 的大小）。
// 合法帧号范围 [1, data_frame_count]；内部转换为 0-based chunk_index。
// 返回去重、升序后的 chunk_index 列表；空规格合法（表示仅重传 manifest 与 end）。
// 解析失败时填充 error 并返回空列表。
std::vector<std::uint32_t> parse_frame_spec(
    std::string_view spec,
    std::uint32_t data_frame_count,
    std::string& error);

} // namespace aqrt::app
