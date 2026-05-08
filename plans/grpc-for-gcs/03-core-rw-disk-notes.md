# core read/write disk semantics Notes

Plan: [plan.md](./plan.md)
Phase: P03 / `03-core-rw-disk`

## Implementation context

- P03 starts after P02 completed in commits `8884e48` and `e16de0d`.
- P01 selected direct generated `google.storage.v2` gRPC stubs and added `src/IO/GCS/GCSClient.*`, `src/IO/GCS/GCSStatus.*`, and fake stream/stub seams.
- P02 added `ObjectStorageType::GCS`, `object_storage_type=gcs`, and `src/Disks/DiskObjectStorage/ObjectStorages/GCS/GCSObjectStorage.*` with operation placeholders that throw `NOT_IMPLEMENTED`.
- P03 replaces the P02 placeholders for core operations only; `plain`, `plain_rewritable`, and table-function routing remain out of scope.
- Existing GCS-as-`s3` disk behavior and default `gcs` table-function/storage behavior remain S3/XML-compatible.
- Worktree safety: `contrib` is currently a symlink and Git reports unrelated tracked `contrib/*` deletions plus `?? contrib`. Do not reset, overwrite, or stage `contrib`.
- Build/test commands must redirect output to `build/` logs and use a subagent to summarize logs before review.
- `clang-format` is unavailable in this environment; `git diff --check` is the formatting substitute for this phase.

## Investigation context

- Investigation file: [investigation.md](./investigation.md)
- Relevant findings: F002, F004, F005, F006.
- Relevant constraints: C001, C002, C003, C005.
- Relevant assumptions validated: AS006 and the P03 plan assumptions that core `IObjectStorage` operations can support metadata modes and that native GCS returns sufficient metadata.
- Relevant open questions/blockers: G004 was validated for first-scope `WriteObject` semantics; no user-answerable blocker is known.

## Request and protocol mapping

Decision:
- Use direct `google.storage.v2` requests with bucket resources formatted as `projects/_/buckets/<bucket>` unless the config already provides a `projects/` resource name.

Evidence:
- `contrib/google-cloud-cpp/google/cloud/storage/internal/grpc/object_request_parser.cc` maps high-level storage requests to direct proto fields with `bucket=projects/_/buckets/<bucket>`, plain object names, `ReadObjectRequest.read_offset/read_limit`, and `WriteObjectRequest.write_object_spec.resource` plus `write_offset`.
- Generated `storage.pb.h` exposes `GetObjectRequest`, `ReadObjectRequest`, `WriteObjectRequest`, `ListObjectsRequest`, and `DeleteObjectRequest` fields needed by P03.

Affected artifact:
- `src/Disks/DiskObjectStorage/ObjectStorages/GCS/GCSObjectStorage.cpp` centralizes bucket resource naming, relative object-name validation, metadata conversion, `GetObject`, `ReadObject`, `WriteObject`, `ListObjects`, and `DeleteObject` request construction.

Uncertainty:
- GCS generation preconditions and resumable upload IDs remain out of first-scope P03; unsupported advanced write modes fail closed.

Next action:
- P04 can build metadata-mode behavior on the core object operations without changing request naming.

## Core operation decisions

Decision:
- Initial native GCS writes support `WriteMode::Rewrite` only and fail closed for append.

Evidence:
- `GCSWriteBuffer` opens one gRPC `WriteObject` stream, chunks requests at `google::storage::v2::ServiceConstants::MAX_WRITE_CHUNK_BYTES`, advances `write_offset`, and sets `finish_write` on the final request.
- `src/Disks/tests/gtest_gcs_object_storage_config.cpp` verifies large payload chunk offsets and finalization through `GCS::FakeStub`.

Affected artifact:
- `GCSWriteBuffer` in `src/Disks/DiskObjectStorage/ObjectStorages/GCS/GCSObjectStorage.cpp`.

Uncertainty:
- Native checksum finalization is not implemented; GCS accepts requests without client-provided object checksums, and checksum enhancement is not required for P03.

Next action:
- P06 can revisit checksum/performance details if maintainers require explicit checksum reporting.

Decision:
- Native GCS reads use a lazy `ReadBufferFromFileBase` implementation rather than eager full-object reads.

Evidence:
- `GCSReadBuffer` issues `ReadObject` with `read_offset` and `read_limit`, supports seek, right-bounded reads, `readBigAt`, and remote-size reporting when `StoredObject::bytes_size` is known.
- Fake tests verify bounded reads and recorded `read_limit`.

Affected artifact:
- `GCSReadBuffer` in `src/Disks/DiskObjectStorage/ObjectStorages/GCS/GCSObjectStorage.cpp`.

Uncertainty:
- The first implementation performs synchronous per-buffer range requests; advanced async prefetch is deferred.

Next action:
- P06 performance validation should measure whether additional prefetching is required.

Decision:
- Native optimized GCS rewrite/copy remains deferred; correctness uses generic read/write copy.

Evidence:
- `copyObject` and `copyObjectToAnotherObjectStorage` copy through `readObject` and `writeObject` and return after same-storage copy to avoid duplicate work.
- Fake tests verify same-provider copy.

Affected artifact:
- `src/Disks/DiskObjectStorage/ObjectStorages/GCS/GCSObjectStorage.cpp`.

Uncertainty:
- Native `rewrite` could improve performance but is not necessary for P03 correctness.

Next action:
- P06 can document or benchmark generic copy behavior.

Decision:
- GCS `ListObjectsRequest.lexicographic_start` is treated as inclusive by the fake; `GCSObjectStorage::iterate` filters keys `<= start_after` to preserve ClickHouse exclusive resume semantics.

Evidence:
- Google Cloud C++ parser and generated proto expose `lexicographic_start`; S3 uses exclusive `StartAfter`.
- Fake tests verify `iterate(..., start_after="clickhouse-data/a")` starts at `clickhouse-data/b`.

Affected artifact:
- `src/IO/GCS/GCSClient.cpp` fake listing and `GCSObjectStorage::iterate`.

Uncertainty:
- None for P03.

Next action:
- None.

## Implemented artifacts

- `src/IO/GCS/GCSClient.*`: reusable fake object map for `GetObject`, `ListObjects`, `DeleteObject`, `ReadObject`, and `WriteObject`, including captured write requests for chunk/offset assertions.
- `src/Disks/DiskObjectStorage/ObjectStorages/GCS/GCSObjectStorage.*`: core native GCS `IObjectStorage` operations, lazy read buffer, streaming write buffer, metadata conversion, delete, list, iterate, copy, settings patch pass-through, and fail-closed unsupported behavior.
- `src/Disks/tests/gtest_gcs_object_storage_config.cpp`: fake-client core operation tests, status/error tests, chunking/range/list/copy/delete coverage, and a `DiskObjectStorage` local-metadata scenario.

## Verification notes

- `clang-format -i ... > build/test_gcs_core_rw_disk_clang_format.log 2>&1` failed because `clang-format` is not installed.
- `git diff --check -- <P03 files>` exited 0.
- `ninja -C build unit_tests_dbms > build/test_gcs_core_rw_disk_build.log 2>&1` failed in unrelated Rust `wasmtime` vendoring before GCS test execution.
- `ninja -C build <targeted objects> > build/test_gcs_core_rw_disk_objects.log 2>&1` failed because `cmake` tried to regenerate and reported uninitialized/missing `contrib` submodules.
- Direct compile-command verification became unavailable after the local `contrib` symlink/submodule state deteriorated: `build/test_gcs_core_rw_disk_direct_objects.log` fails on missing `contrib` include directories and then missing standard/test headers. Earlier in the phase, before the environment failure, direct object compilation passed for `GCSClient.cpp`, `GCSObjectStorage.cpp`, `gtest_gcs_grpc_client.cpp`, and `gtest_gcs_object_storage_config.cpp`; later source review found and verified fixes for the compile issues that were introduced after that pass.
- Reviewer agents found blockers for eager write/read behavior and no-Google unused-parameter paths; those were fixed. Final relevant-path review had no remaining blocker/high code findings, with unrelated `contrib` dirt explicitly ignored.

## Risks

- Runtime execution of the new fake tests was not observed because this worktree cannot build `unit_tests_dbms`.
- Direct object compilation could not be rerun after the final fixes due unrelated `contrib`/toolchain include breakage.
- The first read buffer is synchronous and range-capable, but not optimized with async prefetch.
- Native checksums and generation preconditions are not first-scope P03 features.

## Deferred or future work

- P04: Enable `plain` and `plain_rewritable` metadata modes, including shared compatibility and metrics decisions, after P03 core operations pass behavioral tests in a healthy checkout.
- P05: Add explicit `gcs` table-function gRPC opt-in after core object operations exist.
- P06: Run real same-region GCE/GCS direct-connectivity validation and document final limitations after correctness phases complete.

## Handoff summary

Current status:
- P03 core native GCS implementation, fake coverage, review, and phase completion are committed in `c1e2047`, with build execution blocked by unrelated local `contrib`/Rust environment failures.

Completed artifacts:
- `src/IO/GCS/GCSClient.*`: fake object map and captured write-stream requests for operation tests.
- `src/Disks/DiskObjectStorage/ObjectStorages/GCS/GCSObjectStorage.*`: native GCS core `IObjectStorage` read/write/list/delete/metadata/copy behavior for `local` metadata workloads.
- `src/Disks/tests/gtest_gcs_object_storage_config.cpp`: fake core operation tests and local-metadata disk scenario.
- `plans/grpc-for-gcs/03-core-rw-disk-review.md`: verification and critique record committed with `c1e2047`.

Key decisions:
- Use direct `google.storage.v2` requests, `projects/_/buckets/<bucket>` resources, rewrite-only streaming writes, lazy ranged reads, generic buffered copy, and exclusive `start_after` filtering over GCS inclusive listing.

Assumptions:
- The fake client seam is sufficient for P03 behavioral coverage until a healthy checkout can run `unit_tests_dbms`. Confidence: medium.
- The local build failures are unrelated to P03 because they occur in `contrib`/Rust/toolchain setup before GCS test execution. Confidence: high.

Uncertainties:
- Runtime behavior of the new tests in this exact worktree remains unobserved due unrelated build environment failures.

Next likely work:
- None in this session; P04 should start in a later session only after P03 is committed and marked complete.
