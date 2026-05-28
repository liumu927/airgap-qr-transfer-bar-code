#pragma once

#include "assembler.hpp"
#include "frame.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace aqrt::app {

enum class ReceiveFrameError {
    None,
    DecodeFailed,
    UnsupportedFrame,
    AssemblerRejected,
    AssembleFailed,
};

struct ReceiveFrameResult {
    ReceiveFrameError error = ReceiveFrameError::None;
    core::ParseError parse_error = core::ParseError::None;
    core::AssemblerError assembler_error = core::AssemblerError::None;
    std::string message;
    bool accepted = false;
    bool duplicate = false;
    bool completed = false;
    std::uint32_t received_chunks = 0;
    std::uint32_t total_chunks = 0;
    std::string file_name;
    bool text_message = false;
    core::Bytes assembled_bytes;

    [[nodiscard]] bool ok() const { return error == ReceiveFrameError::None; }
};

class ReceiveFrameCollector {
public:
    ReceiveFrameResult add_payload(const core::Bytes& payload);
    void reset();

    [[nodiscard]] bool has_manifest() const;
    [[nodiscard]] bool has_end() const;
    [[nodiscard]] bool data_complete() const;
    [[nodiscard]] bool completed() const;
    [[nodiscard]] std::uint32_t received_chunks() const;
    [[nodiscard]] std::uint32_t total_chunks() const;
    [[nodiscard]] const std::string& file_name() const;
    [[nodiscard]] bool text_message() const;
    [[nodiscard]] const core::Bytes& assembled_bytes() const;
    [[nodiscard]] std::uint32_t missing_chunk_count() const;
    [[nodiscard]] std::vector<core::MissingRange> missing_ranges() const;
    [[nodiscard]] std::optional<core::MissingRequestFrame> missing_request_frame(std::uint32_t feedback_seq) const;

private:
    ReceiveFrameResult finish_result(ReceiveFrameResult result);

    core::ReceiverAssembler assembler_;
    bool completed_ = false;
    std::string file_name_;
    bool text_message_ = false;
    core::Bytes assembled_bytes_;
};

const char* receive_frame_error_name(ReceiveFrameError error);

} // namespace aqrt::app
