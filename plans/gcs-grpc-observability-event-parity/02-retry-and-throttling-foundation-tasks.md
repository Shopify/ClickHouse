# Retry and throttling foundation Tasks

Phase status: completed

Phase id: P02
Phase slug: 02-retry-and-throttling-foundation
Plan: [plan.md](./plan.md)

Phase goal:
Add shared native GCS retry/throttle behavior and accounting boundaries so later disk and table-function paths report real behavior consistently. Keep retries fail-close at unsafe stream boundaries and make fake-service tests prove attempts, retryable errors, throttles, and request-throttler accounting.

Verification tier:
Tier 1

Dependencies:
- P01 / `01-event-vocabulary-and-status-model` completed and reviewed

Tasks:
- [x] T001: Add a `P02 retry-safety and accounting matrix` section to `plans/gcs-grpc-observability-event-parity/02-retry-and-throttling-foundation-notes.md`. Acceptance: the matrix names `GetObject`, `ListObjects`, `DeleteObject`, `ReadObject`, and `WriteObject`; classifies retry boundaries as safe, limited, or fail-close; maps each operation to the P01 attempt, retryable-error, throttling, request-throttler, and later bandwidth-throttler accounting surface; and records no blocking retry-policy questions.
  Done: local change pending commit, verified by notes section `P02 retry-safety and accounting matrix`.
- [x] T002: Add retry-control and request-throttler plumbing in `src/IO/GCS/GCSClient.h`, `src/IO/GCS/GCSClient.cpp`, and native GCS settings parsing in `src/Disks/DiskObjectStorage/ObjectStorages/GCS/GCSObjectStorage.cpp`. Acceptance: `GCS::ClientSettings` carries bounded retry controls and GET/PUT request-throttler inputs usable by fake-service tests; disk/native GCS settings preserve current behavior unless configured; no S3/XML settings or behavior are changed.
  Done: local change pending commit, verified by `GCSGrpcClientFoundation.*` tests and scoped diff.
- [x] T003: Add shared native GCS operation metadata/accounting helpers in `src/IO/GCS/GCSClient.*`. Acceptance: every client RPC maps to its P01 provider and disk `ProfileEvents`, read/write aggregate category, request-throttler category, retryability policy from T001, and a documented handoff for byte-movement throttling that P03 will apply around disk buffers.
  Done: local change pending commit, verified by `GCSGrpcClientFoundation.*` tests.
- [x] T004: Extend `GCS::FakeStub`, `FakeReadStream`, or `FakeWriteStream` in `src/IO/GCS/GCSClient.*` only as needed for deterministic retry/throttle tests. Acceptance: tests can script at least one retryable failure followed by success, one throttling failure followed by success, one non-retryable failure, and one unsafe streaming write failure without real GCS credentials.
  Done: local change pending commit, verified by new fake-service retry/throttle tests.
- [x] T005: Implement fail-close retry behavior and `ProfileEvents` accounting for safe unary RPCs in `src/IO/GCS/GCSClient.cpp`. Acceptance: `GetObject`, `ListObjects`, and `DeleteObject` retry only statuses allowed by T001; attempt counters count the initial attempt and actual retries; retryable-error and throttling counters match `GCS::isRetryableStatus` and `GCS::isThrottlingStatus`; non-retryable statuses perform one attempt and surface the original error.
  Done: local change pending commit, verified by `RetryableUnaryRequestRetriesAndAccounts`, `ThrottledUnaryRequestRetriesAndAccounts`, and `NonRetryableUnaryRequestFailsOnceAndAccounts`.
- [x] T006: Implement limited stream retry behavior in `src/IO/GCS/GCSClient.cpp` for `ReadObject` and `WriteObject` according to the T001 safety matrix. Acceptance: stream creation failures may retry only before a stream is returned; read failures after bytes are exposed and write failures after payload is accepted fail closed without replaying data; unsafe write retry cases produce a clear error and matching attempt/error counters.
  Done: local change pending commit, verified by `StreamCreationRetriesBeforeReturningStream` and `WriteStreamFailureAfterPayloadIsNotReplayed`.
- [x] T007: Implement native GCS request-throttler accounting in `src/IO/GCS/GCSClient.cpp` using the P01 `GCSGetRequestThrottler*`, `GCSPutRequestThrottler*`, `DiskGCSGetRequestThrottler*`, and `DiskGCSPutRequestThrottler*` events. Acceptance: fake-service tests can force GET and PUT/write throttler blocking, and the resulting `ProfileEvents` deltas show request count, blocked count, and sleep microseconds for provider and disk modes.
  Done: local change pending commit, verified by `RequestThrottlersAccountProviderAndDiskEvents`.
- [x] T008: Add Tier 1 fake-service tests in `src/IO/tests/gtest_gcs_grpc_client.cpp` for retry, throttle, and fail-close behavior. Acceptance: tests cover retryable `UNAVAILABLE` or `DEADLINE_EXCEEDED`, throttling `RESOURCE_EXHAUSTED`, at least one non-retryable status, request-throttler blocked/sleep accounting, actual attempt counts, retryable-error counters, throttling counters, and unsafe `WriteObject` no-replay behavior.
  Done: local change pending commit, verified by `build/src/unit_tests_dbms --gtest_filter='GCSGrpcClientFoundation.*'`.
- [x] T009: Format and run static smoke checks for the P02 C++ changes. Acceptance: `PATH=/opt/homebrew/opt/llvm/bin:$PATH clang-format -i src/IO/GCS/GCSClient.cpp src/IO/GCS/GCSClient.h src/Disks/DiskObjectStorage/ObjectStorages/GCS/GCSObjectStorage.cpp src/Disks/DiskObjectStorage/ObjectStorages/GCS/GCSObjectStorage.h src/IO/tests/gtest_gcs_grpc_client.cpp` exits 0, and `git diff --check -- src/IO/GCS/GCSClient.cpp src/IO/GCS/GCSClient.h src/Disks/DiskObjectStorage/ObjectStorages/GCS/GCSObjectStorage.cpp src/Disks/DiskObjectStorage/ObjectStorages/GCS/GCSObjectStorage.h src/IO/tests/gtest_gcs_grpc_client.cpp` exits 0.
  Done: local change pending commit, verified by `PATH=/opt/homebrew/opt/llvm/bin:$PATH clang-format -i ...` and `git diff --check -- ...`.
- [x] T010: Run the Tier 1 targeted GCS retry/throttle tests with build-directory logs. Acceptance: `ninja -C build unit_tests_dbms > build/gcs_grpc_retry_throttling_foundation_build.log 2>&1` exits 0, then `build/src/unit_tests_dbms --gtest_filter='GCSGrpcClientFoundation.*' > build/gcs_grpc_retry_throttling_foundation_unit.log 2>&1` exits 0; log summaries are ready to cite in `02-retry-and-throttling-foundation-review.md`.
  Done: local change pending commit, verified by build and unit logs summarized by reviewer agents.

Task dependencies:

| Task | Depends on | Can run before review? | Reason |
|---|---|---|---|
| T001 | none | yes | Retry safety and accounting boundaries must be explicit before behavior changes. |
| T002 | T001 | yes | Retry controls and throttler inputs depend on the selected safe boundaries and accounting categories. |
| T003 | T001, T002 | yes | Shared accounting helpers need the selected policy and settings surface. |
| T004 | T001 | yes | Fake-service scripting must support the statuses and boundaries selected in the matrix. |
| T005 | T003, T004 | yes | Unary retry behavior needs operation metadata and deterministic fake failures. |
| T006 | T003, T004 | yes | Stream retry behavior needs operation metadata and deterministic stream failure coverage. |
| T007 | T002, T003, T004 | yes | Request-throttler accounting needs settings plumbing, event metadata, and tests that can force blocking. |
| T008 | T005, T006, T007 | yes | Tests assert the implemented retry, throttle, and fail-close behavior. |
| T009 | T002, T003, T004, T005, T006, T007, T008 | yes | Formatting and diff checks apply after all P02 code/test edits. |
| T010 | T008, T009 | yes | The targeted Tier 1 test run requires the P02 tests and formatted source to exist. |

Review gates:
- Verification must be recorded in `02-retry-and-throttling-foundation-review.md`.
- Critique must be recorded in `02-retry-and-throttling-foundation-review.md`.
- Reviewer all-clear must be recorded before phase completion.
