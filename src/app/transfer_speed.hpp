#pragma once

#include <array>
#include <cstdint>

#ifndef AIRGAP_HAS_CIMBAR
#define AIRGAP_HAS_CIMBAR 0
#endif

namespace aqrt::app {

enum class VisualTransferCodec {
    Qr,
    Cimbar,
};

struct TransferSpeedProfile {
    const char* name;
    std::uint32_t chunk_size;
    int playback_interval_ms;
    int decode_interval_ms;
    VisualTransferCodec codec;
};

inline constexpr int kDefaultTransferSpeedMode = 0;

inline constexpr std::array<TransferSpeedProfile, AIRGAP_HAS_CIMBAR ? 5U : 4U> kTransferSpeedProfiles{{
    {"Safe", 128, 450, 220, VisualTransferCodec::Qr},
    {"Balanced", 256, 300, 180, VisualTransferCodec::Qr},
    {"Fast", 384, 240, 150, VisualTransferCodec::Qr},
    {"Dense", 1024, 450, 220, VisualTransferCodec::Qr},
#if AIRGAP_HAS_CIMBAR
    {"Cimbar", 0, 90, 120, VisualTransferCodec::Cimbar},
#endif
}};

inline int normalize_transfer_speed_mode(int mode)
{
    if (mode < 0 || mode >= static_cast<int>(kTransferSpeedProfiles.size())) {
        return kDefaultTransferSpeedMode;
    }
    return mode;
}

inline const TransferSpeedProfile& transfer_speed_profile(int mode)
{
    return kTransferSpeedProfiles[static_cast<std::size_t>(normalize_transfer_speed_mode(mode))];
}

inline bool transfer_speed_uses_cimbar(int mode)
{
    return transfer_speed_profile(mode).codec == VisualTransferCodec::Cimbar;
}

} // namespace aqrt::app
