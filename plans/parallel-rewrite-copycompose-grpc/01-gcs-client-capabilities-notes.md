# GCS client capabilities Notes

Plan: [plan.md](./plan.md)
Phase: P01 / `01-gcs-client-capabilities`

## Implementation context

- Primary files for this phase are `src/IO/GCS/GCSClient.h`, `src/IO/GCS/GCSClient.cpp`, and `src/IO/tests/gtest_gcs_grpc_client.cpp`.
- `GCS::IStub` now exposes direct-stub `composeObject` and `rewriteObject` operations using `google::storage::v2::ComposeObjectRequest`, `google::storage::v2::Object`, `google::storage::v2::RewriteObjectRequest`, and `google::storage::v2::RewriteResponse`.
- `GCS::Client` now has `composeObject` and `rewriteObject` wrappers. They use existing deadline/auth context creation and return `GCS::Result` / `GCS::Status`; no high-level `google::cloud::storage::Client` fallback was added.
- `rewriteObject` routes both `source_bucket=<encoded>` and destination `bucket=<encoded>` in `x-goog-request-params` when present.
- `GCS::FakeStub` now captures compose/rewrite requests, supports configurable failure statuses/responses, composes object-map data in source order, copies object-map data for rewrite, and can script rewrite-token responses.
- Pre-edit worktree inventory: `src/IO/GCS/GCSClient.h`, `src/IO/GCS/GCSClient.cpp`, `src/IO/GCS/GCSStatus.h`, `src/IO/GCS/GCSStatus.cpp`, and `src/IO/tests/gtest_gcs_grpc_client.cpp` had no pre-existing diffs when P01 implementation began. Unrelated `contrib/liburing`, `contrib/sysroot`, and `tmp/*` changes were left untouched.
- Verification target drift: `gtest_gcs_grpc_client` is not a Ninja target in this build; targeted verification uses `unit_tests_dbms` with a GoogleTest filter.

## Investigation context

- Investigation file: None
- Relevant findings: None from `plans/parallel-rewrite-copycompose-grpc/investigation.md`; related plan imports `gcs-grpc-perf` F001, F002, F003, F007, and F008 as performance-gap context.
- Relevant constraints: related `gcs-grpc-perf` C002, C003, and C005; project fail-closed constraint from `AGENTS.md`.
- Relevant assumptions validated: generated `google.storage.v2` stubs expose usable compose/rewrite and resumable/bidi write surfaces.
- Relevant open questions/blockers: None blocking. Plan Q001 about exact performance threshold belongs to P06 and does not block P01.

## Decisions from planning

- Preserve the direct generated-stub client architecture and extend `GCS::IStub`; do not switch wholesale to high-level `google::cloud::storage::Client`.
- Keep native GCS under explicit `object_storage_type=gcs`; this phase only prepares the client seam and fake support.
- Treat `GCS*` profile events as an external dependency; P01 did not add compose/rewrite-specific profile events.
- Keep compose-backed temporary chunks as the default P03 write primitive. Evidence: direct generated `StartResumableWrite` and `BidiWriteObject` exist, but they are resumable/streaming write primitives rather than a parallel multipart-style primitive; compose remains the native concatenate operation for independently uploaded temporary chunks.

## Assumptions

- Generated direct-stub methods verified in `build/contrib/google-cloud-cpp-cmake/google/storage/v2/storage.grpc.pb.h`: `Storage::StubInterface::ComposeObject`, `RewriteObject`, `StartResumableWrite`, and `BidiWriteObject`. Confidence: high after build/test.
- Generated protobuf types verified in `build/external/googleapis/src/googleapis_download/google/storage/v2/storage.proto`: `ComposeObjectRequest`, `RewriteObjectRequest`, `RewriteResponse`, `StartResumableWriteRequest`, `StartResumableWriteResponse`, `BidiWriteObjectRequest`, and `BidiWriteObjectResponse`. Confidence: high after compile-time test coverage.
- Rewrite token continuation can be represented at the client/fake seam without implementing full object-storage copy in this phase. Confidence: high; covered by fake scripted `RewriteResponse` tests.
- Fake object-map compose behavior is sufficient for deterministic unit tests before real GCS validation. Confidence: high; real service validation is deferred to P06.

## Risks

- `composeObject` and `rewriteObject` currently use write-side throttling and retry behavior but do not add dedicated compose/rewrite `ProfileEvents`; this is intentional because `GCS*` event work is external to P01.
- P03 must still design temp-object naming, compose-tree handling, cleanup, and precondition mapping; P01 only exposes the client seam.
- P04 must still decide object-storage policy for unsupported rewrite options; P01 only exposes raw request/response plumbing and fake behavior.

## Deferred or future work

- P02 owns `GCSReadBuffer` read-path optimization after the client seam is ready.
- P03 owns parallel write scheduling, temporary object naming, compose trees, cleanup, and `supportParallelWrite` behavior after P01's compose-backed primitive decision.
- P04 owns native same-GCS copy/rewrite dispatch and fail-closed copy behavior.
- P05 owns compatibility validation across disk settings and metadata modes.
- P06 owns real GCS performance validation and `GCS*` event readiness checks.

## Handoff summary

Current status:
- P01 implementation, verification, critique, review all-clear, and task bookkeeping are complete; changes are ready for commit.

Completed artifacts:
- `src/IO/GCS/GCSClient.h`: added compose/rewrite client seam and fake state declarations.
- `src/IO/GCS/GCSClient.cpp`: added direct generated-stub wrappers, client wrappers, rewrite routing metadata, and fake compose/rewrite behavior.
- `src/IO/tests/gtest_gcs_grpc_client.cpp`: added generated API validation plus compose/rewrite routing, fake behavior, failure, token, and auth tests.
- `plans/parallel-rewrite-copycompose-grpc/01-gcs-client-capabilities-tasks.md`: completed task list, including review follow-up tasks.
- `plans/parallel-rewrite-copycompose-grpc/01-gcs-client-capabilities-notes.md`: implementation notes.
- `plans/parallel-rewrite-copycompose-grpc/01-gcs-client-capabilities-review.md`: verification and reviewer all-clear.

Key decisions:
- Direct generated stubs remain the client implementation path because this preserves fail-closed native gRPC behavior.
- P03 should default to compose-backed temporary chunks for parallel write; resumable/bidi write surfaces exist but are not the planned parallel primitive by themselves.

Assumptions:
- Downstream phases can call `GCS::Client::composeObject` and `GCS::Client::rewriteObject` without depending on high-level storage clients. Confidence: high.

Uncertainties:
- Exact precondition/metadata mapping for object-storage compose and rewrite remains deferred to P03/P04.

Next likely work:
- Stop after committing P01; run `/phase-tasks parallel-rewrite-copycompose-grpc 02-read-path-optimization` only when ready for the next phase.
