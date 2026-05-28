#include "receive_frame_collector.hpp"

#include <type_traits>
#include <utility>
#include <variant>

namespace aqrt::app {
namespace {

std::string make_decode_message(const core::ParseResult& parsed)
{
    if (!parsed.message.empty()) {
        return parsed.message;
    }
    return core::parse_error_name(parsed.error);
}

std::string make_assembler_message(core::AssemblerError error)
{
    return core::assembler_error_name(error);
}

} // namespace

ReceiveFrameResult ReceiveFrameCollector::add_payload(const core::Bytes& payload)
{
    const auto parsed = core::decode_frame(payload);
    if (!parsed.ok()) {
        return finish_result({
            ReceiveFrameError::DecodeFailed,
            parsed.error,
            core::AssemblerError::None,
            make_decode_message(parsed),
            false,
            false,
            completed_,
            0,
            0,
            file_name_,
            text_message_,
            {},
        });
    }

    core::AddFrameResult add_result;
    bool supported_frame = false;
    std::visit(
        [this, &add_result, &supported_frame](const auto& frame) {
            using FrameT = std::decay_t<decltype(frame)>;
            if constexpr (std::is_same_v<FrameT, core::ManifestFrame>) {
                supported_frame = true;
                add_result = assembler_.add_manifest(frame);
                if (add_result.ok()) {
                    file_name_ = frame.file_name;
                    text_message_ = (frame.manifest_flags & core::kManifestFlagTextMessage) != 0U;
                }
            } else if constexpr (std::is_same_v<FrameT, core::DataFrame>) {
                supported_frame = true;
                add_result = assembler_.add_data(frame);
            } else if constexpr (std::is_same_v<FrameT, core::EndFrame>) {
                supported_frame = true;
                add_result = assembler_.add_end(frame);
            }
        },
        parsed.frame);

    if (!supported_frame) {
        return finish_result({
            ReceiveFrameError::UnsupportedFrame,
            core::ParseError::None,
            core::AssemblerError::None,
            "frame type is not part of the receive data flow",
            false,
            false,
            completed_,
            0,
            0,
            file_name_,
            text_message_,
            {},
        });
    }

    if (!add_result.ok()) {
        return finish_result({
            ReceiveFrameError::AssemblerRejected,
            core::ParseError::None,
            add_result.error,
            make_assembler_message(add_result.error),
            add_result.accepted,
            add_result.duplicate,
            completed_,
            0,
            0,
            file_name_,
            text_message_,
            {},
        });
    }

    auto result = ReceiveFrameResult{
        ReceiveFrameError::None,
        core::ParseError::None,
        core::AssemblerError::None,
        {},
        add_result.accepted,
        add_result.duplicate,
        completed_,
        0,
        0,
        file_name_,
        text_message_,
        {},
    };

    if (!completed_ && assembler_.complete() && assembler_.has_end()) {
        auto assembled = assembler_.assemble();
        if (!assembled.ok()) {
            return finish_result({
                ReceiveFrameError::AssembleFailed,
                core::ParseError::None,
                assembled.error,
                make_assembler_message(assembled.error),
                add_result.accepted,
                add_result.duplicate,
                false,
                0,
                0,
                file_name_,
                text_message_,
                {},
            });
        }

        completed_ = true;
        assembled_bytes_ = std::move(assembled.data);
    }

    result.completed = completed_;
    if (completed_) {
        result.assembled_bytes = assembled_bytes_;
    }
    return finish_result(std::move(result));
}

void ReceiveFrameCollector::reset()
{
    assembler_ = core::ReceiverAssembler{};
    completed_ = false;
    file_name_.clear();
    text_message_ = false;
    assembled_bytes_.clear();
}

bool ReceiveFrameCollector::has_manifest() const
{
    return assembler_.has_manifest();
}

bool ReceiveFrameCollector::has_end() const
{
    return assembler_.has_end();
}

bool ReceiveFrameCollector::data_complete() const
{
    return assembler_.complete();
}

bool ReceiveFrameCollector::completed() const
{
    return completed_;
}

std::uint32_t ReceiveFrameCollector::received_chunks() const
{
    return assembler_.received_chunks();
}

std::uint32_t ReceiveFrameCollector::total_chunks() const
{
    return assembler_.total_chunks();
}

const std::string& ReceiveFrameCollector::file_name() const
{
    return file_name_;
}

bool ReceiveFrameCollector::text_message() const
{
    return text_message_;
}

const core::Bytes& ReceiveFrameCollector::assembled_bytes() const
{
    return assembled_bytes_;
}

std::uint32_t ReceiveFrameCollector::missing_chunk_count() const
{
    if (!assembler_.has_manifest()) {
        return 0;
    }
    return assembler_.total_chunks() - assembler_.received_chunks();
}

std::vector<core::MissingRange> ReceiveFrameCollector::missing_ranges() const
{
    return assembler_.missing_ranges();
}

std::optional<core::MissingRequestFrame> ReceiveFrameCollector::missing_request_frame(std::uint32_t feedback_seq) const
{
    const auto manifest = assembler_.manifest();
    if (!manifest.has_value() || completed_) {
        return std::nullopt;
    }

    auto ranges = assembler_.missing_ranges();
    const bool needs_end = !assembler_.has_end();
    if (ranges.empty() && !needs_end) {
        return std::nullopt;
    }

    core::MissingRequestFrame request;
    request.session_id = manifest->session_id;
    request.file_id = manifest->file_id;
    request.request_flags = needs_end ? core::kMissingRequestFlagNeedEnd : 0U;
    request.total_chunks = manifest->total_chunks;
    request.received_chunks = assembler_.received_chunks();
    request.feedback_seq = feedback_seq;
    request.ranges = std::move(ranges);
    return request;
}

ReceiveFrameResult ReceiveFrameCollector::finish_result(ReceiveFrameResult result)
{
    result.completed = completed_;
    result.received_chunks = assembler_.received_chunks();
    result.total_chunks = assembler_.total_chunks();
    result.file_name = file_name_;
    result.text_message = text_message_;
    if (completed_ && result.assembled_bytes.empty()) {
        result.assembled_bytes = assembled_bytes_;
    }
    return result;
}

const char* receive_frame_error_name(ReceiveFrameError error)
{
    switch (error) {
    case ReceiveFrameError::None:
        return "None";
    case ReceiveFrameError::DecodeFailed:
        return "DecodeFailed";
    case ReceiveFrameError::UnsupportedFrame:
        return "UnsupportedFrame";
    case ReceiveFrameError::AssemblerRejected:
        return "AssemblerRejected";
    case ReceiveFrameError::AssembleFailed:
        return "AssembleFailed";
    }

    return "Unknown";
}

} // namespace aqrt::app
