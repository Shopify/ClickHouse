# Read path optimization Tasks

Phase status: completed

Phase id: P02
Phase slug: 02-read-path-optimization
Plan: [plan.md](./plan.md)

Phase goal:
Optimize native GCS reads so sequential `MergeTree` scans no longer issue a new synchronous bounded `ReadObject` RPC for every buffer refill and no longer copy through an intermediate `String` on the normal path, while preserving existing read contracts.

Verification tier:
Tier 1

Dependencies:
- P01 / `01-gcs-client-capabilities` completed with reviewer all-clear.

Tasks:
- [x] T001: Validate the P02 read strategy against `src/Disks/DiskObjectStorage/ObjectStorages/GCS/GCSObjectStorage.cpp`, `src/IO/GCS/GCSClient.cpp`, `src/IO/GCS/GCSClient.h`, and `src/IO/ReadSettings.h`. Acceptance: `plans/parallel-rewrite-copycompose-grpc/02-read-path-optimization-notes.md` records the selected sequential strategy, which `ReadSettings` fields are used or intentionally left unused, and the test targets for request-boundary evidence.
  Done: local change pending commit; notes record the persistent sequential stream/window strategy and `ReadSettings` decisions.
- [x] T002: Add sequential read request-boundary regression coverage in `src/Disks/tests/gtest_gcs_object_storage_config.cpp`. Acceptance: a native GCS read-buffer test reads an object larger than multiple buffer refills, asserts full byte correctness, and asserts fewer `ReadObject` requests than the current one-request-per-refill behavior for the selected strategy.
  Done: local change pending commit; `GCSObjectStorageReadBuffer.SequentialReadsUseSingleStreamAcrossBufferRefills` verifies one-window and multi-window sequential reads with fewer requests than per-refill behavior.
- [x] T003: Update the normal sequential `GCSReadBuffer` path in `src/Disks/DiskObjectStorage/ObjectStorages/GCS/GCSObjectStorage.cpp` to use the selected stream, prefetch, or larger-range context instead of fetching each buffer refill through `fetchRange`. Acceptance: the common `nextImpl` path avoids intermediate `String` accumulation before filling the internal buffer, returns correct data across buffer boundaries, and satisfies T002 request-count evidence.
  Done: local change pending commit; `nextImpl` reads from a persistent sequential `ReadObject` stream/window and copies response chunks into the internal buffer with bounded pending data.
- [x] T004: Preserve `seek` and position-reset behavior in `GCSReadBuffer` after the sequential read change. Acceptance: tests in `src/Disks/tests/gtest_gcs_object_storage_config.cpp` cover `SEEK_SET`, `SEEK_CUR`, buffer-end position reporting, stream/range reset after seeking, negative seek exceptions, and seek-past-object exceptions.
  Done: local change pending commit; `SeekPositionAndOffsetContracts` and `SeekSurfacesActiveStreamFinishFailure` cover position resets, request offsets, invalid seeks, and active-stream finish errors on seek.
- [x] T005: Preserve `readBigAt`, right-bounded reads, EOF, and remote-size behavior in `GCSReadBuffer`. Acceptance: tests cover `readBigAt` offset/limit requests, bounded `StoredObject` reads, empty objects, exact-buffer objects, short final reads, `supportsRightBoundedReads`, and `getRemoteFileSize`.
  Done: local change pending commit; `RangeReadsAndEOF` covers bounded `readBigAt`, EOF, cancellation, right-bounded support, and remote-size behavior.
- [x] T006: Add native GCS read failure and accounting coverage for the optimized path. Acceptance: tests prove start-status failures, null stream creation, stream `Finish` failures, and mid-stream short reads surface as exceptions without fallback; profile/blob-log byte and error accounting remains asserted for representative success and failure cases.
  Done: local change pending commit; `ReadFailuresAndAccounting`, `StreamFinishFailure`, and `SeekSurfacesActiveStreamFinishFailure` cover auth/start failure, null streams, finish failures, short reads, and profile/blob-log accounting.
- [x] T007: Thread `read_hint` and applicable `ReadSettings` values from `GCSObjectStorage::readObject` into the optimized `GCSReadBuffer` behavior. Acceptance: `read_hint`, `remote_fs_buffer_size`, throttling, and applicable prefetch/seek threshold settings are either used by code with targeted assertions or explicitly recorded in notes as intentionally unsupported for this phase with a reason.
  Done: local change pending commit; code uses `remote_fs_buffer_size`, `read_hint`, `remote_fs_prefetch`, `prefetch_buffer_size`, throttling, and blob-log settings, with deferred settings documented in notes.
- [x] T008: Run Tier 1 verification with `ninja -C build unit_tests_dbms > build/test_02_read_path_optimization_build.log 2>&1` followed by `build/src/unit_tests_dbms --gtest_filter='GCSObjectStorageReadBuffer.*:GCSObjectStorageCore.FakeReadWriteListDeleteAndCopy' > build/test_02_read_path_optimization.log 2>&1`. Acceptance: both commands exit 0, logs are saved under `build/`, subagents summarize the logs, and `02-read-path-optimization-review.md` records the commands, log paths, and result.
  Done: local change pending commit; both commands exited 0, logs are under `build/`, and subagents summarized successful build and 8 passing tests.

Task dependencies:

| Task | Depends on | Can run before review? | Reason |
|---|---|---|---|
| T001 | none | yes | Strategy and settings validation prevents guessing before changing the read path. |
| T002 | T001 | yes | Request-boundary regression coverage depends on the selected sequential strategy and evidence shape. |
| T003 | T001 | yes | The implementation depends on the selected stream, prefetch, or range strategy. |
| T004 | T003 | yes | Seek/reset behavior must be validated against the changed sequential read state. |
| T005 | T003 | yes | Range and EOF behavior must be validated against the changed buffer implementation. |
| T006 | T003 | yes | Failure and accounting behavior must be validated against the optimized read path. |
| T007 | T001, T003 | yes | Settings wiring depends on the selected strategy and the new read-buffer state. |
| T008 | T002, T004, T005, T006, T007 | yes | Tier 1 verification should run after behavior tests and settings coverage are present. |

Review gates:
- Verification must be recorded in `02-read-path-optimization-review.md`.
- Critique must be recorded in `02-read-path-optimization-review.md`.
- Reviewer all-clear must be recorded before phase completion.
