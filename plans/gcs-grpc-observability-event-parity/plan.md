# GCS gRPC observability event parity

Plan status: ready

Plan slug: gcs-grpc-observability-event-parity

## User goal

Work through the completed investigation and ensure solid observability for the GCS gRPC implementation, including the ability to understand what happens with a query through `ProfileEvents`, blob storage logs, and accounting for errors, throttles, retries, bytes, timing, and object-storage operations.

## Investigation baseline

Investigation file: [investigation.md](./investigation.md)

Investigation status: ready

Imported findings:
- F001: Native GCS gRPC has core operation surfaces but no observed `ProfileEvents` instrumentation.
- F002: S3 provides the production observability parity baseline for requests, API operations, buffers, retry/throttle accounting, and blob storage logs.
- F003: Azure provides precedent for distinct provider event names.
- F004: Blob storage log infrastructure is provider-neutral and can likely be reused.
- F005: GCS retryable status classification exists but no retry-loop usage was observed.
- F006: GCS gRPC operation mapping is simpler than S3 multipart but still needs per-RPC counters.
- F007: Generic remote bandwidth throttler events exist, but native GCS does not appear to enter read/write throttling scopes.
- F008: The prior native GCS plan includes a future explicit `gcs` table-function gRPC path.

Imported constraints:
- C003: Existing GCS-as-`s3` behavior must remain preserved unless explicitly changed by a later plan.
- C004: Distinct GCS event names are required.
- C005: Future explicit `gcs` table-function gRPC path is in observability scope.
- C006: Retry/throttle behavior is in scope, not just accounting.
- C007: Future C++ implementation must follow repository instructions.
- C008: Existing unrelated worktree changes must be left untouched.
- Rejected as investigation-only: C001 and C002 limited the investigation prompt to `investigation.md`; this planning prompt explicitly allows only `plan.md`.

Imported assumptions or grey areas:
- G001: Exact final event list must be resolved by a parity matrix.
- G002: Exact retry policy and retryable operation set require implementation-phase idempotency validation.
- G003: Exact throttle category mapping for gRPC statuses requires validation, especially `RESOURCE_EXHAUSTED`.
- G004: Blob storage log granularity should default to per `ReadObject` request and per `WriteObject` stream.
- G005: Table-function gRPC timing should not block disk observability if shared instrumentation can be used.
- AS001: Distinct GCS event names should follow Azure/S3 naming patterns.
- AS002: S3 multipart-specific events are not required for current GCS gRPC parity.
- AS003: Blob storage log schema likely does not need extension for basic GCS read/upload/delete parity.
- AS004: Actual retry/throttle behavior is not yet implemented in native GCS.
- AS005: A shared client-level instrumentation layer is preferable so disk and table-function GCS gRPC paths do not diverge.
- AS006: Modified submodules and `tmp/` are assumed unrelated and must remain untouched.

Imported blockers:
- None.

Planning response:
- This plan follows the investigation recommendation: combine distinct native GCS observability with retry/throttle behavior hardening.
- Disk/object-storage observability is planned first because it exists now; the table-function path is included through shared instrumentation and a later compatibility phase so it does not force disk work to wait for unfinished table-function code.
- S3/Azure are used as parity baselines, but S3-only multipart and redirect semantics are not treated as required for current GCS streaming writes.
- No blocking user questions remain; the user already clarified table-function scope, distinct event naming, and behavior hardening.

## Problem statement

Native GCS gRPC can perform object-storage work, but the implementation is not yet production-observable at the level expected from existing ClickHouse object-storage providers. Operators need query-visible `ProfileEvents`, server-wide `system.events`, blob storage log rows, retry/throttle counters, byte/timing accounting, and operation counters that identify native GCS behavior without conflating it with S3/XML compatibility paths. This plan defines reviewable phases to add that observability and the matching retry/throttle behavior while preserving existing GCS-as-`s3` behavior.

## Non-goals

- Replacing or transparently upgrading existing GCS-as-`s3` disk behavior.
- Changing default `gcs` table-function behavior away from the existing S3/XML path.
- Adding S3 multipart-specific GCS counters before native GCS has equivalent multipart or resumable-upload phases.
- Broad GCS performance benchmarking or direct-connectivity optimization work beyond ensuring observability can diagnose behavior.
- User-facing documentation work unless required by maintainer review for the new observable surface.
- Creating phase task files, notes files, review files, implementation changes, staging, or commits from this prompt.

## Constraints

- Only `plans/gcs-grpc-observability-event-parity/plan.md` may be written by this planning step.
- Existing unrelated worktree changes, currently `contrib/liburing`, `contrib/sysroot`, and `tmp/`, must not be modified or attributed to this plan.
- Native GCS events must use distinct GCS names such as `GCS*`, `DiskGCS*`, `ReadBufferFromGCS*`, and `WriteBufferFromGCS*` rather than reusing `S3*` names.
- Existing S3/XML GCS compatibility paths must continue to emit existing S3-compatible events unless the user explicitly opts into native GCS gRPC.
- Retry and throttling behavior must be designed fail-close; unsafe retry boundaries must surface errors rather than silently falling back or retrying consequential writes.
- Future C++ changes must follow ClickHouse Allman brace style.
- Future build/test runs must redirect output to build-directory log files and use a subagent to summarize logs.
- Future `ninja` usage must not pass `-j` or use `nproc`.
- Temporary files for future work must use repository-local `tmp/`, not `/tmp`.

## Assumptions

- Distinct GCS event names will be accepted because Azure already has provider-specific event families and the user explicitly requested this direction. Confidence: high. Validate in P01 through maintainer review and event-name parity checks.
- Basic blob storage log parity can reuse existing `Read`, `Upload`, and `Delete` event types without schema changes. Confidence: high. Validate in P03 with blob log field coverage for GCS bucket/object/path/error/time/bytes.
- Current GCS writes are a single `WriteObject` stream, so S3 multipart event parity is not required yet. Confidence: high. Validate in P01/P03 against current `GCSObjectStorage` write semantics.
- `RESOURCE_EXHAUSTED` should be treated as throttling if the status model can distinguish it from generic unavailable errors. Confidence: medium. Validate in P01 with `GCSStatus` mapping review.
- Shared GCS client/buffer instrumentation can serve both disk/object-storage and future explicit table-function gRPC paths. Confidence: medium. Validate in P02 and P04 against actual integration boundaries.

## Open questions

- None. The investigation asked and resolved the plan-shaping questions about table-function scope, distinct GCS names, and retry/throttle behavior.

## Acceptance criteria

- [ ] A001: Native GCS exposes a distinct event family for provider operations, disk operations, read buffers, write buffers, retry/throttle behavior, and request/bandwidth throttling; mapped phases: P01, P02, P03, P04.
- [ ] A002: Native GCS disk/object-storage operations report query-visible and server-visible counts, bytes, timings, errors, throttles, retries, and attempts for metadata/list/delete/read/write paths; mapped phases: P02, P03, P05.
- [ ] A003: Native GCS emits blob storage log rows for read, upload, and delete outcomes with enough data to diagnose object path, size, elapsed time, and error details without unnecessary schema changes; mapped phases: P03, P05.
- [ ] A004: Retry and throttling behavior is implemented only at validated safe boundaries, with accounting that matches actual attempts, blocked time, and retryable/throttled statuses; mapped phases: P01, P02, P03, P05.
- [ ] A005: The future explicit `gcs` table-function gRPC path uses the same GCS observability contract, while default S3/XML table-function behavior remains unchanged; mapped phases: P02, P04, P05.
- [ ] A006: Automated verification proves successful and failing native GCS operations produce expected `ProfileEvents` and blob storage log evidence, and proves existing S3/GCS-as-`s3` event behavior is not regressed; mapped phases: P03, P04, P05.

## Relevant context

- `plans/gcs-grpc-observability-event-parity/investigation.md`: Ready evidence baseline for this plan; imports findings F001-F008, constraints C003-C008, assumptions AS001-AS006, and grey areas G001-G005.
- `plans/grpc-for-gcs/plan.md`: Prior native GCS gRPC plan; establishes explicit native GCS scope, preservation of GCS-as-`s3`, and future explicit table-function gRPC intent.
- `src/Common/ProfileEvents.cpp`: Defines exported event names used by `system.events` and `system.query_log.ProfileEvents`; current S3/Azure event families are the parity model.
- `src/IO/GCS/GCSClient.*`: Native GCS gRPC client surface for `GetObject`, `ListObjects`, `DeleteObject`, `ReadObject`, and `WriteObject`; likely shared accounting boundary.
- `src/IO/GCS/GCSStatus.*`: Native GCS status conversion and retryability model; must support retry/throttle classification.
- `src/Disks/DiskObjectStorage/ObjectStorages/GCS/GCSObjectStorage.*`: Native GCS disk/object-storage implementation and nested read/write buffers; main current integration target.
- `src/IO/S3/PocoHTTPClient.cpp`, `src/IO/S3/Client.cpp`, `src/IO/ReadBufferFromS3.cpp`, `src/IO/WriteBufferFromS3.cpp`, and `src/Disks/DiskObjectStorage/ObjectStorages/S3/S3ObjectStorage.cpp`: S3 observability, retry, throttler, and blob log parity baseline.
- `src/Disks/IO/ReadBufferFromAzureBlobStorage.cpp` and `src/Disks/IO/WriteBufferFromAzureBlobStorage.cpp`: Azure provider-specific event and blob log precedent.
- `src/Common/BlobStorageLogWriter.h` and `src/Interpreters/BlobStorageLog.h`: Provider-neutral blob storage log API and schema to reuse if sufficient.

## Decisions

- D001: Use distinct native GCS event names instead of aliasing S3 events.
  Rationale: The user requested Azure-style distinct names, and native GCS should not be misattributed as S3/XML.
  Alternatives considered: Reuse S3 names for dashboard continuity; use only generic remote-storage names.
  Reversible: no
  Affects phases: P01, P02, P03, P04, P05

- D002: Establish the event vocabulary and status/throttle classification before wiring broad instrumentation.
  Rationale: Event names are exported observable surface; changing them after implementation would create dashboard churn.
  Alternatives considered: Add counters opportunistically at each call site.
  Reversible: partly
  Affects phases: P01, P02, P03, P04

- D003: Keep retry/throttle behavior and accounting in the same implementation stream.
  Rationale: Retry and throttle counters are only trustworthy if they reflect actual behavior, not just final-error classification.
  Alternatives considered: Observability-only counters first; behavior hardening later.
  Reversible: partly
  Affects phases: P02, P03, P05

- D004: Reuse existing blob storage log schema for GCS read/upload/delete unless P03 proves required fields are missing.
  Rationale: The existing schema is provider-neutral and already covers S3/Azure/Local core events.
  Alternatives considered: Add GCS-specific blob log event types immediately.
  Reversible: yes
  Affects phases: P03, P05

- D005: Do not block disk/object-storage observability on the future explicit table-function gRPC path.
  Rationale: Disk native GCS code exists now, while the table-function path may land separately; shared instrumentation should allow later reuse.
  Alternatives considered: Wait for table-function implementation before any observability work; make a disk-only plan.
  Reversible: yes
  Affects phases: P02, P03, P04

## Verification ladder

Use the lowest sufficient verification tier for each phase:

- Tier 0 smoke: format, lint, typecheck, targeted unit tests, static checks.
- Tier 1 core: relevant unit or integration test suite.
- Tier 2 behavioral: end-to-end test, migration test, benchmark, scenario, or regression reproduction.
- Tier 3 manual: manual inspection or human review when no automated check exists.

Each phase must name its intended tier and why that tier is sufficient.

## Phase overview

| Phase | Slug | Goal | Dependencies | Expected artifacts | Verification tier | Verification |
|---|---|---|---|---|---|---|
| P01 | 01-event-vocabulary-and-status-model | Establish exported GCS event vocabulary and status categories for retry/throttle/error accounting. | none | `src/Common/ProfileEvents.cpp`, `src/IO/GCS/GCSStatus.*`, parity matrix in phase notes | Tier 0 | Static/event-name checks plus targeted status classification tests |
| P02 | 02-retry-and-throttling-foundation | Add shared retry/throttling behavior and accounting boundaries for native GCS gRPC. | P01 | `src/IO/GCS/GCSClient.*`, GCS throttler/settings plumbing, fake-service retry/throttle tests | Tier 1 | Fake-service client tests for attempts, retryable errors, throttle classification, and blocked-time accounting |
| P03 | 03-disk-buffer-and-blob-log-parity | Wire native GCS disk/object-storage read/write/list/delete paths into events, throttling scopes, and blob storage logs. | P01, P02 | `src/Disks/DiskObjectStorage/ObjectStorages/GCS/GCSObjectStorage.*`, blob log wiring, disk/fake-service tests | Tier 2 | Disk/object-storage scenario tests validating `ProfileEvents` and blob storage log rows |
| P04 | 04-table-function-observability-contract | Ensure the explicit `gcs` table-function gRPC path reuses the same observability contract without changing default S3/XML behavior. | P01, P02 | Table-function routing/integration artifacts if the explicit gRPC path exists; compatibility tests or readiness contract otherwise | Tier 2 | Behavioral table-function tests when path exists; otherwise automated compatibility checks plus review of exported contract |
| P05 | 05-compatibility-and-regression-evidence | Prove end-to-end parity, compatibility, and non-regression across native GCS and existing S3/XML paths. | P03, P04 | Regression tests, scenario evidence, build/test log summaries | Tier 2 | Targeted integration/regression suite covering success, error, retry, throttle, blob log, and S3 compatibility cases |

## Phases

### P01: Event vocabulary and status model

Slug: `01-event-vocabulary-and-status-model`

Goal:
Define the native GCS observable surface before broad wiring: exported event names, descriptions, provider/disk/read-buffer/write-buffer families, and status categories for success, error, throttle, retryable error, and retry attempts.

Scope:
- Distinct `GCS*`, `DiskGCS*`, `ReadBufferFromGCS*`, and `WriteBufferFromGCS*` event families.
- Mapping from S3/Azure parity concepts to GCS concepts, with S3-only multipart/redirect concepts marked non-applicable.
- `GCSStatus` classification needed to distinguish retryable statuses from throttling statuses where possible.

Out of scope:
- Broad instrumentation at every call site.
- Changing S3 or Azure event semantics.
- Implementing request throttlers or retry loops.

Dependencies:
- none

Phase interface:

Inputs:
- Investigation findings F001-F007.
- User decision requiring distinct GCS event names.
- Current `ProfileEvents` S3/Azure event families and current `GCSStatus` mapping.

Outputs:
- Exported GCS event vocabulary suitable for `system.events` and `system.query_log.ProfileEvents`.
- Status classification contract for later retry/throttle accounting.
- A parity matrix recorded in phase notes for later task decomposition and review.

Downstream contract:
- Later phases may rely on stable event names and status categories unless maintainer review explicitly requires a rename before P02 begins.

Assumptions exported:
- S3 multipart-specific events are not part of current GCS parity until native GCS has equivalent multipart or resumable-upload phases.

Assumptions not exported:
- That the first proposed event list is final without maintainer review.

Expected artifacts:
- `src/Common/ProfileEvents.cpp`: Native GCS event definitions and descriptions.
- `src/IO/GCS/GCSStatus.*`: Classification refinements if required for throttle/retry separation.
- Phase notes parity matrix: S3/Azure baseline, GCS equivalent, and non-applicable events.

Verification approach:
- Tier: Tier 0
- Method: Static/event-name checks, targeted status-classification unit tests if status code behavior changes, and format/build smoke appropriate to touched files.
- Sufficiency: P01 only establishes exported names and classification rules; targeted static and unit checks are enough before behavioral wiring exists.

Completion criteria:
- Distinct GCS events cover aggregate requests, disk requests, operation counters, buffer bytes/timing/errors, retry attempts, retryable errors, throttles, and request/bandwidth throttler accounting.
- `RESOURCE_EXHAUSTED` and other retryable statuses have an explicit classification decision or a documented fail-close limitation.
- The parity matrix identifies intentionally omitted S3-only events.

Risks and rollback:
- Risk: Event names become public surface and are painful to rename. Mitigation: compare with S3/Azure names and capture maintainer review before broad usage. Rollback: remove unused event definitions before later phases depend on them.

Task decomposition guidance:
- Decompose around event families and status categories, not around every future increment site.

### P02: Retry and throttling foundation

Slug: `02-retry-and-throttling-foundation`

Goal:
Add shared native GCS retry/throttle behavior and accounting boundaries so later disk and table-function paths report real behavior consistently.

Scope:
- Safe retry boundaries for idempotent metadata, list, delete, and read operations.
- Carefully validated retry behavior for `WriteObject`, limited to safe stream boundaries.
- Request-throttler and generic remote bandwidth-throttler integration points where native GCS performs remote requests or moves bytes.
- Accounting for attempts, retryable errors, throttles, throttler blocks, and sleep time.

Out of scope:
- Full disk blob storage log wiring.
- Table-function routing changes.
- New fallback from native GCS gRPC to S3/XML.

Dependencies:
- P01

Phase interface:

Inputs:
- P01 event vocabulary and status classification.
- Current `GCSClient` RPC boundaries and fake-service seam.
- Repository fail-close constraint.

Outputs:
- Shared retry/throttle behavior usable by native GCS disk and future table-function gRPC paths.
- Query-visible accounting that matches actual retry and throttling behavior.
- Fake-service coverage for retryable errors, throttles, and non-retryable exceptions.

Downstream contract:
- Later phases may call native GCS client operations and expect consistent attempt, retry, throttle, and request-throttler accounting.

Assumptions exported:
- Operations without proven safe retry boundaries must fail after the observed error rather than retrying unsafely.

Assumptions not exported:
- That all GCS RPCs have identical retry semantics.

Expected artifacts:
- `src/IO/GCS/GCSClient.*`: Shared retry/throttle behavior and accounting boundaries.
- GCS settings/config plumbing if needed for retry and throttler controls.
- Fake-service tests for retry and throttle status behavior.

Verification approach:
- Tier: Tier 1
- Method: Targeted fake-service unit/core tests that force retryable, throttled, and non-retryable GCS statuses and assert attempts, errors, throttle counters, and no unsafe retry behavior.
- Sufficiency: Fake-service tests can deterministically prove client behavior without real GCS credentials or end-to-end disk scenarios.

Completion criteria:
- Retry attempt counters increment only for actual attempts.
- Retryable-error counters and throttle counters match status classification.
- Request throttler blocked/sleep accounting is visible when throttling is forced.
- Unsafe write retry cases fail closed with a clear exception.

Risks and rollback:
- Risk: Incorrect write retries could duplicate or corrupt writes. Mitigation: restrict retries to validated safe boundaries and add fake-service stream-failure coverage. Rollback: disable write retries while preserving read/metadata retry behavior and visible errors.

Task decomposition guidance:
- Decompose by shared behavior boundary: status classification, retry control, request throttling, bandwidth throttling, and deterministic fake-service verification.

### P03: Disk buffer and blob log parity

Slug: `03-disk-buffer-and-blob-log-parity`

Goal:
Make current native GCS disk/object-storage read, write, metadata, list, and delete operations observable through GCS events, throttling scopes, and blob storage logs.

Scope:
- `GCSObjectStorage` metadata, existence, listing, deletion, range-read, and streaming-write paths.
- Nested `GCSReadBuffer` and `GCSWriteBuffer` byte/timing/error accounting.
- Blob storage log rows for GCS reads, uploads, and deletes using existing event types where sufficient.
- Disk-specific `DiskGCS*` counters in disk/object-storage code paths.

Out of scope:
- Default `gcs` table-function behavior.
- New blob storage log schema unless P03 proves existing fields are insufficient.
- S3 multipart-equivalent events without native GCS multipart behavior.

Dependencies:
- P01
- P02

Phase interface:

Inputs:
- P01 event vocabulary.
- P02 shared retry/throttle behavior.
- Existing GCS disk/object-storage implementation.
- Existing blob storage log writer API.

Outputs:
- Native GCS disk/object-storage operations visible in `ProfileEvents` and blob storage logs.
- Disk/fake-service scenarios for success, error, retry, and throttle paths.
- Confirmation that blob storage log schema is reused or a documented reason if schema work becomes necessary.

Downstream contract:
- P05 may treat native GCS disk/object-storage as the canonical observable path for end-to-end compatibility and regression evidence.

Assumptions exported:
- Existing blob storage log event types are sufficient if tests can capture read/upload/delete rows with GCS object identity, bytes, time, and errors.

Assumptions not exported:
- That table-function GCS observability is complete merely because disk observability is complete.

Expected artifacts:
- `src/Disks/DiskObjectStorage/ObjectStorages/GCS/GCSObjectStorage.*`: Disk path event, throttling, and blob log wiring.
- Tests or scenarios under the relevant existing GCS/disk test area.
- Build/test log summaries for the targeted scenario runs.

Verification approach:
- Tier: Tier 2
- Method: Fake-service disk/object-storage scenarios that execute read, write, list, metadata, and delete operations, then assert `ProfileEvents` deltas and blob storage log rows for success and failure cases.
- Sufficiency: Disk scenarios prove the observable behavior operators need for real native GCS disks, not just isolated client counters.

Completion criteria:
- Successful disk reads/writes report bytes and elapsed time through GCS buffer events.
- Metadata/list/delete operations increment matching `GCS*` and `DiskGCS*` operation counters.
- Error, retry, and throttle paths produce matching event deltas and blob log error details.
- Existing S3/XML paths are not touched by GCS disk instrumentation.

Risks and rollback:
- Risk: Blob log volume may increase if logging granularity is too fine. Mitigation: default to per `ReadObject` request and per `WriteObject` stream, matching investigation guidance. Rollback: adjust granularity before relying on the emitted rows in P05.

Task decomposition guidance:
- Decompose by observable disk surface: metadata/list/delete, read buffer, write buffer, blob log writer, and fake-service scenario evidence.

### P04: Table-function observability contract

Slug: `04-table-function-observability-contract`

Goal:
Ensure the future explicit `gcs` table-function gRPC path uses the same native GCS observability, while preserving default S3/XML table-function behavior.

Scope:
- Shared GCS instrumentation contract for table-function gRPC integration.
- Wiring to the explicit table-function gRPC path if that path exists by this phase.
- Compatibility guard that default `gcs` table-function behavior remains S3/XML unless explicit native gRPC is selected.

Out of scope:
- Implementing the whole table-function gRPC feature if it is still absent from the prior native GCS plan.
- Changing public default table-function semantics.
- Disk/object-storage blob log work already covered by P03.

Dependencies:
- P01
- P02

Phase interface:

Inputs:
- P01 event vocabulary.
- P02 shared native GCS client observability behavior.
- Current table-function implementation state from the native GCS plan.

Outputs:
- If explicit table-function gRPC code exists: it emits native GCS events and retry/throttle accounting through shared instrumentation.
- If explicit table-function gRPC code does not yet exist: a documented integration contract and automated guard proving default `gcs` table-function behavior still uses the existing S3/XML path.
- Compatibility tests for default and explicit paths where both exist.

Downstream contract:
- P05 may verify table-function compatibility using either concrete wiring or the recorded readiness contract, depending on whether the explicit path exists by then.

Assumptions exported:
- Shared client-level instrumentation is the stable reuse boundary for future table-function gRPC behavior.

Assumptions not exported:
- That the explicit table-function gRPC path is present in the repository when this phase begins.

Expected artifacts:
- Table-function routing/integration files if explicit gRPC support exists by this phase.
- Compatibility tests or static guards proving default S3/XML behavior remains unchanged.
- Phase notes documenting the exported instrumentation contract for future table-function work if code is not present.

Verification approach:
- Tier: Tier 2
- Method: Behavioral table-function tests when explicit gRPC routing exists; otherwise targeted compatibility checks for default S3/XML behavior plus review of the shared instrumentation contract.
- Sufficiency: The tier is behavioral because user-visible table-function routing is compatibility-sensitive; manual-only verification would miss regressions.

Completion criteria:
- Default `gcs` table-function behavior remains S3/XML and existing S3 events remain the expected observable surface.
- Explicit native gRPC table-function behavior, when present, emits GCS events through the same instrumentation used by disk/native object storage.
- If explicit gRPC routing is absent, the phase records a concrete downstream contract instead of adding speculative code.

Risks and rollback:
- Risk: Table-function work may blur into unfinished feature implementation. Mitigation: limit this phase to observability integration and compatibility guards. Rollback: retain the shared instrumentation contract and defer concrete wiring until the explicit path lands.

Task decomposition guidance:
- Decompose around compatibility boundary and instrumentation reuse, not around implementing unrelated table-function functionality.

### P05: Compatibility and regression evidence

Slug: `05-compatibility-and-regression-evidence`

Goal:
Produce end-to-end evidence that native GCS observability parity works and that existing S3/XML behavior remains unchanged.

Scope:
- Regression coverage for success, error, retryable error, throttle, and non-retryable exception paths.
- Evidence that `system.query_log.ProfileEvents`, `system.events`, and blob storage logs expose expected GCS data.
- Compatibility checks for existing S3/GCS-as-`s3` paths and default `gcs` table-function behavior.
- Build/test log summaries following repository rules.

Out of scope:
- Broad real-GCS performance benchmarking.
- New user-facing documentation unless a previous phase or maintainer review makes it mandatory.
- Additional event families not justified by P01-P04 evidence.

Dependencies:
- P03
- P04

Phase interface:

Inputs:
- Completed disk/native GCS observability from P03.
- Table-function compatibility or readiness contract from P04.
- Existing S3/Azure parity expectations from investigation.

Outputs:
- A regression evidence set demonstrating GCS events/logs for native paths and no regression for S3/XML paths.
- Targeted integration/fake-service test coverage suitable for CI review.
- A concise handoff summary identifying any remaining observability gaps as follow-up scope.

Downstream contract:
- The plan can be considered complete when P05 evidence satisfies all plan-level acceptance criteria or explicitly records deferred table-function wiring because the explicit path is not present yet.

Assumptions exported:
- None; this phase validates previous assumptions rather than exporting new ones.

Assumptions not exported:
- That real-GCS production performance is proven by fake-service or local integration tests.

Expected artifacts:
- Targeted regression tests in the relevant existing test area.
- Build/test logs under the build directory with subagent summaries.
- Phase notes or review evidence mapping tests to A001-A006.

Verification approach:
- Tier: Tier 2
- Method: Targeted integration/fake-service regression suite covering native GCS disk operations, table-function compatibility where available, `ProfileEvents`, blob storage logs, retry/throttle behavior, and unchanged S3/XML behavior.
- Sufficiency: End-to-end scenarios are needed because the goal is operational observability across query-visible and log-visible surfaces.

Completion criteria:
- Each plan-level acceptance criterion A001-A006 has automated evidence or a documented deferral tied only to absent future table-function gRPC code.
- Existing S3/GCS-as-`s3` behavior and event names remain unchanged.
- No unrelated worktree changes are introduced.

Risks and rollback:
- Risk: Tests may be too environment-dependent if they require real GCS credentials. Mitigation: prefer fake-service coverage and keep real-GCS checks environment-gated. Rollback: remove environment-dependent requirements from CI while preserving deterministic fake-service tests.

Task decomposition guidance:
- Decompose by evidence mapping to acceptance criteria, not by adding broad test sweeps.

## Plan validation

Status: passed

Hard checks:
- Unique phase ids and slugs: pass
- Dependencies reference existing earlier phases or `none`: pass
- Phase dependency graph has no cycles: pass
- No phase depends on a later phase: pass
- Every phase has expected artifacts or `None` with a reason: pass
- Every phase has a verification approach and tier: pass
- Every plan-level acceptance criterion maps to at least one phase: pass
- If `investigation.md` exists, imported findings, constraints, assumptions, grey areas, and blockers are reflected or explicitly rejected with rationale: pass
- Blocking or plan-shaping open questions are asked directly in the chat response, or plan status is `blocked`: pass
- Plan contains no implementation task checklist: pass
- Plan contains no generic filler: pass

Warnings:
- Worktree has pre-existing changes in `contrib/liburing`, `contrib/sysroot`, and `tmp/`; this plan leaves them untouched.
- P04 may record a readiness contract rather than concrete table-function wiring if the explicit table-function gRPC path from the prior native GCS plan is not present when that phase starts.

## Review and handoff expectations

- Each phase must produce `<phase-slug>-review.md` before completion.
- Review findings that require work must become tasks before the next phase starts.
- Notes must capture assumptions, decisions, uncertainties, and handoff summary.

## Plan change log

- 2026-05-11: Initial plan created from ready investigation baseline and current repository context.

## Plan maintenance

- Update this plan only when scope, phase order, acceptance criteria, or constraints change.
- Record every material plan change in the plan change log.
- Do not use this plan as a task list.
