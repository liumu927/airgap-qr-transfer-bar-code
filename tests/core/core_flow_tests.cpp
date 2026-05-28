#include "assembler.hpp"
#include "chunker.hpp"
#include "session.hpp"
#include "sha256.hpp"
#include "test_support.hpp"

#include <iostream>
#include <string_view>

void run_frame_codec_tests();

namespace {

using namespace aqrt::core;
using namespace aqrt::core::test;

Bytes sample_file()
{
    return Bytes{
        'A', 'i', 'r', 'G', 'a', 'p', ' ',
        'Q', 'R', ' ',
        'T', 'r', 'a', 'n', 's', 'f', 'e', 'r',
        0, 1, 2, 3, 4, 5,
    };
}

ManifestFrame sample_manifest(const Bytes& file, std::uint32_t chunk_size)
{
    return make_manifest(id_from_seed(10), id_from_seed(80), "payload.bin", file, chunk_size);
}

void test_sha256_known_vectors()
{
    const Bytes empty;
    require(
        sha256_hex(sha256(empty)) == "e3b0c44298fc1c149afbf4c8996fb924"
            "27ae41e4649b934ca495991b7852b855",
        "empty SHA-256 vector should match");

    const Bytes abc{'a', 'b', 'c'};
    require(
        sha256_hex(sha256(abc)) == "ba7816bf8f01cfea414140de5dae2223"
            "b00361a396177a9cb410ff61f20015ad",
        "abc SHA-256 vector should match");
}

void test_chunk_plan_and_data_frames()
{
    const auto plan = plan_chunks(24, 7);
    require(plan.ok(), "valid chunk plan should succeed");
    require(plan.plan.total_chunks == 4, "chunk plan should ceil-divide file size");

    require(plan_chunks(0, 7).error == ChunkPlanError::EmptyFile, "empty file should be rejected");
    require(plan_chunks(24, 0).error == ChunkPlanError::ZeroChunkSize, "zero chunk size should be rejected");
    require(plan_chunks(25, 7, 24).error == ChunkPlanError::FileTooLarge, "oversized file should be rejected");

    const auto file = sample_file();
    const auto manifest = sample_manifest(file, 7);
    const auto frames = make_data_frames(manifest, file);
    require(frames.size() == 4, "data frame count should match total_chunks");
    require(frames[0].chunk_offset == 0, "first chunk offset should be zero");
    require(frames[1].chunk_offset == 7, "second chunk offset should match chunk size");
    require(frames.back().data.size() == 3, "last chunk should contain remainder bytes");
}

void test_session_id_generation_and_hex()
{
    const auto first = generate_session_id();
    const auto second = generate_session_id();

    require(first != second, "generated session IDs should differ across calls");
    require(id_to_hex(first).size() == 32, "session ID hex should be 32 characters");
}

void test_file_id_derivation_is_stable()
{
    const auto file = sample_file();
    const auto digest = sha256(file);
    const auto session = id_from_seed(42);

    const auto first = derive_file_id(session, file.size(), digest, "payload.bin");
    const auto second = derive_file_id(session, file.size(), digest, "payload.bin");
    const auto other_session = derive_file_id(id_from_seed(43), file.size(), digest, "payload.bin");
    const auto other_name = derive_file_id(session, file.size(), digest, "other.bin");

    require(first == second, "file_id derivation should be stable for the same inputs");
    require(first != other_session, "file_id should change when session_id changes");
    require(first != other_name, "file_id should change when file name changes");
}

void test_build_manifest_derives_file_id_and_validates_inputs()
{
    const auto file = sample_file();
    const auto session = id_from_seed(77);
    const auto built = build_manifest(session, "payload.bin", file, 8);

    require(built.ok(), manifest_build_error_name(built.error));
    require(built.manifest.session_id == session, "built manifest should preserve session_id");
    require(built.manifest.file_sha256 == sha256(file), "built manifest should hash file bytes");
    require(
        built.manifest.file_id == derive_file_id(session, file.size(), sha256(file), "payload.bin"),
        "built manifest should derive file_id from manifest inputs");

    require(
        build_manifest(session, "", file, 8).error == ManifestBuildError::EmptyFileName,
        "empty file name should be rejected");
    require(
        build_manifest(session, "dir\\payload.bin", file, 8).error == ManifestBuildError::UnsafeFileName,
        "path-like file name should be rejected");
    require(
        build_manifest(session, "payload.bin", Bytes{}, 8).error == ManifestBuildError::ChunkPlanInvalid,
        "empty file should be rejected by manifest builder");
    require(
        build_manifest(session, "payload.bin", file, 8, 4).chunk_plan_error == ChunkPlanError::FileTooLarge,
        "manifest builder should report oversized file");

    const std::string long_name(kMaxFileNameBytes + 1U, 'a');
    require(
        build_manifest(session, long_name, file, 8).error == ManifestBuildError::FileNameTooLong,
        "long file name should be rejected");
}

void test_assembler_reassembles_out_of_order()
{
    const auto file = sample_file();
    const auto manifest = sample_manifest(file, 7);
    const auto frames = make_data_frames(manifest, file);
    const auto end = make_end_frame(manifest);

    ReceiverAssembler assembler;
    require(assembler.add_manifest(manifest).ok(), "manifest should be accepted");
    require(assembler.add_data(frames[2]).ok(), "out-of-order chunk 2 should be accepted");
    require(assembler.add_data(frames[0]).ok(), "out-of-order chunk 0 should be accepted");
    require(assembler.add_data(frames[3]).ok(), "out-of-order chunk 3 should be accepted");
    require(assembler.add_data(frames[1]).ok(), "out-of-order chunk 1 should be accepted");
    require(!assembler.has_end(), "assembler should not report end before end frame");
    require(assembler.add_end(end).ok(), "matching end frame should be accepted");
    require(assembler.has_end(), "assembler should report end after end frame");
    require(assembler.complete(), "assembler should be complete");

    const auto assembled = assembler.assemble();
    require(assembled.ok(), assembler_error_name(assembled.error));
    require(assembled.data == file, "assembled file should equal original bytes");
}

void test_assembler_duplicate_chunk_is_idempotent()
{
    const auto file = sample_file();
    const auto manifest = sample_manifest(file, 8);
    const auto frames = make_data_frames(manifest, file);

    ReceiverAssembler assembler;
    require(assembler.add_manifest(manifest).ok(), "manifest should be accepted");
    const auto first = assembler.add_data(frames[0]);
    const auto second = assembler.add_data(frames[0]);

    require(first.ok() && first.accepted && !first.duplicate, "first chunk should be new");
    require(second.ok() && second.accepted && second.duplicate, "same chunk should be duplicate");
    require(assembler.received_chunks() == 1, "duplicate chunk should not increment received count");
}

void test_assembler_rejects_duplicate_conflict()
{
    const auto file = sample_file();
    const auto manifest = sample_manifest(file, 8);
    auto frames = make_data_frames(manifest, file);

    ReceiverAssembler assembler;
    require(assembler.add_manifest(manifest).ok(), "manifest should be accepted");
    require(assembler.add_data(frames[0]).ok(), "first chunk should be accepted");

    frames[0].data[0] ^= 0xFFU;
    const auto result = assembler.add_data(frames[0]);
    require(result.error == AssemblerError::DuplicateChunkConflict, "changed duplicate chunk should be rejected");
}

void test_assembler_rejects_cross_session_frame()
{
    const auto file = sample_file();
    const auto manifest = sample_manifest(file, 8);
    auto frames = make_data_frames(manifest, file);
    frames[0].session_id = id_from_seed(200);

    ReceiverAssembler assembler;
    require(assembler.add_manifest(manifest).ok(), "manifest should be accepted");
    const auto result = assembler.add_data(frames[0]);
    require(result.error == AssemblerError::SessionMismatch, "cross-session data should be rejected");
}

void test_assembler_reports_incomplete()
{
    const auto file = sample_file();
    const auto manifest = sample_manifest(file, 8);
    const auto frames = make_data_frames(manifest, file);

    ReceiverAssembler assembler;
    require(assembler.add_manifest(manifest).ok(), "manifest should be accepted");
    require(assembler.add_data(frames[0]).ok(), "first chunk should be accepted");

    const auto assembled = assembler.assemble();
    require(assembled.error == AssemblerError::Incomplete, "missing chunks should prevent assembly");
}

void test_assembler_reports_missing_ranges()
{
    const auto file = sample_file();
    const auto manifest = sample_manifest(file, 4);
    const auto frames = make_data_frames(manifest, file);

    ReceiverAssembler assembler;
    require(assembler.add_manifest(manifest).ok(), "manifest should be accepted");
    require(assembler.add_data(frames[0]).ok(), "chunk 0 should be accepted");
    require(assembler.add_data(frames[3]).ok(), "chunk 3 should be accepted");
    require(assembler.add_data(frames[4]).ok(), "chunk 4 should be accepted");

    const auto ranges = assembler.missing_ranges();
    require(ranges.size() == 2, "missing chunks should be compacted into two ranges");
    require(ranges[0].start_index == 1 && ranges[0].count == 2, "first missing range should cover chunks 1..2");
    require(ranges[1].start_index == 5 && ranges[1].count == 1, "second missing range should cover chunk 5");
}

void test_assembler_reports_sha256_mismatch()
{
    const auto file = sample_file();
    auto manifest = sample_manifest(file, 8);
    const auto frames = make_data_frames(manifest, file);
    manifest.file_sha256[0] ^= 0xFFU;

    ReceiverAssembler assembler;
    require(assembler.add_manifest(manifest).ok(), "manifest with bad hash shape should be accepted");
    for (const auto& frame : frames) {
        require(assembler.add_data(frame).ok(), "chunk should be accepted before final hash check");
    }

    const auto assembled = assembler.assemble();
    require(assembled.error == AssemblerError::Sha256Mismatch, "bad manifest hash should fail final verification");
}

void test_assembler_rejects_bad_chunk_size()
{
    const auto file = sample_file();
    const auto manifest = sample_manifest(file, 8);
    auto frames = make_data_frames(manifest, file);
    frames[0].data.pop_back();

    ReceiverAssembler assembler;
    require(assembler.add_manifest(manifest).ok(), "manifest should be accepted");
    const auto result = assembler.add_data(frames[0]);
    require(result.error == AssemblerError::InvalidChunkSize, "wrong chunk size should be rejected");
}

void run_core_flow_tests()
{
    test_sha256_known_vectors();
    test_chunk_plan_and_data_frames();
    test_session_id_generation_and_hex();
    test_file_id_derivation_is_stable();
    test_build_manifest_derives_file_id_and_validates_inputs();
    test_assembler_reassembles_out_of_order();
    test_assembler_duplicate_chunk_is_idempotent();
    test_assembler_rejects_duplicate_conflict();
    test_assembler_rejects_cross_session_frame();
    test_assembler_reports_incomplete();
    test_assembler_reports_missing_ranges();
    test_assembler_reports_sha256_mismatch();
    test_assembler_rejects_bad_chunk_size();
}

} // namespace

int main()
{
    run_frame_codec_tests();
    run_core_flow_tests();

    std::cout << "airgap_core_tests passed\n";
    return 0;
}
