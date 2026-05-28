#pragma once

#include "frame.hpp"

#include <string>
#include <string_view>

namespace aqrt::core {

Id128 generate_session_id();
Id128 derive_file_id(
    const Id128& session_id,
    std::uint64_t file_size,
    const Sha256& file_sha256,
    std::string_view file_name);
std::string id_to_hex(const Id128& id);

} // namespace aqrt::core
