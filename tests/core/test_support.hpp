#pragma once

#include "frame.hpp"

#include <cstdlib>
#include <iostream>
#include <string_view>

namespace aqrt::core::test {

inline void require(bool condition, std::string_view message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

inline Id128 id_from_seed(std::uint8_t seed)
{
    Id128 id{};
    for (std::size_t i = 0; i < id.size(); ++i) {
        id[i] = static_cast<std::uint8_t>(seed + i);
    }
    return id;
}

inline Sha256 sha_from_seed(std::uint8_t seed)
{
    Sha256 hash{};
    for (std::size_t i = 0; i < hash.size(); ++i) {
        hash[i] = static_cast<std::uint8_t>(seed + i);
    }
    return hash;
}

} // namespace aqrt::core::test
