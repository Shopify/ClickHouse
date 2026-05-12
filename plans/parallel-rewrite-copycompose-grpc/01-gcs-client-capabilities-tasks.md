# GCS client capabilities Tasks

Phase status: completed

Phase id: P01
Phase slug: 01-gcs-client-capabilities
Plan: [plan.md](./plan.md)

Phase goal:
Extend the ClickHouse-owned native GCS client wrapper and fake seam so later phases can use direct gRPC operations for `ComposeObject`, `RewriteObject`, and the selected parallel/resumable write primitive without bypassing fail-closed behavior.

Verification tier:
Tier 0

Dependencies:
- none

Tasks:
- [x] T001: Inspect pre-existing diffs in `src/IO/GCS/GCSClient.h`, `src/IO/GCS/GCSClient.cpp`, `src/IO/GCS/GCSStatus.h`, `src/IO/GCS/GCSStatus.cpp`, and `src/IO/tests/gtest_gcs_grpc_client.cpp` before editing. Acceptance: `plans/parallel-rewrite-copycompose-grpc/01-gcs-client-capabilities-notes.md` records which diffs are prerequisite, unrelated, or already satisfy P01 work, and no unrelated user changes are overwritten.
  Done: local change pending commit; relevant source files had no pre-existing diffs, while unrelated `contrib/*` and `tmp/*` changes were left untouched.
- [x] T002: Validate generated direct-stub and protobuf availability for `ComposeObject`, `RewriteObject`, `StartResumableWrite`, and `BidiWriteObject` from the current build inputs referenced by `src/IO/GCS/GCSClient.*`. Acceptance: notes list the exact available generated methods/types and state whether P03 should default to compose-backed temp chunks or a resumable/bidi hybrid.
  Done: local change pending commit; notes list generated signatures and keep compose-backed temp chunks as the P03 default.
- [x] T003: Define the P01 client API surface in `src/IO/GCS/GCSClient.h` for native compose and rewrite operations. Acceptance: `GCS::IStub` and `GCS::Client` declarations expose direct-stub compose/rewrite entry points with request/response types that downstream phases can call without using high-level `google::cloud::storage::Client`.
  Done: local change pending commit; `GCS::IStub` and `GCS::Client` expose `composeObject` and `rewriteObject`.
- [x] T004: Implement compose and rewrite wrappers in `src/IO/GCS/GCSClient.cpp` using the existing deadline, auth, routing metadata, and `Status` conversion patterns. Acceptance: wrappers call the generated stub methods, propagate auth and gRPC failures as `GCS::Status`, and do not introduce a high-level-client fallback path.
  Done: local change pending commit; wrappers use generated direct stubs and `Result` / `Status` propagation.
- [x] T005: Extend `GCS::FakeStub` in `src/IO/GCS/GCSClient.h` and `src/IO/GCS/GCSClient.cpp` with request capture, configurable statuses/responses, and object-map behavior for compose and rewrite. Acceptance: fake state can simulate compose success, compose failure, rewrite completion, rewrite-token continuation, and rewrite failure deterministically.
  Done: local change pending commit; fake compose/rewrite request capture, object map, statuses, and token responses are implemented.
- [x] T006: Add targeted compose tests in `src/IO/tests/gtest_gcs_grpc_client.cpp`. Acceptance: tests prove compose routing metadata, user-project metadata, request capture, fake object-map concatenation order, success response, and failure status propagation.
  Done: local change pending commit; verified by `build/src/unit_tests_dbms --gtest_filter='GCSGrpcClientFoundation.*'`.
- [x] T007: Add targeted rewrite and primitive-selection tests in `src/IO/tests/gtest_gcs_grpc_client.cpp`. Acceptance: tests prove rewrite routing metadata, token-continuation response handling, fake source/destination object behavior, failure status propagation, auth failure short-circuiting, and the recorded P03 primitive decision remains aligned with generated API availability.
  Done: local change pending commit; tests cover rewrite routing, fake rewrite, token responses, auth failures, and generated API method signatures.
- [x] T008: Run Tier 0 verification for the changed GCS client artifacts with `ninja -C <build_dir> unit_tests_dbms > <build_dir>/test_01_gcs_client_capabilities_build.log 2>&1` followed by `<build_dir>/src/unit_tests_dbms --gtest_filter='GCSGrpcClientFoundation.*' > <build_dir>/test_01_gcs_client_capabilities.log 2>&1`. Acceptance: both commands exit 0, logs are saved under the chosen build directory, a subagent summarizes each log, and the review records the build directory and log paths.
  Done: local change pending commit; `build/test_01_gcs_client_capabilities_build.log` and `build/test_01_gcs_client_capabilities.log` were summarized by subagents.
- [x] T009: Resolve review finding R001 by adding complete `RewriteObject` routing metadata in `src/IO/GCS/GCSClient.cpp` and asserting it in `src/IO/tests/gtest_gcs_grpc_client.cpp`. Acceptance: rewrite requests send both encoded `source_bucket` and destination `bucket` in `x-goog-request-params`, and targeted tests pass.
  Done: local change pending commit; reviewer all-clear confirmed R001 resolved.
- [x] T010: Resolve review finding R002 by recording generated API signatures and the P03 primitive decision in `plans/parallel-rewrite-copycompose-grpc/01-gcs-client-capabilities-notes.md`, and by validating generated method pointer signatures in `src/IO/tests/gtest_gcs_grpc_client.cpp`. Acceptance: notes include exact generated symbols and tests compile against `ComposeObject`, `RewriteObject`, `StartResumableWrite`, and `BidiWriteObject` signatures.
  Done: local change pending commit; reviewer all-clear confirmed R002 resolved.
- [x] T011: Resolve review finding R003 by updating P01 task, notes, and review bookkeeping. Acceptance: tasks are checked off, handoff summary is current, and `01-gcs-client-capabilities-review.md` records verification, critique, findings, and reviewer all-clear.
  Done: local change pending commit; P01 bookkeeping is complete.

Task dependencies:

| Task | Depends on | Can run before review? | Reason |
|---|---|---|---|
| T001 | none | yes | Worktree safety must be established before editing files that already have local diffs. |
| T002 | none | yes | Generated API availability is the critical validation for the phase design. |
| T003 | T002 | yes | The public client surface depends on which direct generated RPCs and messages exist. |
| T004 | T003 | yes | Wrapper implementation depends on the declared client and stub interface. |
| T005 | T003 | yes | Fake support depends on the declared client and stub interface but can be built alongside wrappers. |
| T006 | T004, T005 | yes | Compose tests need both real wrapper behavior and fake behavior. |
| T007 | T002, T004, T005 | yes | Rewrite and primitive-selection tests need generated API validation plus wrapper and fake behavior. |
| T008 | T006, T007 | yes | Tier 0 verification should run after the implementation and targeted tests are present. |
| T009 | T004, T007 | yes | R001 required wrapper and test changes after reviewer critique. |
| T010 | T002, T007 | yes | R002 required generated API evidence and notes after reviewer critique. |
| T011 | T008, T009, T010 | yes | R003 is bookkeeping after verification and review finding resolution. |

Review gates:
- Verification must be recorded in `01-gcs-client-capabilities-review.md`.
- Critique must be recorded in `01-gcs-client-capabilities-review.md`.
- Reviewer all-clear must be recorded before phase completion.
