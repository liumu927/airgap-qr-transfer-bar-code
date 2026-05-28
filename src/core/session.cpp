#include "session.hpp"

#include "sha256.hpp"

#include <algorithm>
#include <random>

namespace aqrt::core {
namespace {

void append_u64_le(Bytes& out, std::uint64_t value)
{
    for (int shift = 0; shift < 64; shift += 8) {
        out.push_back(static_cast<std::uint8_t>((value >> shift) & 0xFFU));
    }
}

template <std::size_t N>
void append_array(Bytes& out, const std::array<std::uint8_t, N>& value)
{
    out.insert(out.end(), value.begin(), value.end());
}

} // namespace

Id128 generate_session_id()
{
    std::random_device random;
    Id128 id{};
    for (auto& byte : id) {
        byte = static_cast<std::uint8_t>(random());
    }
    return id;
}

Id128 derive_file_id(
    const Id128& session_id,
    std::uint64_t file_size,
    const Sha256& file_sha256,
    std::string_view file_name)
{
    Bytes input;
    input.reserve(session_id.size() + sizeof(file_size) + file_sha256.size() + file_name.size());
    append_array(input, session_id);
    append_u64_le(input, file_size);
    append_array(input, file_sha256);
    input.insert(input.end(), file_name.begin(), file_name.end());

    const auto digest = sha256(input);
    Id128 id{};
    std::copy_n(digest.begin(), id.size(), id.begin());
    return id;
}

std::string id_to_hex(const Id128& id)
{
    constexpr char kHex[] = "0123456789abcdef";
    std::string out;
    out.reserve(id.size() * 2);
    for (const auto byte : id) {
        out.push_back(kHex[(byte >> 4U) & 0x0FU]);
        out.push_back(kHex[byte & 0x0FU]);
    }
    return out;
}

} // namespace aqrt::core
