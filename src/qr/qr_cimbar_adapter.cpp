#include "qr_cimbar_adapter.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>

#if AIRGAP_HAS_CIMBAR
#include "cimbar_js/cimbar_recv_js.h"
#include "cimb_translator/Config.h"
#include "encoder/EncoderPlus.h"
#include "fountain/FountainInit.h"

#include <opencv2/imgproc.hpp>
#endif

namespace aqrt::qr {
namespace {

std::filesystem::path make_temp_path(const std::string& suffix)
{
    const auto base = std::filesystem::temp_directory_path();
    const auto seed = std::to_string(
        static_cast<unsigned long long>(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    return base / ("airgap-cimbar-" + seed + suffix);
}

} // namespace

const char* cimbar_error_name(CimbarError error)
{
    switch (error) {
    case CimbarError::None:
        return "None";
    case CimbarError::BackendUnavailable:
        return "BackendUnavailable";
    case CimbarError::InvalidInput:
        return "InvalidInput";
    case CimbarError::EncodeFailed:
        return "EncodeFailed";
    case CimbarError::DecodeFailed:
        return "DecodeFailed";
    case CimbarError::Incomplete:
        return "Incomplete";
    }

    return "Unknown";
}

bool cimbar_backend_available()
{
#if AIRGAP_HAS_CIMBAR
    return true;
#else
    return false;
#endif
}

CimbarEncodeResult CimbarFileEncoder::encode_bytes(
    const std::string& file_name,
    const Bytes& file_bytes,
    const CimbarEncodeOptions& options) const
{
#if AIRGAP_HAS_CIMBAR
    if (file_name.empty() || file_bytes.empty()) {
        return {CimbarError::InvalidInput, {}, "file name and payload must not be empty"};
    }

    const auto temp_path = make_temp_path(".bin");
    struct TempFileGuard {
        std::filesystem::path path;
        ~TempFileGuard()
        {
            std::error_code ignored;
            std::filesystem::remove(path, ignored);
        }
    } guard{temp_path};

    {
        std::ofstream out(temp_path, std::ios::binary | std::ios::trunc);
        if (!out) {
            return {CimbarError::EncodeFailed, {}, "could not create cimbar staging file"};
        }
        out.write(reinterpret_cast<const char*>(file_bytes.data()), static_cast<std::streamsize>(file_bytes.size()));
        if (!out) {
            return {CimbarError::EncodeFailed, {}, "could not write cimbar staging file"};
        }
    }

    if (!FountainInit::init()) {
        return {CimbarError::EncodeFailed, {}, "could not initialize fountain encoder"};
    }

    cimbar::Config::update(options.mode);
    EncoderPlus encoder;
    encoder.set_encode_id(options.encode_id);

    std::vector<RgbImage> frames;
    const auto on_frame = [&frames](const cv::Mat& frame, unsigned) {
        if (frame.empty() || frame.cols <= 0 || frame.rows <= 0 || frame.channels() != 3) {
            return false;
        }

        cv::Mat rgb;
        if (frame.isContinuous()) {
            rgb = frame;
        } else {
            rgb = frame.clone();
        }

        RgbImage image;
        image.width = static_cast<std::uint32_t>(rgb.cols);
        image.height = static_cast<std::uint32_t>(rgb.rows);
        image.rgb.assign(rgb.data, rgb.data + rgb.total() * rgb.elemSize());
        frames.push_back(std::move(image));
        return true;
    };

    const unsigned count = encoder.encode_fountain(
        temp_path.string(),
        on_frame,
        options.compression_level,
        options.redundancy);
    if (count == 0 || frames.empty()) {
        return {CimbarError::EncodeFailed, {}, "libcimbar did not produce frames"};
    }

    return {CimbarError::None, std::move(frames), {}};
#else
    (void)file_name;
    (void)file_bytes;
    (void)options;
    return {CimbarError::BackendUnavailable, {}, "libcimbar support is not enabled in this build"};
#endif
}

CimbarFileDecoder::CimbarFileDecoder(int mode)
    : mode_(mode)
{
    reset(mode_);
}

CimbarFileDecoder::~CimbarFileDecoder() = default;

void CimbarFileDecoder::reset(int mode)
{
    mode_ = mode;
#if AIRGAP_HAS_CIMBAR
    cimbard_configure_decode(mode_);
#endif
}

CimbarDecodeResult CimbarFileDecoder::decode_rgb(const RgbImage& image)
{
#if AIRGAP_HAS_CIMBAR
    if (!is_valid_rgb_image_shape(image)) {
        return {CimbarError::InvalidInput, false, {}, {}, "invalid RGB image"};
    }

    const int buffer_size = cimbard_get_bufsize();
    if (buffer_size <= 0) {
        return {CimbarError::DecodeFailed, false, {}, {}, "invalid cimbar decode buffer size"};
    }

    std::vector<std::uint8_t> buffer(static_cast<std::size_t>(buffer_size));
    const int decoded_size = cimbard_scan_extract_decode(
        image.rgb.data(),
        image.width,
        image.height,
        3,
        buffer.data(),
        static_cast<unsigned>(buffer.size()));
    if (decoded_size <= 0) {
        return {CimbarError::Incomplete, false, {}, {}, "no cimbar frame decoded"};
    }

    const auto fountain_result = cimbard_fountain_decode(buffer.data(), static_cast<unsigned>(decoded_size));
    if (fountain_result == 0) {
        return {CimbarError::Incomplete, false, {}, {}, "cimbar transfer still incomplete"};
    }
    if (fountain_result < 0 || fountain_result > std::numeric_limits<std::uint32_t>::max()) {
        return {CimbarError::DecodeFailed, false, {}, {}, "cimbar fountain decode failed"};
    }

    const auto file_id = static_cast<std::uint32_t>(fountain_result);
    std::string file_name(4096, '\0');
    const int name_size = cimbard_get_filename(file_id, file_name.data(), static_cast<unsigned>(file_name.size()));
    if (name_size < 0) {
        return {CimbarError::DecodeFailed, false, {}, {}, "could not recover cimbar file name"};
    }
    file_name.resize(static_cast<std::size_t>(std::max(name_size, 0)));

    const int chunk_size = cimbard_get_decompress_bufsize();
    if (chunk_size <= 0) {
        return {CimbarError::DecodeFailed, false, {}, {}, "invalid cimbar decompression buffer size"};
    }

    Bytes file_bytes;
    std::vector<std::uint8_t> chunk(static_cast<std::size_t>(chunk_size));
    while (true) {
        const int read = cimbard_decompress_read(file_id, chunk.data(), static_cast<unsigned>(chunk.size()));
        if (read < 0) {
            return {CimbarError::DecodeFailed, false, {}, {}, "could not decompress cimbar payload"};
        }
        if (read == 0) {
            break;
        }
        file_bytes.insert(file_bytes.end(), chunk.begin(), chunk.begin() + read);
    }

    return {CimbarError::None, true, std::move(file_name), std::move(file_bytes), {}};
#else
    (void)image;
    return {CimbarError::BackendUnavailable, false, {}, {}, "libcimbar support is not enabled in this build"};
#endif
}

} // namespace aqrt::qr
