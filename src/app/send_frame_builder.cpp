#include "send_frame_builder.hpp"

#include <algorithm>
#include <limits>
#include <string>
#include <string_view>
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

namespace {

// 去除 token 首尾的空格与制表符
void trim_token(std::string_view& token)
{
    while (!token.empty() && (token.front() == ' ' || token.front() == '\t')) {
        token.remove_prefix(1);
    }
    while (!token.empty() && (token.back() == ' ' || token.back() == '\t')) {
        token.remove_suffix(1);
    }
}

// 将纯数字文本解析为 uint32，含溢出保护；非法字符返回 false
bool parse_frame_number(std::string_view token, std::uint32_t& out)
{
    if (token.empty()) {
        return false;
    }
    std::uint32_t value = 0;
    for (const char c : token) {
        if (c < '0' || c > '9') {
            return false;
        }
        const unsigned digit = static_cast<unsigned>(c - '0');
        if (value > (std::numeric_limits<std::uint32_t>::max() - digit) / 10U) {
            return false;  // 溢出
        }
        value = value * 10U + digit;
    }
    out = value;
    return true;
}

// 处理单个 token（单帧 "3" 或区间 "3-5"），展开为 0-based chunk_index 追加到 out。
// 空段（连续逗号/首尾逗号产生的空 token）忽略；非法填充 error 并返回 false。
bool append_token_frames(
    std::string_view token,
    std::uint32_t data_frame_count,
    std::vector<std::uint32_t>& out,
    std::string& error)
{
    trim_token(token);
    if (token.empty()) {
        return true;
    }

    const std::size_t dash = token.find('-');
    std::uint32_t lo = 0;
    std::uint32_t hi = 0;
    if (dash == std::string_view::npos) {
        if (!parse_frame_number(token, lo)) {
            error = "invalid frame number: \"" + std::string(token) + "\"";
            return false;
        }
        hi = lo;
    } else {
        std::string_view lo_part = token.substr(0, dash);
        std::string_view hi_part = token.substr(dash + 1);
        // 区间内只允许一个 '-'（避免 "1-2-3" 之类）
        if (hi_part.find('-') != std::string_view::npos) {
            error = "invalid range: \"" + std::string(token) + "\"";
            return false;
        }
        trim_token(lo_part);
        trim_token(hi_part);
        if (!parse_frame_number(lo_part, lo) || !parse_frame_number(hi_part, hi)) {
            error = "invalid range: \"" + std::string(token) + "\"";
            return false;
        }
        if (lo > hi) {
            error = "reverse range: \"" + std::string(token) + "\"";
            return false;
        }
    }

    if (lo < 1U || hi > data_frame_count) {
        error = "frame number out of range [1, " + std::to_string(data_frame_count)
                + "]: \"" + std::string(token) + "\"";
        return false;
    }

    for (std::uint32_t frame = lo; frame <= hi; ++frame) {
        out.push_back(frame - 1U);  // 转 0-based chunk_index
    }
    return true;
}

} // namespace

std::vector<std::uint32_t> parse_frame_spec(
    std::string_view spec,
    std::uint32_t data_frame_count,
    std::string& error)
{
    error.clear();
    std::vector<std::uint32_t> indices;

    // 无数据帧时只接受空规格（仅重传 manifest + end）
    if (data_frame_count == 0U) {
        if (spec.empty()) {
            return indices;
        }
        error = "no data frames to resend; leave blank for manifest and end only";
        return indices;
    }

    // 按逗号逐段解析
    std::size_t cursor = 0;
    while (cursor <= spec.size()) {
        const std::size_t comma = spec.find(',', cursor);
        const std::string_view token = (comma == std::string_view::npos)
            ? spec.substr(cursor)
            : spec.substr(cursor, comma - cursor);
        if (!append_token_frames(token, data_frame_count, indices, error)) {
            return {};
        }
        if (comma == std::string_view::npos) {
            break;
        }
        cursor = comma + 1;
    }

    // 去重并升序排列（与发送端 all_qr_frames_ 的下标顺序一致）
    std::sort(indices.begin(), indices.end());
    indices.erase(std::unique(indices.begin(), indices.end()), indices.end());
    return indices;
}

} // namespace aqrt::app
