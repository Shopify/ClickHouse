# Parallel compose write Review

## Verification

- Commands run:
  - `git diff --check -- src/Disks/DiskObjectStorage/ObjectStorages/GCS/GCSObjectStorage.cpp src/Disks/DiskObjectStorage/ObjectStorages/GCS/GCSObjectStorage.h src/IO/GCS/GCSClient.cpp src/IO/GCS/GCSClient.h src/Disks/tests/gtest_gcs_object_storage_config.cpp src/IO/tests/gtest_gcs_grpc_client.cpp`
  - `ninja -C build unit_tests_dbms > build/test_03_parallel_compose_write_build.log 2>&1`
  - `build/src/unit_tests_dbms --gtest_filter='GCSObjectStorageWriteBuffer.*:GCSObjectStorageObservability.ProfileEventsForDiskOperationsAndBuffers:GCSObjectStorageObservability.ReadAndWriteBufferFailuresAccountErrors:GCSGrpcClientFoundation.FakeCompose*' > build/test_03_parallel_compose_write.log 2>&1`
- Results:
  - `git diff --check` passed.
  - `ninja -C build unit_tests_dbms` passed.
  - Targeted GCS tests passed: 14 tests run, 14 passed.
- Evidence:
  - `build/test_03_parallel_compose_write_build.log`: build completed and `src/unit_tests_dbms` linked with no relevant warnings/errors per log reviewer.
  - `build/test_03_parallel_compose_write.log`: targeted run passed all 14 tests.
  - Subagent log summaries reported successful build and successful test run.
- Verification tier used:
  - Tier 1
- Deviations from planned verification:
  - The targeted test filter was extended by one additional `GCSObjectStorageWriteBuffer` test for `sync` after parallel mode. The planned command shape and log paths were otherwise used.

## Critique

- Risks:
  - Real GCS object generation behavior and compose latency are not proven by fake tests; P06 owns real service validation.
  - Parallel upload currently uses bounded `std::async` workers rather than ClickHouse's S3 writer thread-pool path; concurrency is capped at 4 to avoid unbounded native thread fan-out.
  - Temporary object names are scoped under the destination object prefix and protected with create-only preconditions, but cleanup of leaked objects across process lifetimes remains future operational work.
- Gaps:
  - No real GCS validation in P03; fake/unit coverage is sufficient for the Tier 1 write-contract phase.
  - No new `GCS*` profile events were added, matching the plan's external-metrics constraint.
- Over-scope or under-scope concerns:
  - Fake precondition enforcement was expanded to cover create-only write and compose behavior needed by P03 tests.
  - P04 native rewrite/copy work was not started.

## Review findings

- [x] R001: `sync` after parallel mode must not switch to a single final-object stream and lose already uploaded temp data.
  Severity: blocker
  Evidence: Initial reviewer pass found `sync` could set `explicit_sync_flush`, open a normal `WriteObject` stream, and let `finalize` skip compose/cleanup.
  Required follow-up: T010 completed by keeping `sync` in the parallel path once parallel mode has started and adding `SyncAfterParallelModeKeepsComposedData`.

- [x] R002: Parallel uploads must avoid unbounded native thread creation and unsafe tiny-part fan-out.
  Severity: high
  Evidence: Initial reviewer pass found one `std::async` per flushed buffer and a `buf_size * 2` threshold.
  Required follow-up: T011 completed by coalescing staged data into `MAX_WRITE_CHUNK_BYTES` temp objects and bounding concurrent upload futures to 4.

- [x] R003: Temporary object naming and cleanup must be fail-closed and must not delete objects not created by this upload.
  Severity: high
  Evidence: Reviewer found process-local temp names, unconditional temp cleanup, and missing create-only preconditions for temp/intermediate objects.
  Required follow-up: T012 completed by placing temp names under the destination object prefix, applying create-only preconditions to temp writes and intermediate compose objects, recording temps only after successful creation, and expanding fake precondition coverage.

## Tasks added from findings

- T010
- T011
- T012

## Reviewer all-clear

Reviewer: critic agent
Status: approved
Notes: Final reviewer pass approved P03 after fixes for sync-after-parallel behavior, bounded concurrency, compose-tree source limits, temp naming under the destination prefix, create-only temp/intermediate preconditions, safe cleanup of only created temps, and targeted fake/unit coverage. Latest build and targeted tests passed.
