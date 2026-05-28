#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace aqrt::core {

std::uint32_t crc32(std::span<const std::uint8_t> bytes);

} // namespace aqrt::core
