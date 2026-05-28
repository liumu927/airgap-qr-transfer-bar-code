#pragma once

#include "frame.hpp"

#include <span>
#include <string>

namespace aqrt::core {

Sha256 sha256(std::span<const std::uint8_t> bytes);
Sha256 sha256(const Bytes& bytes);
std::string sha256_hex(const Sha256& digest);

} // namespace aqrt::core
