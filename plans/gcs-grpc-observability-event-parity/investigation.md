# GCS gRPC observability event parity investigation

Investigation status: ready

Plan slug: gcs-grpc-observability-event-parity

## User goal

Implement parity for `ProfileEvents` and blob storage log coverage for native GCS gRPC so the production implementation is observable, including actual retry/throttle behavior and accounting for errors, throttles, retries, bytes, timing, and object-storage operations.

## Feedback loop state

Iteration: 2

Questions asked directly so far:
- Q001, 2026-05-11/session: Asked whether parity should cover only native GCS disk/object-storage or also the future explicit `gcs` table-function gRPC path. Answer: cover the future `gcs` table function too. Impact: plan scope includes both native disk/object-storage and explicit table-function gRPC observability.
- Q002, 2026-05-11/session: Asked whether to use distinct GCS event names or reuse/alias S3-compatible names. Answer: use distinct GCS names; Azure has its own names, so GCS should follow similarly. Impact: plan should create `GCS*`, `DiskGCS*`, `ReadBufferFromGCS*`, and `WriteBufferFromGCS*` names rather than reusing `S3*`.
- Q003, 2026-05-11/session: Asked whether retry/throttle scope means behavior plus observability or observability only. Answer: implement retry/throttle behavior as well as observability. Impact: plan must include behavior hardening, not just counters/logs.

Current questions for the user:
- None.

User answers incorporated:
- Scope includes native GCS disk/object-storage and future explicit `gcs` table-function gRPC path.
- Event namespace should be distinct GCS names, following the Azure precedent.
- Retry/throttle behavior is in scope, with matching accounting/observability.

Ready to stop asking: yes

Reason:
- The blocking scope, naming, and behavior questions are resolved. Remaining decisions are technical design choices for `/phase-plan` and later implementation review, not user-answerable blockers. Vamos, now it is solid enough to plan.

## Normalized problem statement

Native GCS gRPC now has core disk/object-storage code, but it lacks production observability and hardening parity with existing object-storage providers. The next plan should add a distinct GCS event namespace, blob storage log wiring, error/throttle/retry accounting, actual retry/throttle behavior, and verification coverage across native GCS disk/object-storage and the future explicit `gcs` table-function gRPC path, without changing existing GCS-as-`s3` compatibility behavior.

## User model

Stated request:
- Create or update only `plans/gcs-grpc-observability-event-parity/investigation.md`.
- Solidify the idea `gcs-grpc-observability-event-parity`.
- Goal: implement `ProfileEvents` and blob storage log parity for GCS gRPC, including error/throttle/retry accounting, to make it production observable.
- User clarified that future `gcs` table-function gRPC support is in scope, distinct GCS event names should be used, and retry/throttle behavior itself should be implemented.

Inferred goal:
- The user wants a plan-ready brief for a production-readiness phase after native GCS gRPC core read/write disk work, covering observability and runtime hardening across native GCS surfaces. Confidence: high.

Likely success criteria:
- Native GCS gRPC operations can be diagnosed through `system.query_log.ProfileEvents`, `system.events`, and blob storage logs with provider-specific GCS counters. Confidence: high.
- Native GCS exposes distinct names comparable to Azure/S3 families: `GCS*`, `DiskGCS*`, `ReadBufferFromGCS*`, and `WriteBufferFromGCS*`. Confidence: high.
- Errors, retryable errors, throttling/resource-exhaustion conditions, request attempts, bytes, elapsed time, operation counts, and blob storage log rows are visible for reads, writes, metadata/list/delete operations, disk workloads, and explicit table-function gRPC workloads. Confidence: high.
- Retry and throttling behavior is implemented safely, with accounting matching behavior rather than merely classifying final errors. Confidence: high as user intent, medium for exact mechanics until technical design.
- Existing GCS-as-`s3` behavior and S3/XML observability remain unchanged unless a user explicitly selects native GCS gRPC. Confidence: high.

Hard constraints:
- Only `plans/gcs-grpc-observability-event-parity/investigation.md` may be created or updated. Source: user prompt.
- Do not modify `plan.md`, phase task files, notes/review files, implementation files, code, staging, or commits. Source: user prompt.
- Existing unrelated worktree changes must be left alone. Source: user prompt and `git status`.
- Repository instructions require fail-close behavior over silent fallbacks and no commits to master. Source: `AGENTS.md`.

Soft preferences:
- Prefer evidence-backed parity with S3 because the user explicitly framed the goal as `ProfileEvents` / blob storage log parity and production quality. Confidence: high.
- Prefer distinct native GCS identity because the prior `grpc-for-gcs` plan chose explicit native GCS rather than transparent S3/XML upgrade, and the user confirmed Azure-style naming parity. Confidence: high.
- Prefer actual behavior hardening alongside observability because the user clarified retry/throttle behavior is in scope. Confidence: high.

Authority boundaries:
- Allowed: inspect repository context, inspect existing plans, create/update this investigation, ask clarifying questions.
- Forbidden: implementation changes, phase plan creation, task decomposition, notes/review files, staging, committing.
- Unclear: exact final event list, exact retry policy, and exact throttler configuration names; these are technical design choices for `/phase-plan` and maintainer review.

Likely user assumptions:
- Native GCS gRPC is close enough to production-readiness that observability and retry/throttle hardening are now the important next step. Evidence: completed `grpc-for-gcs` P01-P03 notes and user's new goal. Confidence: high.
- S3 and Azure object-storage event/log surfaces are the model for acceptable provider observability. Evidence: prior S3 gap-analysis request, current parity wording, and user mention of Azure's own event namespace. Confidence: high.
- Retry/throttle accounting should be query-visible and backed by real behavior, not merely debug logs or final-error classification. Evidence: user answer Q003. Confidence: high.

## Investigation scope

In scope:
- Native GCS gRPC code under `src/IO/GCS` and `src/Disks/DiskObjectStorage/ObjectStorages/GCS`.
- Existing S3 observability surfaces in `src/Common/ProfileEvents.cpp`, S3 read/write buffers, S3 HTTP client, S3 object storage, retry loop, throttling scopes, and blob storage log usage.
- Existing Azure provider event namespace and throttler/logging precedent as a naming and parity reference.
- Prior `grpc-for-gcs` investigation/plan/phase notes needed to understand current native GCS design state and future table-function intent.
- Compatibility and verification implications for future planning.

Out of scope:
- Implementing events/logging/retries/throttling.
- Running builds/tests.
- Real GCS or ClickHouse cluster queries.
- Changing docs, code, `plan.md`, task files, notes, or review files.
- Designing every exact counter increment site; only planning-level surfaces and tradeoffs are in scope.

## Executive findings

- F001: Native GCS gRPC currently has core operation surfaces but no observed `ProfileEvents` instrumentation.
  Evidence: `rg -n "namespace ProfileEvents|ProfileEvents::increment" src/IO/GCS src/Disks/DiskObjectStorage/ObjectStorages/GCS` found operation code but no GCS `ProfileEvents::increment` calls; `src/Common/ProfileEvents.cpp` has no `GCS`/`DiskGCS` events.
  Confidence: high
  Plan impact: The plan should add a GCS event vocabulary and wire it through native GCS client/object-storage/read-write buffer paths.

- F002: S3 has a broad production observability model that covers aggregate request accounting, API operation counters, buffer bytes/timing/errors, request throttlers, retries, and blob storage logs.
  Evidence: `src/Common/ProfileEvents.cpp` defines `S3Read*`, `S3Write*`, `DiskS3Read*`, `DiskS3Write*`, per-API `S3*`/`DiskS3*`, `ReadBufferFromS3*`, `WriteBufferFromS3*`, and S3 request throttler events; S3 code increments them in `src/IO/S3/PocoHTTPClient.cpp`, `src/IO/S3/Client.cpp`, `src/IO/ReadBufferFromS3.cpp`, `src/IO/WriteBufferFromS3.cpp`, and `src/Disks/DiskObjectStorage/ObjectStorages/S3/S3ObjectStorage.cpp`.
  Confidence: high
  Plan impact: S3 should be a parity baseline, but S3 multipart and redirect events should only map where gRPC has equivalent behavior.

- F003: Azure provides a strong precedent for distinct provider names rather than overloading S3 counters.
  Evidence: `src/Common/ProfileEvents.cpp` defines `AzureRead*`, `DiskAzureRead*`, `AzureWrite*`, `DiskAzureWrite*`, `Azure*RequestThrottler*`, `DiskAzure*RequestThrottler*`, Azure API operation counters, and `ReadBufferFromAzure*`; Azure code wires those events in `src/IO/AzureBlobStorage/PocoHTTPClient.cpp`, `src/Disks/IO/ReadBufferFromAzureBlobStorage.cpp`, and Azure object-storage files.
  Confidence: high
  Plan impact: Use distinct GCS names following Azure/S3 provider families, as the user confirmed.

- F004: Blob storage log infrastructure is provider-neutral, and S3/Azure/Local already use it for reads, uploads, deletes, and multipart events.
  Evidence: `src/Common/BlobStorageLogWriter.h`; `src/Interpreters/BlobStorageLog.h`; usage in `src/IO/ReadBufferFromS3.cpp`, `src/IO/WriteBufferFromS3.cpp`, `src/IO/S3/deleteFileFromS3.cpp`, `src/Disks/IO/ReadBufferFromAzureBlobStorage.cpp`, and `src/Disks/IO/WriteBufferFromAzureBlobStorage.cpp`.
  Confidence: high
  Plan impact: Native GCS can likely add blob storage log parity without schema changes by using existing `EventType::Read`, `Upload`, and `Delete`; multipart event types are probably not applicable to current GCS streaming writes.

- F005: Native GCS status mapping already distinguishes retryable status categories, but no retry loop usage was observed.
  Evidence: `src/IO/GCS/GCSStatus.cpp` defines `isRetryableStatus` for `Unavailable` and `DeadlineExceeded`; `rg` only found its definition/declaration, not usage.
  Confidence: high
  Plan impact: `/phase-plan` must include retry behavior design and accounting. It should also validate whether `ResourceExhausted` can be represented separately for throttle accounting.

- F006: GCS gRPC operation mapping is simpler than S3 multipart but still needs per-RPC counters.
  Evidence: `src/IO/GCS/GCSClient.h` and `.cpp` expose `getObject`, `listObjects`, `deleteObject`, `readObject`, and `writeObject`; `src/Disks/DiskObjectStorage/ObjectStorages/GCS/GCSObjectStorage.cpp` uses these for metadata/existence, listing/iteration, deletion, range reads, and streaming writes.
  Confidence: high
  Plan impact: Candidate events should include `GCS*` and `DiskGCS*` operation counters for `GetObject`, `ListObjects`, `DeleteObject`, `ReadObject`, and `WriteObject`, plus aggregate read/write events.

- F007: Generic remote bandwidth throttler events exist, but native GCS does not currently appear to enter read/write throttling scopes.
  Evidence: `src/Common/ProfileEvents.cpp` defines `RemoteReadThrottler*`, `RemoteWriteThrottler*`, and query-level equivalents; S3 uses `CurrentThread::ReadThrottlingScope` and `WriteThrottlingScope` in `ReadBufferFromS3`/`WriteBufferFromS3`; no matching usage was observed in native GCS files.
  Confidence: high
  Plan impact: Throttle parity should include both provider request-throttler accounting and generic remote bandwidth throttler integration if applicable.

- F008: Existing prior plan explicitly includes a future explicit `gcs` table-function gRPC path and treated observability naming as provider-specific.
  Evidence: `plans/grpc-for-gcs/plan.md` includes a P05 `gcs` table-function phase and imported `G006: Observability naming should prefer distinct GCS identity where provider-specific additions are needed`.
  Confidence: high
  Plan impact: This plan should set a shared observability vocabulary for disk/object-storage and future table-function native gRPC paths.

## Evidence and belief register

| ID | Claim | Type | Evidence | Confidence | Would change if |
|---|---|---|---|---|---|
| B001 | Target investigation artifact did not exist before this run. | fact | `if [ -f plans/gcs-grpc-observability-event-parity/investigation.md ]; then ...` returned `missing`. | high | Another process creates a conflicting file later. |
| B002 | Existing `grpc-for-gcs` plan selected native explicit GCS object storage and completed core client/type/read-write disk phases. | fact | `plans/grpc-for-gcs/plan.md`; `plans/grpc-for-gcs/01-*`, `02-*`, `03-*` files. | high | The branch history is rewritten or phase files are stale. |
| B003 | Native GCS gRPC files expose client RPCs for `GetObject`, `ListObjects`, `DeleteObject`, `ReadObject`, and `WriteObject`. | fact | `src/IO/GCS/GCSClient.h`; `src/IO/GCS/GCSClient.cpp`. | high | New code changes add/remove RPCs. |
| B004 | Native GCS object storage currently uses nested `GCSReadBuffer` and `GCSWriteBuffer`. | fact | `src/Disks/DiskObjectStorage/ObjectStorages/GCS/GCSObjectStorage.cpp`. | high | Buffers are refactored before planning. |
| B005 | No native GCS `ProfileEvents` exist today. | fact | `src/Common/ProfileEvents.cpp`; `rg` for GCS event names. | high | Another branch already adds them. |
| B006 | S3 has provider-specific event names and separate `DiskS3*` duplicates for disk object storage. | fact | `src/Common/ProfileEvents.cpp` lines with `S3Read*`, `S3Write*`, `DiskS3Read*`, `DiskS3Write*`, per-API S3 events. | high | Maintainers reject adding new provider-specific event families. |
| B007 | Azure has its own provider-specific event namespace and is a good naming precedent for GCS. | fact | `src/Common/ProfileEvents.cpp` Azure/DiskAzure/ReadBufferFromAzure events; user answer Q002. | high | Maintainers prefer a different naming convention for GCS. |
| B008 | Blob storage log can capture GCS read/upload/delete with existing event types. | inference | `BlobStorageLogElement::EventType` has `Read`, `Upload`, `Delete`; S3/Azure/Local use same writer. | high | GCS requires operation types not representable by current enum. |
| B009 | S3 multipart-specific events do not directly map to current GCS gRPC writes. | inference | GCS writes use one `WriteObject` stream; `GCSObjectStorage::supportParallelWrite` returns false. | high | Future GCS upload design adds multipart/resumable upload phases needing similar counters. |
| B010 | GCS retryable status classification exists but is unused. | fact | `GCS::isRetryableStatus` is defined/declared; `rg` did not find call sites. | high | Hidden generated code or future changes add usage. |
| B011 | Retry/throttle behavior is in scope, not only observability. | user-stated constraint | User answer Q003. | high | User narrows scope later. |
| B012 | Future explicit `gcs` table-function gRPC path is in scope. | user-stated constraint | User answer Q001 and `plans/grpc-for-gcs/plan.md` P05. | high | User narrows scope later. |
| B013 | Worktree has unrelated modified submodules and untracked `tmp/`. | fact | `git status --short` showed `m contrib/liburing`, `m contrib/sysroot`, and `?? tmp/`. | high | User declares them related. |

## Repository context inspected

| Path / command | Why inspected | Key findings | Confidence |
|---|---|---|---|
| `git status --short`; `git branch --show-current` | Startup safety | Branch is `gcs-grpc-coherent`; unrelated-looking modified submodules `contrib/liburing`, `contrib/sysroot`, and untracked `tmp/` exist. | high |
| `AGENTS.md` | Project instructions | Do not commit to master, do not rebase/amend, prefer fail-close, future C++ Allman style, build/test log rules. | high |
| `README.md` | Project overview | General ClickHouse repo; no direct observability/GCS details. | high |
| `plans/grpc-for-gcs/investigation.md` | Prior GCS native design context | Native GCS was explicit opt-in; current behavior must preserve GCS-as-`s3`; observability naming was noted for later. | high |
| `plans/grpc-for-gcs/plan.md` | Existing phase plan context | P01-P03 establish client, native type/config, and core read/write disk; P05 includes explicit table-function gRPC path. | high |
| `plans/grpc-for-gcs/*notes.md`, `*tasks.md`, `*review.md` via `rg` | Current phase evidence | P01 selected direct generated stubs; P02 added native `ObjectStorageType::GCS`; P03 implemented core GCS read/write buffers. | medium |
| `src/Common/ProfileEvents.cpp` | S3/Azure parity baseline | Rich S3/DiskS3/read-buffer/write-buffer/throttler event vocabulary; Azure has distinct provider events; no GCS events. | high |
| `src/IO/S3/PocoHTTPClient.cpp` | S3 aggregate request and throttler instrumentation | Maps request kind to read/write events, errors, throttles, redirects; wires DiskS3 request throttler profile events. | high |
| `src/IO/S3/Client.cpp` | S3 retry attempt accounting | Increments S3/DiskS3 request attempts and retryable errors around retry loop. | high |
| `src/IO/ReadBufferFromS3.cpp` | S3 read buffer parity | Increments API `S3GetObject`/`DiskS3GetObject`, read bytes/errors/timing, throttling scope, and blob storage read log. | high |
| `src/IO/WriteBufferFromS3.cpp` | S3 write buffer parity | Increments write bytes/timing/errors, operation counters, write throttling scope, and blob storage upload/multipart logs. | high |
| `src/Disks/DiskObjectStorage/ObjectStorages/S3/S3ObjectStorage.cpp` | S3 disk object-storage parity | Creates blob storage log writer, increments list/delete events, passes disk delete event to S3 delete helpers. | high |
| `src/IO/AzureBlobStorage/PocoHTTPClient.cpp`; `src/Disks/IO/ReadBufferFromAzureBlobStorage.cpp`; Azure object-storage files | Azure naming and provider parity precedent | Azure has provider-specific request, operation, throttler, read-buffer, and blob-log instrumentation. | high |
| `src/IO/GCS/GCSClient.*` | Native GCS client surface | Direct generated gRPC stubs with five core RPCs; status mapping and fake test seam exist. | high |
| `src/IO/GCS/GCSStatus.*` | Native GCS error model | Maps gRPC/cloud statuses to internal status codes; retryable helper exists but no usage found. | high |
| `src/Disks/DiskObjectStorage/ObjectStorages/GCS/GCSObjectStorage.*` | Native GCS disk implementation | Implements read/write/list/delete/metadata using gRPC client; no profile events or blob log usage observed. | high |
| `src/Common/BlobStorageLogWriter.h`; `src/Interpreters/BlobStorageLog.h` | Blob storage log schema | Existing log supports `Upload`, `Delete`, multipart lifecycle, and `Read`; no GCS-specific schema required for basic parity. | high |

## External research consulted

| Source | Why consulted | Key findings | Plan impact |
|---|---|---|---|
| None newly consulted | Repository and existing prior GCS plan contain enough evidence for this observability investigation. | No external API uncertainty was needed after user clarified scope. | Future phase-plan may consult GCS gRPC status/retry docs while designing retry policy. |

## Current state

The branch contains a native GCS gRPC foundation and disk object-storage implementation. `src/IO/GCS/GCSClient.*` wraps generated `google.storage.v2` gRPC stubs for metadata, listing, deletion, streaming reads, and streaming writes. `src/Disks/DiskObjectStorage/ObjectStorages/GCS/GCSObjectStorage.*` implements native object storage behavior with nested `GCSReadBuffer` and `GCSWriteBuffer`.

The S3 implementation is much more observable. It exposes provider-level read/write request counters, disk-specific duplicates, API operation counters, request throttler counters, retry attempt counters, retryable-error counters, buffer byte/timing/error counters, generic remote bandwidth throttling scopes, and blob storage log records. Azure independently follows the same broad pattern with `Azure*`, `DiskAzure*`, and `ReadBufferFromAzure*` names. Native GCS currently lacks equivalent instrumentation and lacks observed retry/throttle behavior.

## Compatibility surface

| Surface | Current behavior | Compatibility risk | Plan implication |
|---|---|---|---|
| `ProfileEvents` enum/list | No `GCS*`, `DiskGCS*`, `ReadBufferFromGCS*`, or `WriteBufferFromGCS*` events. | Adding many events changes exported system/event surfaces and dashboard vocabulary. | Add distinct GCS names, following Azure/S3 precedent and user preference. |
| `system.query_log.ProfileEvents` / `system.events` | Native GCS work is mostly invisible through provider-specific counters. | Operators cannot diagnose native GCS production issues; missing events hide regressions. | Add query-visible and server-visible counters for GCS gRPC. |
| Blob storage log | Existing schema supports read/upload/delete/multipart event types; native GCS does not appear to use writer. | Missing object-level audit/performance records for native GCS disk operations. | Reuse existing log schema where possible; avoid schema changes unless required. |
| S3/GCS-as-`s3` dashboards/queries | Existing compatibility paths emit S3 events. | Native GCS distinct names require dashboard updates but avoid false S3 attribution. | Keep existing S3 events unchanged; native GCS gets distinct events. |
| Azure precedent | Azure has `Azure*`, `DiskAzure*`, and `ReadBufferFromAzure*`. | Diverging from provider-specific naming would make GCS inconsistent. | Use Azure/S3 naming pattern for GCS. |
| GCS status model | `isRetryableStatus` exists but is unused. | Adding retries changes behavior and write/idempotency risk. | Include retry behavior design, attempt counters, retryable-error counters, and safety validation. |
| Request throttling | S3 and Azure have request throttler profile events; native GCS has no equivalent observed. | Adding throttling behavior may affect throughput; not adding it conflicts with user goal. | Include provider request throttlers and matching `GCS*RequestThrottler*`/`DiskGCS*RequestThrottler*` events. |
| Generic remote bandwidth throttling | S3 read/write paths use read/write throttling scopes; native GCS does not appear to. | Remote bandwidth limits may not apply consistently across providers. | Include generic remote bandwidth throttler integration where GCS reads/writes move bytes. |
| Future `gcs` table-function gRPC path | Prior `grpc-for-gcs` plan includes future explicit table-function gRPC path. | If disk-only observability is added, table-function gRPC may diverge later. | Plan shared GCS event vocabulary for disk and future explicit table-function gRPC path. |

## Constraints

- C001: Only `plans/gcs-grpc-observability-event-parity/investigation.md` may be written.
  Source: user prompt.
  Impact: This investigation stops after updating the handoff; no implementation or plan file changes.

- C002: Do not implement code, modify plan/task/notes/review files, stage, or commit.
  Source: user prompt.
  Impact: Findings must be plan-ready, not executable tasks or code edits.

- C003: Existing GCS-as-`s3` behavior must remain preserved unless explicitly changed by a later plan.
  Source: `plans/grpc-for-gcs/investigation.md` and `plan.md`.
  Impact: Native GCS should not reuse S3 events in ways that imply S3/XML internals or alter S3 compatibility paths.

- C004: Distinct GCS event names are required.
  Source: user answer Q002.
  Impact: Phase-plan should define a `GCS`/`DiskGCS`/`ReadBufferFromGCS`/`WriteBufferFromGCS` event family.

- C005: Future explicit `gcs` table-function gRPC path is in observability scope.
  Source: user answer Q001.
  Impact: Phase-plan should avoid disk-only abstractions that cannot be reused by table-function gRPC.

- C006: Retry/throttle behavior is in scope, not just accounting.
  Source: user answer Q003.
  Impact: Phase-plan must include safe retry/throttle behavior design and verification, especially for writes.

- C007: Future C++ implementation must follow repository instructions.
  Source: `AGENTS.md`.
  Impact: Future phase-plan should mention Allman style, fail-close behavior, build/test log handling, and no silent fallback.

- C008: Existing unrelated worktree changes must be left untouched.
  Source: `git status --short` and user prompt.
  Impact: This investigation does not attribute or modify `contrib/liburing`, `contrib/sysroot`, or `tmp/`.

## Non-goals

- Implementing GCS observability code.
- Creating or updating `plans/gcs-grpc-observability-event-parity/plan.md`.
- Creating phase task, notes, or review files.
- Adding docs or tests now.
- Changing existing GCS-as-`s3` behavior.
- Proving real GCS production performance or connectivity.
- Reusing S3 event names for native GCS gRPC.
- Deciding exact per-line increment sites; only planning-level surfaces and tradeoffs are in scope.

## Grey areas

- G001: Exact final event list.
  Severity: amber
  Blocking: no
  Why it matters: Too few events leave blind spots; too many create noisy public surface.
  Resolution path: Phase-plan should define a parity matrix against S3/Azure and mark non-applicable S3 events such as multipart/redirects.
  Safe default if non-blocking: Add distinct aggregate request, operation, buffer, retry/throttle, and request-throttler events for GCS; omit S3-only multipart events until GCS has equivalent behavior.

- G002: Exact retry policy and retryable operation set.
  Severity: red
  Blocking: no for investigation, yes for implementation planning detail
  Why it matters: Retrying streaming writes or partially consumed reads incorrectly can change correctness.
  Resolution path: Phase-plan should require idempotency analysis per RPC: metadata/list/delete/read/write stream creation/write stream finish.
  Safe default if non-blocking: Start with safe retry boundaries for idempotent reads/metadata/list and carefully scoped write retries before any bytes are committed; fail closed where safety is unclear.

- G003: Exact throttle category mapping for gRPC statuses.
  Severity: amber
  Blocking: no
  Why it matters: GCS gRPC `RESOURCE_EXHAUSTED` is currently mapped to `Unavailable`; S3 has explicit 429/503 throttling counters.
  Resolution path: Phase-plan should require validating status mapping and distinguishing throttling from general retryable errors if supported by gRPC status codes/details.
  Safe default if non-blocking: Treat `ResourceExhausted` as throttling and `Unavailable`/`DeadlineExceeded` as retryable errors, preserving raw error message in blob log.

- G004: Blob storage log granularity for streaming reads/writes.
  Severity: green
  Blocking: no
  Why it matters: Logging each range fetch/chunk gives detail but could be noisy; aggregate per object operation may hide micro-pathologies.
  Resolution path: Phase-plan should pick per-request logging for parity with S3 read requests and per-stream upload logging for GCS writes unless tests show excessive volume.
  Safe default if non-blocking: Log each `ReadObject` request and each `WriteObject` stream as existing `Read`/`Upload` events.

- G005: Table-function gRPC timing relative to this plan.
  Severity: green
  Blocking: no
  Why it matters: The explicit table-function gRPC path may not be implemented yet when observability begins.
  Resolution path: Phase-plan should define shared GCS instrumentation helpers now and a table-function integration/validation phase that can land when the table-function path exists.
  Safe default if non-blocking: Do not block disk observability on table-function implementation; design reusable client-level events.

## Assumptions

- AS001: Distinct GCS event names are required and should follow Azure/S3 naming patterns.
  Confidence: high
  Validation path: User answer Q002 and maintainer review.
  Exported to plan: yes

- AS002: S3 multipart-specific events are not required for current GCS gRPC parity.
  Confidence: high
  Validation path: Confirm current GCS write model remains single `WriteObject` stream during phase-plan.
  Exported to plan: yes

- AS003: Blob storage log schema does not need extension for basic GCS read/upload/delete parity.
  Confidence: high
  Validation path: Phase-plan should verify required fields are enough for GCS bucket/object/path/error/time/bytes.
  Exported to plan: yes

- AS004: Actual retry/throttle behavior is not yet implemented in native GCS, only status classification exists.
  Confidence: high
  Validation path: Re-run usage search before planning/implementation.
  Exported to plan: yes

- AS005: A shared client-level instrumentation layer is preferable so disk and table-function GCS gRPC paths do not diverge.
  Confidence: medium
  Validation path: Phase-plan should inspect current and planned table-function integration boundaries.
  Exported to plan: yes

- AS006: The current worktree's modified submodules and `tmp/` are unrelated user or environment changes.
  Confidence: medium
  Validation path: Ask user only if future work needs to touch those paths.
  Exported to plan: no

## Open questions

- [x] Q001: Should parity target only the native GCS disk/object-storage path now, or also the future explicit `gcs` table-function gRPC path from `plans/grpc-for-gcs/plan.md`?
  Blocking: no
  Plan-shaping: yes
  Asked directly in chat: yes
  Answer: Cover future `gcs` table-function gRPC too.
  Why it matters: It determines whether phase-plan should include table-function observability and tests, or limit itself to disk/native object storage.
  Suggested resolution path: Resolved by user answer; phase-plan should include both surfaces.

- [x] Q002: Should the event namespace be distinct native names such as `DiskGCSReadRequestsCount` / `ReadBufferFromGCSBytes`, or should some counters intentionally reuse existing generic/S3-compatible names for dashboards?
  Blocking: no
  Plan-shaping: yes
  Asked directly in chat: yes
  Answer: Use distinct GCS names; Azure has its own names, so GCS should follow similarly.
  Why it matters: Event names are part of ClickHouse's observable surface; renaming later is painful.
  Suggested resolution path: Resolved by user answer; phase-plan should define distinct GCS event families.

- [x] Q003: Should this plan include implementing actual ClickHouse-level retry/throttle behavior for GCS gRPC, or only accounting for errors/throttles/retries that already happen in the client/RPC layer?
  Blocking: no
  Plan-shaping: yes
  Asked directly in chat: yes
  Answer: Implement retry/throttle behavior as well as observability.
  Why it matters: Behavior changes require more risk analysis and verification than adding counters/logging alone.
  Suggested resolution path: Resolved by user answer; phase-plan should include behavior plus accounting.

## Candidate approaches

### Option A: Disk-first distinct GCS observability parity

Summary:
Add a distinct GCS/DiskGCS event family and blob storage log wiring for native disk/object-storage GCS operations first. Cover read/write bytes and timings, request counts, operation counters, error/throttle/retry classification, generic remote throttling scopes, and blob storage logs for read/upload/delete. Leave table-function gRPC observability for the future phase that introduces that path.

Pros:
- Aligns with current implemented native GCS disk code.
- Keeps scope smaller.
- Preserves provider identity with clean `GCS`/`DiskGCS` names.

Cons:
- Conflicts with user answer Q001 because future `gcs` table-function gRPC is in scope.
- Future table-function gRPC path could repeat work or initially be unobservable.

Risks:
- Too narrow for the stated goal after clarification.

Best when:
- Not recommended now; only useful if scope must be cut later.

### Option B: Full native GCS surface observability parity

Summary:
Plan observability for both native disk/object-storage GCS and the future explicit table-function gRPC path, using one coherent distinct GCS event vocabulary and shared instrumentation helpers.

Pros:
- Matches user answer Q001.
- Avoids divergent observability designs between disk and table-function GCS.
- Produces a complete native GCS production observability story.

Cons:
- Larger scope and more verification requirements.
- Some table-function work may need to be conditional on the future table-function gRPC path readiness.

Risks:
- Could blur this plan into the unfinished table-function phase from `grpc-for-gcs` if phase boundaries are poor.

Best when:
- The goal is a coherent native GCS observability standard across all explicit native GCS surfaces.

### Option C: Observability-only minimum viable counters/logs

Summary:
Add counters and blob storage log records around current native GCS operations without adding new retry or request-throttle behavior. Classify observed gRPC statuses into error/throttle/retryable buckets where possible.

Pros:
- Lowest behavior risk.
- Faster route to query-visible production diagnostics.

Cons:
- Conflicts with user answer Q003 because retry/throttle behavior is in scope.
- `retry attempts` may remain zero or misleading without an actual retry loop.

Risks:
- Users may infer that retry counters mean retry support exists.

Best when:
- Not recommended now; only useful as a fallback if behavior hardening is split out later.

### Option D: Behavior plus observability hardening

Summary:
Plan retry loops, request/bandwidth throttling integration, and all related counters/logging together for native GCS gRPC, with distinct GCS event names and coverage for disk/object-storage plus future table-function gRPC.

Pros:
- Matches user answers Q001-Q003.
- Error/throttle/retry accounting matches actual behavior.
- Aligns with S3/Azure's mature provider observability and user-confirmed production-quality goal.

Cons:
- More complex and riskier; changes runtime behavior.
- Requires careful idempotency analysis for streaming writes, deletes, and partial reads.
- Needs deeper tests for retryable statuses, throttles, partial streams, cancellation, and throttler timing.

Risks:
- Retrying writes incorrectly could duplicate or corrupt operations if GCS write semantics are misunderstood.
- Throttling may reduce throughput and complicate performance comparisons.

Best when:
- The goal is production hardening now, not just observability scaffolding.

## Recommended planning direction

Recommendation:
Use Option D with Option B's full-surface scope: distinct native GCS event names, blob storage log parity, and actual retry/throttle behavior plus accounting for both native disk/object-storage and future explicit `gcs` table-function gRPC.

Rationale:
- User confirmed future table-function gRPC is in scope.
- User confirmed distinct GCS event names, with Azure as precedent.
- User confirmed retry/throttle behavior is in scope, not only accounting.
- Repository evidence shows native GCS lacks `ProfileEvents`, blob logs, retry usage, and throttling scopes today.
- S3 and Azure show mature provider-specific observability patterns to mirror without conflating native GCS with S3/XML.

Avoid:
- Reusing `S3*`/`DiskS3*` counters for native GCS gRPC.
- Adding silent fallback to S3/XML if native GCS observability or retry behavior fails.
- Treating multipart S3 events as required for current GCS streaming writes.
- Implementing retry behavior for writes without idempotency/safety analysis.
- Blocking disk observability on table-function implementation details if shared client-level instrumentation can cover both.

## Suggested plan shape

Potential phases:
- P01 / `event-vocabulary-and-status-model`: Establish distinct GCS/DiskGCS/ReadBufferFromGCS/WriteBufferFromGCS event names, classify S3/Azure parity, and refine GCS status categories for error/throttle/retry accounting.
- P02 / `retry-and-throttling-foundation`: Design and implement safe retry policy boundaries, request throttling, generic remote bandwidth throttling integration, and matching accounting at shared client/buffer layers.
- P03 / `disk-buffer-and-blob-log-parity`: Instrument native GCS disk/object-storage read/write/list/metadata/delete paths, read/write buffers, and blob storage log records.
- P04 / `table-function-gcs-observability`: Ensure the future explicit `gcs` table-function gRPC path uses the same GCS event/logging/retry/throttle machinery when that path is present.
- P05 / `verification-and-compatibility`: Add targeted fake-service tests and regression coverage for success/error/retry/throttle counters, blob storage log rows, and preservation of existing S3/GCS-as-`s3` behavior.

Expected artifacts:
- `src/Common/ProfileEvents.cpp`: likely event definitions.
- `src/IO/GCS/GCSStatus.*`: likely status classification refinements if throttle/retry categories need separation.
- `src/IO/GCS/GCSClient.*`: likely shared request/operation accounting, retry handling, throttler hooks, and fake test support.
- `src/Disks/DiskObjectStorage/ObjectStorages/GCS/GCSObjectStorage.*`: likely disk object-storage, read buffer, write buffer, and blob log wiring.
- `src/Disks/tests/...` or existing GCS test files: likely fake-service/event tests.
- Future table-function GCS gRPC integration files, if present by planning time.
- Existing blob storage log schema: likely reused, not modified.

Verification candidates:
- Unit/fake-service tests proving expected `ProfileEvents` increments for successful `ReadObject`, `WriteObject`, `GetObject`, `ListObjects`, and `DeleteObject` paths.
- Error-path tests for retryable, throttling/resource-exhaustion, not found, permission, and unknown statuses.
- Retry-behavior tests proving attempt counts and retryable-error counts match actual retries.
- Throttler tests proving request-throttler and generic remote bandwidth throttler events increment and sleep accounting is visible.
- Blob storage log tests or scenario tests verifying read/upload/delete rows include disk, bucket, path, bytes, elapsed time, and error details.
- Regression checks proving existing S3/GCS-as-`s3` paths still emit their existing S3 counters and are not changed by native GCS instrumentation.
- Future table-function explicit gRPC tests proving it emits GCS events and does not alter default `gcs` S3/XML behavior.

## Handoff contract for phase-plan

Facts phase-plan may rely on:
- Native GCS gRPC files exist under `src/IO/GCS` and `src/Disks/DiskObjectStorage/ObjectStorages/GCS` and implement core client/object-storage operations.
- S3 observability parity baseline includes aggregate request events, disk-specific events, API operation counters, read/write buffer events, request throttler events, retry events, and blob storage log usage.
- Azure has a distinct provider event namespace, supporting the user's requested distinct GCS names.
- Native GCS currently lacks provider-specific `ProfileEvents`, blob storage log usage, observed retry behavior, and observed throttling-scope integration.
- Blob storage log's existing `Read`, `Upload`, and `Delete` event types can likely cover core native GCS operations without schema changes.
- User requires future table-function gRPC coverage, distinct GCS names, and retry/throttle behavior plus observability.

Facts phase-plan must revalidate:
- Exact current state of native GCS files before planning because this branch is actively evolving.
- Whether any GCS table-function gRPC code exists by the time `/phase-plan` runs.
- Exact maintainer-acceptable GCS event list and descriptions.
- Whether `RESOURCE_EXHAUSTED` can be distinguished in the current `GCSStatus` model rather than collapsed into `Unavailable`.
- Safe retry boundaries for each GCS RPC, especially `WriteObject` streams.

Blocking issues:
- None.

Recommended next command:
- `/phase-plan gcs-grpc-observability-event-parity "We will implement parity for profile event / blob storage log for gcs grpc to ensure production quality implementation. We will support error/throttle/retry accounting. We will make it observable"`

## Readiness gate

Ready for phase-plan: yes

Feedback loop status: satisfied

Reason:
- The initial draft questions were asked directly and answered. The answers resolve all blocking/plan-shaping ambiguities around scope, event names, and retry/throttle behavior. Remaining grey areas are technical design and validation work for `/phase-plan`.

Blocking grey areas:
- None.

Questions for user before planning:
- None.

Safe assumptions if unanswered:
- None; plan-shaping questions were answered.

## Investigation validation

Status: passed

Hard checks:
- User goal captured: pass
- User model documented: pass
- Feedback loop state documented: pass
- Relevant project instructions inspected: pass
- Relevant existing plan/investigation files inspected if present: pass
- Findings cite evidence: pass
- Assumptions separated from facts: pass
- Grey areas marked by severity and blocking status: pass
- Candidate approaches include tradeoffs: pass
- Blocking and plan-shaping questions are asked directly, not only listed: pass
- No implementation tasks included: pass
- No code changes made: pass

Warnings:
- Worktree had pre-existing modified submodules and untracked `tmp/`; this investigation left them untouched.
- No external research was newly consulted because repository evidence, prior native GCS plan, and user answers were sufficient for this observability handoff.

## Investigation change log

- 2026-05-11: Initial investigation created with repository evidence and first direct feedback questions.
- 2026-05-11: Incorporated user answers for table-function scope, distinct GCS event names, and retry/throttle behavior; marked investigation ready for phase planning.
