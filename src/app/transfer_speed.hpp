#pragma once

#include <array>
#include <cstdint>

namespace aqrt::app {

struct TransferSpeedProfile {
    const char* name;
    std::uint32_t chunk_size;
    int playback_interval_ms;
    int decode_interval_ms;
};

inline constexpr int kDefaultTransferSpeedMode = 0;

inline constexpr std::array<TransferSpeedProfile, 3> kTransferSpeedProfiles{{
    {"Safe", 512, 250, 150},
    {"Balanced", 768, 200, 120},
    {"Fast", 896, 150, 100},
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

} // namespace aqrt::app
