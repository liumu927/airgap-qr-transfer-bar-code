#pragma once

#include "qr_adapter.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace aqrt::qr {

enum class CimbarError {
    None,
    BackendUnavailable,
    InvalidInput,
    EncodeFailed,
    DecodeFailed,
    Incomplete,
};

struct CimbarEncodeOptions {
    int mode = 68;
    int compression_level = 16;
    double redundancy = 1.35;
    std::uint8_t encode_id = 109;
};

struct CimbarEncodeResult {
    CimbarError error = CimbarError::None;
    std::vector<RgbImage> frames;
    std::string message;

    [[nodiscard]] bool ok() const { return error == CimbarError::None; }
};

struct CimbarDecodeResult {
    CimbarError error = CimbarError::Incomplete;
    bool completed = false;
    std::string file_name;
    Bytes file_bytes;
    std::string message;

    [[nodiscard]] bool ok() const { return error == CimbarError::None || error == CimbarError::Incomplete; }
};

class CimbarFileEncoder {
public:
    CimbarEncodeResult encode_bytes(
        const std::string& file_name,
        const Bytes& file_bytes,
        const CimbarEncodeOptions& options = {}) const;
};

class CimbarFileDecoder {
public:
    explicit CimbarFileDecoder(int mode = 68);
    ~CimbarFileDecoder();

    CimbarFileDecoder(const CimbarFileDecoder&) = delete;
    CimbarFileDecoder& operator=(const CimbarFileDecoder&) = delete;

    void reset(int mode = 68);
    CimbarDecodeResult decode_rgb(const RgbImage& image);

private:
    int mode_ = 68;
};

const char* cimbar_error_name(CimbarError error);
bool cimbar_backend_available();

} // namespace aqrt::qr
