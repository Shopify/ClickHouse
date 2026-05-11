# Event vocabulary and status model Tasks

Phase status: completed

Phase id: P01
Phase slug: 01-event-vocabulary-and-status-model
Plan: [plan.md](./plan.md)

Phase goal:
Define the native GCS observable surface before broad wiring: exported event names, descriptions, provider/disk/read-buffer/write-buffer families, and status categories for success, error, throttle, retryable error, and retry attempts.

Verification tier:
Tier 0

Dependencies:
- none

Tasks:
- [x] T001: Finalize the P01 parity matrix in `plans/gcs-grpc-observability-event-parity/01-event-vocabulary-and-status-model-notes.md`. Acceptance: the notes contain a `P01 parity matrix` section mapping S3/Azure baseline concepts to selected native GCS event names, marking S3-only multipart and redirect concepts as non-applicable, and recording no unresolved blocking event-name questions.
  Done: local change pending commit, verified by notes section `P01 parity matrix`.
- [x] T002: Add native GCS aggregate request and request-throttler event definitions in `src/Common/ProfileEvents.cpp` using the names selected in T001. Acceptance: `src/Common/ProfileEvents.cpp` defines GCS and DiskGCS read/write request count, microseconds, error, throttling, attempt, retryable-error, GET request-throttler, and PUT/write request-throttler events; existing S3 and Azure event definitions are not renamed.
  Done: local change pending commit, verified by event-vocabulary smoke check and `ninja -C build unit_tests_dbms`.
- [x] T003: Add native GCS operation and buffer event definitions in `src/Common/ProfileEvents.cpp` using the names selected in T001. Acceptance: `src/Common/ProfileEvents.cpp` defines GCS and DiskGCS operation counters for current native RPC concepts `GetObject`, `ListObjects`, `DeleteObject`, `ReadObject`, and `WriteObject`, plus `ReadBufferFromGCS*` and `WriteBufferFromGCS*` byte, microsecond, init-where-applicable, and request-error events.
  Done: local change pending commit, verified by event-vocabulary smoke check and `ninja -C build unit_tests_dbms`.
- [x] T004: Refine native GCS status classification in `src/IO/GCS/GCSStatus.h`, `src/IO/GCS/GCSStatus.cpp`, and `src/IO/GCS/GCSClient.cpp` so throttling can be distinguished from generic retryable errors. Acceptance: `RESOURCE_EXHAUSTED` no longer collapses indistinguishably into generic `Unavailable`, the status API exposes a throttling classification usable by later phases, and `Unavailable`/`DeadlineExceeded` remain retryable without being falsely classified as permission or not-found errors.
  Done: local change pending commit, verified by `GCSGrpcClientFoundation.*` tests.
- [x] T005: Update status classification coverage in `src/IO/tests/gtest_gcs_grpc_client.cpp`. Acceptance: tests cover retryable and throttling classification for `RESOURCE_EXHAUSTED`, `UNAVAILABLE`, `DEADLINE_EXCEEDED`, and at least one non-retryable status; tests assert the final mapping chosen in T004.
  Done: local change pending commit, verified by `build/src/unit_tests_dbms --gtest_filter='GCSGrpcClientFoundation.*'`.
- [x] T006: Format and run static smoke checks for the P01 C++ changes. Acceptance: `clang-format -i src/Common/ProfileEvents.cpp src/IO/GCS/GCSStatus.cpp src/IO/GCS/GCSStatus.h src/IO/GCS/GCSClient.cpp src/IO/tests/gtest_gcs_grpc_client.cpp` exits 0, and `git diff --check -- src/Common/ProfileEvents.cpp src/IO/GCS/GCSStatus.cpp src/IO/GCS/GCSStatus.h src/IO/GCS/GCSClient.cpp src/IO/tests/gtest_gcs_grpc_client.cpp` exits 0.
  Done: local change pending commit, verified by `PATH=/opt/homebrew/opt/llvm/bin:$PATH clang-format -i ...` and `git diff --check -- ...`.
- [x] T007: Run the Tier 0 targeted GCS status tests with build-directory logs. Acceptance: `ninja -C build unit_tests_dbms > build/gcs_grpc_observability_event_model_build.log 2>&1` exits 0, then `build/src/unit_tests_dbms --gtest_filter='GCSGrpcClientFoundation.*' > build/gcs_grpc_observability_event_model_unit.log 2>&1` exits 0; log summaries are ready to cite in `01-event-vocabulary-and-status-model-review.md`.
  Done: local change pending commit, verified by build and unit logs summarized by reviewer agents.
- [x] T008: Run an event-vocabulary smoke check against `src/Common/ProfileEvents.cpp`. Acceptance: a repository-local command such as `python3 - <<'PY' ... PY` or equivalent grep-based check exits 0 after confirming the selected GCS event names from T001 are present and intentionally omitted multipart/redirect GCS events are absent; the exact command and output are recorded in `01-event-vocabulary-and-status-model-review.md`.
  Done: local change pending commit, verified by Python smoke check output `checked 53 required GCS events; omitted 14 redirect/multipart events`.

Task dependencies:

| Task | Depends on | Can run before review? | Reason |
|---|---|---|---|
| T001 | none | yes | The event/status vocabulary must be fixed before code definitions are added. |
| T002 | T001 | yes | Aggregate and throttler event names come from the parity matrix. |
| T003 | T001 | yes | Operation and buffer event names come from the parity matrix. |
| T004 | T001 | yes | Status classification must match the throttle/retry categories selected for events. |
| T005 | T004 | yes | Tests must assert the status API chosen in T004. |
| T006 | T002, T003, T004, T005 | yes | Formatting and diff checks apply after all P01 code/test edits. |
| T007 | T005, T006 | yes | Targeted unit tests require the status tests and formatted source to exist. |
| T008 | T002, T003 | yes | Event vocabulary smoke checks require event definitions to exist. |

Review gates:
- Verification must be recorded in `01-event-vocabulary-and-status-model-review.md`.
- Critique must be recorded in `01-event-vocabulary-and-status-model-review.md`.
- Reviewer all-clear must be recorded before phase completion.
