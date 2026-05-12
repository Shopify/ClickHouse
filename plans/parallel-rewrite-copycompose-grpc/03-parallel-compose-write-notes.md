# Parallel compose write Notes

Plan: [plan.md](./plan.md)
Phase: P03 / `03-parallel-compose-write`

## Implementation context

- Primary implementation target: `src/Disks/DiskObjectStorage/ObjectStorages/GCS/GCSObjectStorage.cpp`, especially `GCSWriteBuffer` and `GCSObjectStorage::supportParallelWrite`.
- Secondary targets changed: `src/Disks/DiskObjectStorage/ObjectStorages/GCS/GCSObjectStorage.h`, `src/IO/GCS/GCSClient.h`, `src/IO/GCS/GCSClient.cpp`, `src/IO/tests/gtest_gcs_grpc_client.cpp`, and `src/Disks/tests/gtest_gcs_object_storage_config.cpp`.
- Startup worktree inventory: relevant P03 source paths had no pre-existing diffs. Unrelated local changes under `contrib/liburing`, `contrib/sysroot`, and `tmp/` were left untouched.
- P01 is complete and selected compose-backed temporary chunks as the P03 primitive. `GCS::Client::composeObject` and `GCS::FakeStub` compose-object-map behavior are used by this phase.
- P02 is complete and changed native GCS reads only; P03 did not depend on P02 beyond existing fake object-map read/write coverage.
- New large-write behavior stages data until it crosses `max(buf_size * 2, google::storage::v2::ServiceConstants::MAX_WRITE_CHUNK_BYTES)`, then uploads temp objects of at most `MAX_WRITE_CHUNK_BYTES` and finalizes with `ComposeObject`.
- Parallel upload futures are bounded to 4 concurrent uploads. This uses bounded `std::async` rather than the S3 writer thread-pool path.
- Small writes, empty writes, disabled parallel writes via `WriteSettings::s3_allow_parallel_part_upload=false`, and explicit `sync` before parallel mode keep using the single `WriteObject` stream path.
- Once parallel mode starts, `sync` remains in the compose-backed path and forces currently staged data to temp objects instead of switching to a single final-object stream.
- Temporary object names are derived from the destination object as `<destination>.clickhouse-gcs-compose-tmp/<upload-id>/<kind>-<index>`, keeping them under the same destination prefix for cleanup/discovery.
- Temp `WriteObject` streams and intermediate compose destinations use create-only `if_generation_match=0` preconditions. Cleanup records temp/intermediate objects only after successful creation and deletes only those recorded objects.
- Final compose maps `WriteSettings::object_storage_write_if_none_match="*"` to `if_generation_match=0`. `object_storage_write_if_match` and non-`*` `object_storage_write_if_none_match` values fail closed before writing.
- `GCS::FakeStub` now uses atomic counters for concurrent write stream tests, serializes fake write callbacks with a mutex, and enforces create-only preconditions for fake write and compose destinations.

## Investigation context

- Investigation file: None
- Relevant findings: None from `plans/parallel-rewrite-copycompose-grpc/investigation.md`; related plan imports `gcs-grpc-perf` F001, F002, F003, F007, and F008 as performance-gap context.
- Relevant constraints: related `gcs-grpc-perf` C002, C003, and C005; project fail-closed constraint from `AGENTS.md`; native GCS remains explicit `object_storage_type=gcs`.
- Relevant assumptions validated: compose-backed temp chunks can produce correct final bytes in fake storage; compose tree honors the 32-source limit; cleanup can be restricted to successfully created temps; `supportParallelWrite` is now backed by a safe parallel path.
- Relevant open questions/blockers: plan Q001 is non-blocking and belongs to P06 performance review; no direct user question blocks P03 completion.

## Decisions from planning

- Keep native GCS under explicit `object_storage_type=gcs`; P03 does not change existing GCS-as-`s3` or default table-function behavior.
- Preserve the direct generated-stub client architecture; P03 calls `GCS::Client::composeObject` and `GCS::Client::writeObject` without high-level-client fallback behavior.
- Use compose-backed temporary chunk uploads because P01 validated `ComposeObject` and recorded resumable/bidi write surfaces as non-default for parallel multipart-style uploads.
- Treat `GCS*` profile-event additions as external; P03 preserves existing write accounting surfaces rather than adding metrics work.

## Assumptions

- `MAX_WRITE_CHUNK_BYTES` is a safe temp-object part size for correctness and avoids tiny object fan-out from adaptive/small write buffers. Confidence: high for fake correctness; P06 must measure performance.
- Bounded concurrency of 4 upload futures is a safe initial cap. Confidence: medium; P06/P05 may tune or route this through a ClickHouse scheduler later if needed.
- Destination-prefixed temp names plus create-only preconditions are sufficient to fail closed on stale or colliding temp names. Confidence: high after fake precondition enforcement and reviewer all-clear.
- Fake object-map tests are sufficient for Tier 1 correctness validation. Confidence: high; performance and real generation semantics remain deferred to P06.

## Risks

- Bounded `std::async` is correct but not the final resource-management story if production workloads need a shared ClickHouse scheduler. Mitigation: concurrency is capped and P06/P05 can tune after measurement.
- Temporary objects can still leak if the process terminates after successful temp creation and before cleanup. Mitigation: temp names are destination-prefixed and create-only, making leaks discoverable and preventing overwrite; P05/P06 may define operational cleanup if needed.
- Fake tests do not prove real GCS throughput or service-side compose cost. Mitigation: P06 owns same-region GCE/GCS performance validation.

## Deferred or future work

- P04 owns native same-GCS rewrite/copy paths and must not be implemented in P03.
- P05 owns broader compatibility validation across disk settings, caches, metadata modes, startup/shutdown behavior, and metric readiness; trigger: P02-P04 implementations complete.
- P06 owns real GCS performance validation, direct-connectivity proof, GCS-as-`s3` comparison, and tuning recommendations; trigger: P05 compatibility validation complete.
- New `GCS*` profile-event definitions remain external to this plan unless P05/P06 explicitly re-scope the work.

## Handoff summary

Current status:
- P03 implementation, Tier 1 verification, critique, reviewer all-clear, and task bookkeeping are complete; changes are ready for commit.

Completed artifacts:
- `src/Disks/DiskObjectStorage/ObjectStorages/GCS/GCSObjectStorage.cpp`: compose-backed parallel write path, bounded upload scheduling, compose tree, temp cleanup, precondition mapping, and failure handling.
- `src/Disks/DiskObjectStorage/ObjectStorages/GCS/GCSObjectStorage.h`: `supportParallelWrite` now reflects the implemented safe parallel path.
- `src/IO/GCS/GCSClient.h`: fake write counters and fake write callback state are safe for concurrent temp-upload tests.
- `src/IO/GCS/GCSClient.cpp`: fake write/compose precondition enforcement and concurrent fake write request capture.
- `src/Disks/tests/gtest_gcs_object_storage_config.cpp`: fake/unit coverage for large parallel compose writes, `sync` after parallel mode, compose trees, disabled parallel upload, preconditions, metadata, and failure cleanup.
- `src/IO/tests/gtest_gcs_grpc_client.cpp`: atomic fake counter assertions and namespace cleanup needed by the updated fake.
- `plans/parallel-rewrite-copycompose-grpc/03-parallel-compose-write-tasks.md`: completed task list, including review follow-up tasks.
- `plans/parallel-rewrite-copycompose-grpc/03-parallel-compose-write-notes.md`: updated implementation notes.
- `plans/parallel-rewrite-copycompose-grpc/03-parallel-compose-write-review.md`: verification, critique, findings, and reviewer all-clear.

Key decisions:
- Use destination-prefixed temp names with create-only preconditions and cleanup only after successful temp/intermediate creation to avoid deleting unowned objects.
- Coalesce temp uploads to `MAX_WRITE_CHUNK_BYTES` and cap upload concurrency at 4 to avoid unbounded thread/object fan-out.
- Keep final compose as the only point where destination metadata and representable destination preconditions are applied.

Assumptions:
- Compose-backed writes can provide safe parallel large-object writes for native GCS with deterministic cleanup evidence. Confidence: high after fake tests and reviewer all-clear.
- Real GCS performance and tuning remain outside P03. Confidence: high because P06 owns that validation.

Uncertainties:
- Whether 4 concurrent uploads and `MAX_WRITE_CHUNK_BYTES` temp objects are optimal for production throughput; P06 should measure.

Next likely work:
- Commit P03 changes, then stop. Do not start P04 in this session.
