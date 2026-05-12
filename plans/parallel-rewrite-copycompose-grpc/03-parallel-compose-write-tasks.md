# Parallel compose write Tasks

Phase status: completed

Phase id: P03
Phase slug: 03-parallel-compose-write
Plan: [plan.md](./plan.md)

Phase goal:
Implement safe native GCS parallel writes for large objects using compose-backed finalization, and enable `supportParallelWrite` only after concurrent upload work and safe finalization are implemented.

Verification tier:
Tier 1

Dependencies:
- P01 / `01-gcs-client-capabilities` completed with reviewer all-clear and selected compose-backed temporary chunks as the P03 primitive.

Tasks:
- [x] T001: Inspect pre-existing diffs in `src/Disks/DiskObjectStorage/ObjectStorages/GCS/GCSObjectStorage.*`, `src/IO/GCS/GCSClient.*`, and `src/Disks/tests/gtest_gcs_object_storage_config.cpp` before editing. Acceptance: `plans/parallel-rewrite-copycompose-grpc/03-parallel-compose-write-notes.md` records which diffs are prerequisite, unrelated, or already satisfy P03 work, and no unrelated user changes are overwritten.
  Done: local change pending commit; relevant source paths had no pre-existing diffs, while unrelated `contrib/*` and `tmp/*` changes were left untouched.
- [x] T002: Validate the P03 write strategy against the P01 handoff in `01-gcs-client-capabilities-notes.md`, current `GCSWriteBuffer`, `WriteSettings`, and the existing fake object map. Acceptance: notes record the selected compose-backed strategy, the setting or threshold that chooses parallel versus single-stream writing, and any P01/P02 facts that constrain implementation.
  Done: local change pending commit; notes record compose-backed temp chunks, `s3_allow_parallel_part_upload`, `MAX_WRITE_CHUNK_BYTES` staging, and P01/P02 constraints.
- [x] T003: Add a temporary-object lifecycle model for compose-backed writes in `src/Disks/DiskObjectStorage/ObjectStorages/GCS/GCSObjectStorage.cpp` and any required declarations in `GCSObjectStorage.h`. Acceptance: the chosen temp namespace is derived from the destination object without colliding with user keys, temp object names are deterministic enough for cleanup evidence, and notes document the lifecycle states from chunk upload through final cleanup.
  Done: local change pending commit; temp names are under `<destination>.clickhouse-gcs-compose-tmp/<upload-id>/...`, created temps are tracked after successful creation, and lifecycle is documented in notes.
- [x] T004: Implement large-object chunk upload scheduling in `GCSWriteBuffer` using native `WriteObject` streams for temporary chunks and existing write throttling/settings. Acceptance: a fake large-write test observes multiple temporary `WriteObject` streams when parallel upload is enabled, and a small or disabled-parallel write continues to use the existing single-stream final-object path.
  Done: local change pending commit; `ParallelComposeWritesLargeObjects` and `ParallelUploadCanBeDisabled` cover temp streams and direct fallback.
- [x] T005: Implement final `ComposeObject` handling for compose-backed writes, including an in-order compose tree for more than 32 components. Acceptance: fake tests prove final object bytes and source order are correct for both at-most-32-chunk and more-than-32-chunk writes, and every recorded compose request uses no more than 32 sources.
  Done: local change pending commit; compose finalization and tree compose are covered by `ParallelComposeWritesLargeObjects` and `ParallelComposeTreeHandlesMoreThanThirtyTwoSources`.
- [x] T006: Map metadata and object-write precondition behavior for the final compose operation in `GCSWriteBuffer`. Acceptance: fake tests prove final-object metadata is preserved, representable destination preconditions are sent on the final compose request, and unsupported `WriteSettings::object_storage_write_if_none_match` or `WriteSettings::object_storage_write_if_match` combinations fail before reporting a successful final object.
  Done: local change pending commit; `ParallelComposeMetadataAndPreconditions` covers metadata, `if_generation_match=0`, final precondition failure, and unsupported settings.
- [x] T007: Add fail-closed cleanup behavior for compose-backed writes in `src/Disks/DiskObjectStorage/ObjectStorages/GCS/GCSObjectStorage.cpp`. Acceptance: fake tests cover chunk-upload failure, compose failure, and cleanup attempts; failures surface as exceptions, the destination object is absent unless final compose succeeded, and temp deletion evidence is visible through fake delete requests or notes.
  Done: local change pending commit; `ParallelComposeFailuresCleanupTemps` covers compose failure cleanup and upload failure without deleting uncreated temp names.
- [x] T008: Update native GCS parallel-write capability and accounting surfaces in `GCSObjectStorage.*`. Acceptance: `supportParallelWrite` returns `true` only after the safe parallel path is active, read-only and non-`Rewrite` rejection behavior remains covered, and representative tests assert `WriteBufferFromGCS*` profile events or blob-storage log accounting for parallel writes.
  Done: local change pending commit; `supportParallelWrite` returns `true`, existing rejection tests remain in the targeted suite, and observability tests pass with the new path.
- [x] T009: Run Tier 1 verification with `git diff --check -- src/Disks/DiskObjectStorage/ObjectStorages/GCS/GCSObjectStorage.cpp src/Disks/DiskObjectStorage/ObjectStorages/GCS/GCSObjectStorage.h src/IO/GCS/GCSClient.cpp src/IO/GCS/GCSClient.h src/Disks/tests/gtest_gcs_object_storage_config.cpp`, then `ninja -C build unit_tests_dbms > build/test_03_parallel_compose_write_build.log 2>&1`, then `build/src/unit_tests_dbms --gtest_filter='GCSObjectStorageWriteBuffer.*:GCSObjectStorageObservability.ProfileEventsForDiskOperationsAndBuffers:GCSObjectStorageObservability.ReadAndWriteBufferFailuresAccountErrors:GCSGrpcClientFoundation.FakeCompose*' > build/test_03_parallel_compose_write.log 2>&1`. Acceptance: all commands exit 0, logs are saved under `build/`, subagents summarize the build and test logs, and `03-parallel-compose-write-review.md` records commands, log paths, and results.
  Done: local change pending commit; `git diff --check`, build, and targeted tests passed; review records log paths and subagent summaries.
- [x] T010: Resolve review finding R001 by keeping `sync` in the parallel-compose path after parallel mode starts and adding regression coverage in `src/Disks/tests/gtest_gcs_object_storage_config.cpp`. Acceptance: `SyncAfterParallelModeKeepsComposedData` passes and proves final object bytes include data written before and after `sync`.
  Done: local change pending commit; reviewer all-clear confirmed R001 resolved.
- [x] T011: Resolve review finding R002 by bounding parallel upload concurrency and avoiding tiny temp-object fan-out in `GCSWriteBuffer`. Acceptance: concurrent upload futures are capped, staged data is coalesced to `MAX_WRITE_CHUNK_BYTES` temp objects, and targeted tests still pass.
  Done: local change pending commit; reviewer all-clear confirmed R002 resolved.
- [x] T012: Resolve review finding R003 by making temp/intermediate object ownership fail-closed in `GCSWriteBuffer` and `GCS::FakeStub`. Acceptance: temp names are destination-prefixed, temp/intermediate creations use create-only preconditions, cleanup deletes only successfully created temp objects, fake preconditions are enforced, and targeted tests pass.
  Done: local change pending commit; reviewer all-clear confirmed R003 resolved.

Task dependencies:

| Task | Depends on | Can run before review? | Reason |
|---|---|---|---|
| T001 | none | yes | Worktree safety must be established before editing files that may carry local diffs. |
| T002 | T001 | yes | Strategy validation should happen before changing the write path or tests. |
| T003 | T002 | yes | Temporary object naming and lifecycle depend on the selected compose-backed strategy. |
| T004 | T003 | yes | Chunk upload scheduling needs the temp-object lifecycle model. |
| T005 | T003, T004 | yes | Compose finalization depends on uploaded temporary chunks and their ordering. |
| T006 | T005 | yes | Metadata and precondition behavior must be applied to the final compose request. |
| T007 | T004, T005, T006 | yes | Cleanup and fail-closed behavior depend on upload, compose, and final-object semantics. |
| T008 | T004, T005, T007 | yes | Capability reporting and accounting should be enabled only after the safe parallel path exists. |
| T009 | T004, T005, T006, T007, T008 | yes | Tier 1 verification should run after implementation and targeted coverage are present. |
| T010 | T009 | yes | R001 was discovered during review and required a targeted regression fix. |
| T011 | T009 | yes | R002 was discovered during review and required resource-bound and part-size fixes. |
| T012 | T009 | yes | R003 was discovered during review and required temp ownership and fake precondition fixes. |

Review gates:
- Verification must be recorded in `03-parallel-compose-write-review.md`.
- Critique must be recorded in `03-parallel-compose-write-review.md`.
- Reviewer all-clear must be recorded before phase completion.
