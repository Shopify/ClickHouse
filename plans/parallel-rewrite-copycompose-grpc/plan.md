# Native GCS gRPC parallel write, rewrite, copy, and compose plan

Plan status: ready

Plan slug: parallel-rewrite-copycompose-grpc

## User goal

Implement level D: native GCS gRPC read-path optimizations, parallel write support, and native server-side rewrite/copy/compose support for the GCS object storage backend.

## Investigation baseline

Investigation file: None

Investigation status: not run

Imported findings:
- None from `plans/parallel-rewrite-copycompose-grpc/investigation.md`; no investigation exists for this plan slug.
- Related evidence imported from `plans/gcs-grpc-perf/investigation.md`: F001, F002, F003, F007, F008.
- Related evidence imported from `plans/grpc-for-gcs/03-core-rw-disk-notes.md`: native GCS read is synchronous per range, write is one `WriteObject` stream, and copy uses generic read/write.

Imported constraints:
- Related `gcs-grpc-perf` C002: performance claims must be tied to same-region GCE/GCS direct-connectivity conditions.
- Related `gcs-grpc-perf` C003: preserve existing GCS-as-`s3` behavior by default.
- Related `gcs-grpc-perf` C005: benchmark data needs S3-like `GCS*` profile events or external telemetry; the user stated `GCS*` event work is separate.
- Project constraint from `AGENTS.md`: avoid silent fallback paths; fail closed where native operations fail.

Imported assumptions or grey areas:
- Related `gcs-grpc-perf` AS001: existing GCS-as-`s3` XML path is the primary performance baseline.
- Related `gcs-grpc-perf` AS004: same-region GCE service-account direct connectivity is available or can be provisioned for benchmark validation.
- Related `gcs-grpc-perf` AS005: S3-like `GCS*` profile events will be available shortly through separate work.
- Grey area from current source: generated gRPC/protobuf surfaces include operations beyond current `GCS::IStub`, but the current ClickHouse wrapper exposes only `GetObject`, `ListObjects`, `DeleteObject`, `ReadObject`, and `WriteObject`.

Imported blockers:
- None.

Planning response:
- This plan is created in legacy mode because no investigation exists for this slug, but it is grounded in the existing `gcs-grpc-perf` investigation, prior `grpc-for-gcs` phase notes, and direct source inspection.
- The plan expands beyond the read-heavy benchmark scope from `gcs-grpc-perf` because the user explicitly requested level D implementation: read path, write parallelism, and native rewrite/copy/compose.
- The plan does not include implementing `GCS*` profile events because the user said those are being implemented separately; it includes a readiness/verification dependency on those events.

## Problem statement

Native GCS gRPC currently provides correctness-oriented core object storage operations, but the implementation has major performance gaps: sequential reads open a new bounded `ReadObject` stream per buffer and copy through intermediate strings; writes use a single `WriteObject` stream and report `supportParallelWrite=false`; same-provider copies use generic buffered read/write instead of GCS server-side rewrite; and compose is not exposed through the ClickHouse-owned GCS client seam. This plan turns those known gaps into reviewable implementation phases while preserving existing GCS-as-`s3` behavior and the explicit native GCS object storage type.

## Non-goals

- Changing existing `type=s3` GCS behavior or the default `gcs` table function behavior.
- Implementing S3-like `GCS*` profile events; that is separate work, but this plan depends on their availability for verification.
- Making table-function native gRPC support part of this plan.
- Solving unrelated `plain` / `plain_rewritable` metadata-mode feature gaps unless a planned phase needs a narrow compatibility fix for core object operations.
- Implementing backup-specific GCS APIs beyond making `IObjectStorage` copy paths correct and efficient.
- Adding broad configuration migrations or automatic upgrades of existing disks.
- Treating local MinIO/mock results as proof of real GCS gRPC performance.

## Constraints

- Only `plans/parallel-rewrite-copycompose-grpc/plan.md` is created by this prompt; implementation files are for later `/phase-work` phases.
- Native GCS must remain an explicit opt-in object storage type and must not silently alter GCS-as-`s3` users.
- Native same-GCS copy/rewrite failures must not silently fall back to generic read/write copy; fail closed and surface the error. Cross-provider copy may continue using generic `IObjectStorage` copy where no native GCS target is involved.
- Direct generated `google.storage.v2` stubs remain the default implementation direction unless a phase proves a high-level client is required and fail-closed semantics are preserved.
- Parallel upload via compose must not expose a partially written destination object as successful; temp objects must be namespaced, tracked, and cleaned up or made discoverable for cleanup.
- Benchmarks and performance claims must capture same-region GCE/GCS direct-connectivity proof.
- Real tests and builds must follow project logging rules: build/test output redirected to build logs and analyzed by subagents in implementation phases.

## Assumptions

- `google.storage.v2` generated stubs in the build can expose `ComposeObject`, `RewriteObject`, `StartResumableWrite`, or bidirectional write operations if the ClickHouse `GCS::IStub` seam is extended. Confidence: medium. Validation: P01 direct compile/source verification.
- Parallel writes should use compose-backed temporary chunk uploads by default, with P01 allowed to switch to a resumable/bidi hybrid only if it proves safer for ClickHouse write semantics. Confidence: medium. Validation: P01/P03 design validation and fake tests.
- `ComposeObject` has a 32-source limit per call; larger writes require a compose tree and temp-object cleanup. Confidence: high. Validation: P01 external/API check and P03 tests.
- Native same-GCS copy should use `RewriteObject` with rewrite-token iteration until completion. Confidence: high. Validation: P04 fake tests and real GCS scenario.
- Read-path optimization should preserve `seek`, `readBigAt`, bounded reads, and `getRemoteFileSize` contracts already covered by fake tests. Confidence: high. Validation: P02 tests.
- `GCS*` profile events are available by the time performance verification begins. Confidence: medium. Validation: P06 readiness check; if absent, use external telemetry and mark perf verification degraded.

## Open questions

- [ ] Q001: What exact throughput or latency threshold defines success for level D?
  Blocking: no
  Plan-shaping: no
  Asked directly in chat: no
  Safe assumption or validation path: Collect before/after metrics and compare against existing GCS-as-`s3`; defer a numeric pass/fail threshold to benchmark review.



## Acceptance criteria

- [ ] A001: Native GCS client seam exposes and tests the gRPC operations needed for optimized reads, parallel writes, compose, and rewrite. Mapped phases: P01.
- [ ] A002: Sequential `MergeTree` reads no longer require one full `ReadObject` RPC per buffer refill and avoid the current intermediate `String` copy on the normal path. Mapped phases: P02.
- [ ] A003: Native GCS write path can use parallel upload for large objects, reports `supportParallelWrite=true` only when the implementation is actually safe, and preserves rewrite-only semantics for final objects. Mapped phases: P03.
- [ ] A004: Parallel write failure paths do not leave a visible successful destination object and have deterministic temp-object cleanup or cleanup evidence. Mapped phases: P03, P05.
- [ ] A005: Native same-GCS copy uses `RewriteObject` token iteration rather than generic buffered read/write; cross-provider copy remains correct. Mapped phases: P04.
- [ ] A006: Native compose support handles the GCS 32-source limit, metadata/precondition rules, and temp object lifecycle needed by parallel writes. Mapped phases: P01, P03, P04.
- [ ] A007: Existing GCS-as-`s3` and default `gcs` table-function behavior are unchanged. Mapped phases: P05, P06.
- [ ] A008: Targeted fake/unit tests, selected integration scenarios, and same-region GCE/GCS performance validation demonstrate the new paths and provide before/after attribution using `GCS*` events or documented fallback telemetry. Mapped phases: P06.

## Relevant context

- `plans/parallel-rewrite-copycompose-grpc/investigation.md`: Not present; plan created in legacy mode.
- `plans/gcs-grpc-perf/investigation.md`: Related evidence baseline for performance gaps and user-selected baseline assumptions.
- `plans/grpc-for-gcs/01-grpc-client-foundation-notes.md`: Explains the prior decision to use direct generated `google.storage.v2` stubs rather than high-level `storage_grpc` because fail-closed native gRPC behavior matters.
- `plans/grpc-for-gcs/03-core-rw-disk-notes.md`: Records current GCS read/write/copy semantics and deferred performance gaps.
- `src/IO/GCS/GCSClient.*`: ClickHouse-owned native GCS client wrapper and fake seam; must grow new operations before object storage can use them.
- `src/Disks/DiskObjectStorage/ObjectStorages/GCS/GCSObjectStorage.*`: Native GCS object storage implementation containing current `GCSReadBuffer`, `GCSWriteBuffer`, `supportParallelWrite=false`, and generic copy.
- `src/Disks/DiskObjectStorage/ObjectStorages/S3/S3ObjectStorage.*`: Baseline object storage behavior for `supportParallelWrite`, copy dispatch, and request settings integration.
- `src/IO/ReadSettings.h`: Read-path settings for buffer sizes, prefetch, remote seek thresholds, cache, throttling, and IO scheduling.
- `src/IO/WriteSettings.h`: Write-path settings, including current `s3_allow_parallel_part_upload`, adaptive write buffer, throttlers, and object-write precondition fields.
- `contrib/google-cloud-cpp/google/cloud/storage/client.h`: High-level reference for GCS `ComposeObject`, `RewriteObject`, and compose-many semantics; useful as API/behavior reference, not necessarily the implementation layer.
- `contrib/google-cloud-cpp/google/cloud/storage/async/writer_connection.h`: Reference for gRPC bidirectional resumable upload constraints and upload-id semantics.
- Google Cloud Storage docs for `ComposeObject`, `RewriteObject`, resumable uploads, and direct connectivity: external behavior and performance constraints.

## Decisions

- D001: Keep native GCS under the existing explicit `object_storage_type=gcs` path.
  Rationale: Existing GCS-as-`s3` behavior is documented and must remain unchanged.
  Alternatives considered: transparently upgrade `type=s3` GCS disks; not acceptable for compatibility.
  Reversible: no
  Affects phases: P01, P03, P04, P05, P06

- D002: Preserve the direct generated-stub client architecture and extend `GCS::IStub` rather than switching wholesale to high-level `storage_grpc`.
  Rationale: Prior foundation selected direct stubs to avoid hidden REST fallback and keep ClickHouse-owned fake seams.
  Alternatives considered: use high-level `google::cloud::storage::Client` for `ComposeObject`, `RewriteObject`, and parallel uploads; useful as reference but not the planning default.
  Reversible: yes, if P01 proves generated stubs cannot safely expose needed operations.
  Affects phases: P01, P03, P04

- D003: Optimize reads before writes/copy because read path is the performance-critical baseline, but include all level-D implementation in this plan.
  Rationale: The prior investigation selected read-heavy `MergeTree` scans, while the new user goal explicitly asks for all level-D support.
  Alternatives considered: split write/copy into a separate plan; rejected because the user requested level D.
  Reversible: yes
  Affects phases: P02, P03, P04, P06

- D004: Implement same-GCS copy with native `RewriteObject`, and do not silently fall back to generic read/write on native rewrite failure.
  Rationale: Generic copy hides native errors and produces misleading performance behavior.
  Alternatives considered: try native rewrite then generic copy fallback; rejected by fail-closed guidance.
  Reversible: no
  Affects phases: P04, P05

- D005: Implement parallel write with a native GCS composition strategy unless P01 proves a safer resumable/bidi approach better satisfies ClickHouse write semantics.
  Rationale: Current single `WriteObject` stream is not parallel; GCS `ComposeObject` provides server-side concatenation but needs temp object and 32-source handling.
  Alternatives considered: parallel client-side chunks through one stream; not actually parallel. High-level `ComposeMany`; may be a reference but conflicts with direct-stub default.
  Reversible: yes
  Affects phases: P01, P03, P06

- D006: Treat `GCS*` profile-event implementation as an external dependency to revalidate, not a deliverable of this plan.
  Rationale: User said that work is being implemented separately.
  Alternatives considered: include metrics implementation phase; rejected to avoid duplicate work.
  Reversible: yes, if P06 readiness finds events absent.
  Affects phases: P01, P06

## Verification ladder

Use the lowest sufficient verification tier for each phase:

- Tier 0 smoke: format, lint, typecheck, targeted unit tests, static checks.
- Tier 1 core: relevant unit or integration test suite.
- Tier 2 behavioral: end-to-end test, migration test, benchmark, scenario, or regression reproduction.
- Tier 3 manual: manual inspection or human review when no automated check exists.

Each phase names its intended tier and why that tier is sufficient.

## Phase overview

| Phase | Slug | Goal | Dependencies | Expected artifacts | Verification tier | Verification |
|---|---|---|---|---|---|---|
| P01 | 01-gcs-client-capabilities | Extend and validate the native GCS client seam for compose, rewrite, resumable/bidi write capability decisions, retries, and fake testing. | none | `src/IO/GCS/GCSClient.*`, `src/IO/tests/gtest_gcs_grpc_client.cpp`, phase notes/review | Tier 0 | Targeted GCS client unit tests and direct object/static compile checks. |
| P02 | 02-read-path-optimization | Replace per-buffer synchronous range fetch behavior with an optimized sequential/range read path preserving existing contracts. | P01 | `src/Disks/DiskObjectStorage/ObjectStorages/GCS/GCSObjectStorage.*`, disk tests | Tier 1 | GCS object storage fake tests plus targeted read-buffer behavior tests. |
| P03 | 03-parallel-compose-write | Implement safe parallel large-object writes using native GCS semantics and compose-backed finalization or P01-selected equivalent. | P01 | GCS write buffer/client changes, temp object cleanup support, tests | Tier 1 | Fake/unit tests for chunking, concurrency handoff, compose tree, cleanup, failures. |
| P04 | 04-native-rewrite-copy | Implement native same-GCS rewrite/copy and compose helper behavior for copy paths. | P01 | GCS object storage copy changes, client rewrite helpers, tests | Tier 1 | Fake/unit tests for rewrite-token iteration, metadata/preconditions, cross-provider behavior. |
| P05 | 05-integration-compatibility | Integrate read/write/copy changes with disk settings, caches, metadata modes that already work, and compatibility surfaces. | P02, P03, P04 | targeted integration adjustments, docs/notes if needed, tests | Tier 1 | Selected object-storage/disk integration scenarios and system surface checks. |
| P06 | 06-performance-validation | Validate level-D behavior and performance against existing GCS-as-`s3` under real GCS direct-connectivity conditions. | P02, P03, P04, P05 | benchmark harness/results under `tmp` or build logs, perf notes/review | Tier 2 | Same-region GCE/GCS benchmark plus `GCS*`/`S3*` metric comparison and direct-connectivity proof. |

## Phases

### P01: GCS client capabilities

Slug: `01-gcs-client-capabilities`

Goal:
Extend the ClickHouse-owned native GCS client wrapper and fake seam so later phases can use direct gRPC operations for `ComposeObject`, `RewriteObject`, and the selected parallel/resumable write primitive without bypassing fail-closed behavior.

Scope:
- Extend `GCS::IStub` and `GCS::Client` for `ComposeObject` and `RewriteObject` direct-stub calls.
- Validate whether `StartResumableWrite` / bidirectional write support is available and useful for ClickHouse write semantics.
- Add fake-stub state and request capture for compose, rewrite, and any selected resumable/bidi write operation.
- Define retry/deadline behavior for unary compose/rewrite calls and token iteration boundaries.
- Record the chosen parallel write primitive for P03.

Out of scope:
- Rewriting `GCSObjectStorage` read/write/copy behavior.
- Implementing profile events.
- Real GCS calls.

Dependencies:
- none

Phase interface:

Inputs:
- Current `src/IO/GCS/GCSClient.*` and generated `google.storage.v2` types.
- D002 direct-stub decision.

Outputs:
- Extended native GCS client seam with fake coverage for operations required by P02-P04.
- A recorded decision on compose-backed temp chunk upload versus resumable/bidi write as the P03 primitive.

Downstream contract:
- P03 and P04 may call client-level compose/rewrite APIs without depending on high-level `storage_grpc` client behavior.
- Fake tests can assert compose/rewrite request shape, token continuation, and failure handling.

Assumptions exported:
- Needed generated protobuf/gRPC operations are available through the current build inputs.
- `GCS*` event names are external and should be revalidated later, not implemented here.

Assumptions not exported:
- That compose-backed writes are definitely the final write strategy; P01 must validate and record the final selection.

Expected artifacts:
- `src/IO/GCS/GCSClient.h`: extended interfaces and fake fields.
- `src/IO/GCS/GCSClient.cpp`: direct-stub wrappers and fake behavior.
- `src/IO/tests/gtest_gcs_grpc_client.cpp`: targeted tests for request metadata, status mapping, compose/rewrite fake behavior, and token handling.
- `plans/parallel-rewrite-copycompose-grpc/01-gcs-client-capabilities-notes.md`: phase notes created later by `/phase-tasks` / `/phase-work`.
- `plans/parallel-rewrite-copycompose-grpc/01-gcs-client-capabilities-review.md`: phase review.

Verification approach:
- Tier: Tier 0
- Method: Targeted GCS client unit tests and direct compile/static checks for changed GCS client files.
- Sufficiency: This phase is an API/seam foundation; fake/unit coverage proves request construction, status propagation, and downstream testability without requiring real GCS.

Completion criteria:
- Native GCS client exposes compose and rewrite operations through ClickHouse-owned wrappers.
- Fake client can simulate successful, partial, and failed compose/rewrite flows.
- P03 write primitive selection is documented with rationale and constraints.
- No high-level client fallback path is introduced.

Risks and rollback:
- Risk: Generated direct stubs do not expose the needed operation cleanly in this build. Mitigation: use P01 to prove availability before downstream work. Rollback: keep existing client seam and mark plan blocked for a build-wrapper decision.
- Risk: High-level client appears easier for compose/rewrite. Mitigation: use it as reference only unless fail-closed behavior is proven. Rollback: retain direct-stub implementation path.

Task decomposition guidance:
- Start from client seam and fake behavior, not `GCSObjectStorage` call sites. Keep request construction helpers small and independently testable.

### P02: Read path optimization

Slug: `02-read-path-optimization`

Goal:
Optimize native GCS reads so sequential `MergeTree` scans no longer issue a new synchronous bounded `ReadObject` RPC for every buffer refill and no longer copy through an intermediate `String` on the normal path.

Scope:
- Redesign `GCSReadBuffer` around a persistent sequential stream or an equivalent prefetch/range strategy that respects `ReadSettings`.
- Preserve `seek`, `readBigAt`, right-bounded reads, remote size reporting, and EOF behavior.
- Use `read_hint`, `remote_fs_buffer_size`, `prefetch_buffer_size`, `remote_fs_prefetch`, `remote_read_min_bytes_for_seek`, throttling, and IO scheduling where they are applicable.
- Avoid extra allocations/copies in the common sequential scan path.
- Keep random/bounded `readBigAt` behavior correct even if it remains range-request based.

Out of scope:
- Parallel writes, rewrite/copy, compose, or metadata-mode expansion.
- Changing `ReadBufferFromS3` behavior.

Dependencies:
- P01 for any client seam adjustments needed by stream lifecycle or fake read tests.

Phase interface:

Inputs:
- P01 client/fake seam.
- Current `GCSReadBuffer` behavior and read-related fake tests.
- `ReadSettings` contract.

Outputs:
- Optimized `GCSReadBuffer` implementation with tests proving fewer request boundaries for sequential reads where applicable.

Downstream contract:
- P06 may benchmark read-heavy `MergeTree` scans using the optimized read path.
- Later phases may rely on unchanged `readObject` external semantics.

Assumptions exported:
- Sequential scans can keep a stream or prefetch context alive until seek/range boundaries require reinitialization.

Assumptions not exported:
- That a persistent stream is always faster than larger bounded range requests; P06 must measure.

Expected artifacts:
- `src/Disks/DiskObjectStorage/ObjectStorages/GCS/GCSObjectStorage.cpp`: updated read buffer implementation.
- `src/Disks/tests/gtest_gcs_object_storage_config.cpp` or a dedicated GCS read-buffer test file: coverage for sequential reads, seeks, bounded reads, `readBigAt`, EOF, and failure paths.
- Phase notes/review files under `plans/parallel-rewrite-copycompose-grpc/`.

Verification approach:
- Tier: Tier 1
- Method: Targeted GCS object storage unit/fake tests plus selected disk object-storage tests that exercise read contracts.
- Sufficiency: Read semantics are core object-storage behavior; fake tests can deterministically assert request counts, offsets, limits, and data returned.

Completion criteria:
- Sequential read tests demonstrate reduced per-buffer request churn or documented reason why larger/prefetched ranges are the chosen alternative.
- Normal read path avoids the known intermediate `String` accumulation and second copy where practical.
- Seek and `readBigAt` behavior remain compatible with existing tests.
- Errors from stream reads and stream finish are surfaced without silent fallback.

Risks and rollback:
- Risk: Persistent streams complicate seek/retry behavior. Mitigation: keep bounded range path for random reads and explicitly reset streams on seeks. Rollback: retain current range path behind the simplest correct branch while preserving direct-copy improvements.
- Risk: Prefetch can hide cache behavior in benchmarks. Mitigation: make behavior traceable through `GCS*` events and settings. Rollback: disable prefetch by default if correctness or observability is weak.

Task decomposition guidance:
- Decompose by behavior contract: sequential scan path, seek/reset path, `readBigAt`, failure/finish handling, and metrics attribution.

### P03: Parallel compose write

Slug: `03-parallel-compose-write`

Goal:
Implement safe native GCS parallel writes for large objects and enable `supportParallelWrite` only when the implementation truly supports concurrent upload work and safe finalization.

Scope:
- Replace or augment `GCSWriteBuffer` with a large-object path that uploads chunks concurrently using the P01-selected native primitive.
- Planning default: upload chunks to temporary GCS objects in parallel and finalize with `ComposeObject`, including compose-tree support for more than 32 components.
- Preserve single-stream/simple write behavior for small objects if it is still the best path.
- Honor relevant `WriteSettings`, throttling, IO scheduling, object write preconditions, adaptive buffer constraints, and read-only checks.
- Ensure temporary object naming is deterministic enough for cleanup and isolated enough to avoid user-key collisions.
- Ensure failures do not leave a visible final object marked as successfully written.

Out of scope:
- Server-side copy/rewrite; P04 owns it.
- Real GCS performance benchmarking; P06 owns it.
- New profile event definitions; use externally implemented `GCS*` events if present.

Dependencies:
- P01 client capabilities and write primitive decision.

Phase interface:

Inputs:
- P01 compose/resumable/bidi client seam.
- Current `GCSWriteBuffer` and fake object map.
- `WriteSettings` and S3 parallel-write behavior as reference.

Outputs:
- Native GCS write path with safe parallel large-object finalization and temp cleanup.
- Explicit `supportParallelWrite` behavior aligned with actual implementation capability.

Downstream contract:
- P05 may test integration with disk operations and metadata modes that already work.
- P06 may benchmark insert/load phases without measuring the old single-stream-only path for large objects.

Assumptions exported:
- Parallel write uses temp objects and compose unless P01 selected a better native primitive with equivalent safety.
- Temp objects are either cleaned up on failure or left in a namespace that can be audited and safely removed.

Assumptions not exported:
- That compose-backed writes are faster for all object sizes; P06 must measure thresholds.

Expected artifacts:
- `src/Disks/DiskObjectStorage/ObjectStorages/GCS/GCSObjectStorage.*`: write buffer and `supportParallelWrite` changes.
- `src/IO/GCS/GCSClient.*`: any additional helper calls required by the selected primitive.
- GCS fake tests for chunk upload, compose tree, temp cleanup, failure modes, object attributes, and write preconditions.
- Phase notes/review files under `plans/parallel-rewrite-copycompose-grpc/`.

Verification approach:
- Tier: Tier 1
- Method: Targeted fake/unit tests verifying parallel write request structure, compose ordering, cleanup behavior, final object contents, metadata, and failure propagation.
- Sufficiency: Parallel write correctness is deterministic with a fake object map; real throughput proof belongs to P06.

Completion criteria:
- Large-object writes can be split, uploaded, finalized, and read back correctly through native GCS fake storage.
- More than 32 chunks are handled through a compose tree or explicitly bounded with clear exceptions.
- Temp object cleanup is tested for success and failure paths.
- `supportParallelWrite` returns `true` only after the safe parallel path is active.
- Existing small writes and empty writes remain correct.

Risks and rollback:
- Risk: Compose tree leaves temp objects on partial failures. Mitigation: scoped cleanup, temp namespace, and failure tests. Rollback: keep `supportParallelWrite=false` until cleanup semantics are reliable.
- Risk: Object preconditions are hard to map onto temp-plus-compose. Mitigation: apply destination preconditions to final compose and source-generation preconditions to temp sources where possible. Rollback: fail closed when requested preconditions cannot be represented.
- Risk: Chunk uploads consume too many concurrent resources. Mitigation: reuse existing thread pool and write throttling patterns. Rollback: cap concurrency or route to single-stream path below thresholds.

Task decomposition guidance:
- Split decomposition around temp-object key design, part upload scheduling, compose-tree finalization, cleanup semantics, and `WriteSettings` integration.

### P04: Native rewrite copy

Slug: `04-native-rewrite-copy`

Goal:
Implement native same-GCS copy using GCS `RewriteObject` token iteration and expose compose helpers needed by copy/compose workflows, while preserving generic cross-provider copy correctness.

Scope:
- Implement `GCSObjectStorage::copyObject` through native `RewriteObject` for same GCS object storage.
- Implement `copyObjectToAnotherObjectStorage` native rewrite when the destination is another compatible `GCSObjectStorage`.
- Iterate rewrite tokens until completion and handle long-running rewrite responses.
- Map object attributes/metadata and destination preconditions where representable.
- Use native compose helper behavior where same-bucket composition is required by write/copy flows.
- Do not silently fall back to generic read/write copy when native same-GCS rewrite fails.

Out of scope:
- Backup-specific UX beyond the object storage copy contract.
- Cross-provider native copies; generic `IObjectStorage` read/write remains correct there.
- Table-function copy behavior.

Dependencies:
- P01 client rewrite/compose capabilities.

Phase interface:

Inputs:
- P01 rewrite/compose seam.
- Current generic GCS copy implementation.
- S3 native copy behavior as reference for dispatch and cross-provider rules.

Outputs:
- Native GCS same-provider rewrite/copy implementation with fake coverage.

Downstream contract:
- P05 may rely on native rewrite for disk move/copy paths involving GCS-to-GCS object storage.
- P06 may include copy benchmark scenarios and label them representative of native GCS copy.

Assumptions exported:
- Same-GCS copy no longer measures generic read/write behavior after P04.

Assumptions not exported:
- That every GCS-to-GCS copy can be represented as a fast single-call rewrite; token iteration may still be required.

Expected artifacts:
- `src/Disks/DiskObjectStorage/ObjectStorages/GCS/GCSObjectStorage.*`: native rewrite copy dispatch.
- `src/IO/GCS/GCSClient.*`: rewrite response handling if not fully complete in P01.
- Fake tests covering same-storage copy, cross-GCS copy, token continuation, metadata, not found, permission errors, and no-silent-fallback behavior.
- Phase notes/review files under `plans/parallel-rewrite-copycompose-grpc/`.

Verification approach:
- Tier: Tier 1
- Method: Targeted fake/unit tests for rewrite-token state machine and `IObjectStorage` copy dispatch behavior.
- Sufficiency: Copy semantics and token iteration can be simulated deterministically; real service validation is deferred to P06.

Completion criteria:
- Same-provider GCS copy path issues native rewrite requests and completes token iteration.
- Native rewrite failures surface as exceptions and do not silently perform generic buffered copy.
- Cross-provider copy still uses the generic read/write path and remains correct.
- Existing copy tests are updated or extended to prove native request usage.

Risks and rollback:
- Risk: Rewrite preconditions and metadata mapping differ from ClickHouse object attributes. Mitigation: map only representable semantics and fail closed for unsupported ones. Rollback: reject native rewrite for unsupported options with a clear exception.
- Risk: Long-running rewrites need resumability beyond a single query lifetime. Mitigation: P04 may implement blocking token iteration first and document that persistent checkpointing is out of scope unless required. Rollback: cap/reject unsupported long-running cases rather than falling back silently.

Task decomposition guidance:
- Keep rewrite state machine separate from `GCSObjectStorage` dispatch so it can be tested without disk setup.

### P05: Integration compatibility

Slug: `05-integration-compatibility`

Goal:
Integrate the optimized read, parallel write, compose, and rewrite paths with existing disk/object-storage surfaces while preserving compatibility and making unsupported combinations explicit.

Scope:
- Verify native GCS disk behavior for local metadata after read/write/copy changes.
- Revalidate interactions with cache wrappers, filesystem/page cache settings, throttling, IO scheduling, read-only disks, and startup/shutdown behavior.
- Confirm existing GCS-as-`s3` and default `gcs` table-function behavior are unchanged.
- Confirm `plain` and `plain_rewritable` modes are either unaffected or explicitly deferred with clear exceptions if still not supported.
- Confirm `GCS*` profile-event availability or document fallback telemetry requirements for P06.

Out of scope:
- Implementing table-function native gRPC routing.
- Implementing profile-event definitions unless external event work is absent and the phase is explicitly re-scoped.
- Full CI matrix execution.

Dependencies:
- P02, P03, P04.

Phase interface:

Inputs:
- Completed read/write/copy implementations.
- Existing disk tests and integration test surfaces.
- External `GCS*` profile-event implementation status.

Outputs:
- Compatibility validation and targeted fixes needed before real performance validation.

Downstream contract:
- P06 may benchmark native GCS without rediscovering basic disk/copy/write compatibility issues.
- P06 has a known metric collection contract.

Assumptions exported:
- Existing GCS-as-`s3` behavior remains unchanged.
- Native GCS local-metadata disk behavior is stable enough for performance validation.

Assumptions not exported:
- That all metadata modes are supported unless explicitly verified.

Expected artifacts:
- Targeted updates to GCS unit/integration tests if needed.
- Compatibility notes in phase notes/review.
- Optional docs or test config changes only if implementation behavior becomes user-visible.
- Phase notes/review files under `plans/parallel-rewrite-copycompose-grpc/`.

Verification approach:
- Tier: Tier 1
- Method: Targeted unit tests plus selected integration scenarios for GCS/S3 disk behavior and cache interaction. Use real-GCS tests only as optional pre-P06 smoke because P06 owns behavioral performance validation.
- Sufficiency: Compatibility requires exercising ClickHouse disk surfaces beyond isolated fake client tests, but does not require full benchmark infrastructure.

Completion criteria:
- Existing native GCS fake/disk tests pass with new read/write/copy behavior.
- Existing GCS-as-`s3` paths remain untouched by source inspection and relevant tests.
- Cache/read-only/throttling/precondition behavior has targeted coverage or documented unsupported exceptions.
- P06 metric readiness is resolved: `GCS*` events present or fallback telemetry defined.

Risks and rollback:
- Risk: Integration finds deferred metadata modes need more work. Mitigation: keep plan scoped to already-supported local metadata unless small compatibility fixes are necessary. Rollback: document explicit non-support and block only affected benchmark cases.
- Risk: Event work is not ready. Mitigation: define fallback external telemetry for P06. Rollback: mark performance attribution degraded, not implementation blocked.

Task decomposition guidance:
- Decompose around compatibility surfaces, not around broad test suites: disk local metadata, cache wrapper, read-only/preconditions, S3 compatibility, event readiness.

### P06: Performance validation

Slug: `06-performance-validation`

Goal:
Validate that the level-D implementation works and improves or explains native GCS performance under same-region GCE/GCS direct-connectivity conditions compared with existing GCS-as-`s3` XML.

Scope:
- Run read-heavy `MergeTree` scan benchmarks from `gcs-grpc-perf` scope.
- Add write-heavy insert/merge benchmarks to validate parallel write support.
- Add same-GCS copy/rewrite benchmarks to validate native rewrite/copy and label them representative only after P04.
- Capture direct-connectivity proof, VM zone, bucket location, ClickHouse build options, relevant settings, cache state, object sizes, part counts, `S3*`/`GCS*` events, CPU/allocation profiles, and query logs.
- Compare native GCS against existing GCS-as-`s3` XML on the same GCE VM/bucket/region.

Out of scope:
- Treating MinIO/mock results as cloud performance proof.
- Establishing a permanent CI performance job unless separately requested.
- Making broad user-facing performance claims without direct-connectivity evidence.

Dependencies:
- P02, P03, P04, P05.

Phase interface:

Inputs:
- Completed implementations and compatibility validation.
- Available same-region GCE/GCS environment and credentials.
- `GCS*` metrics or fallback telemetry.

Outputs:
- Benchmark evidence and bottleneck attribution for read, write, and copy/rewrite paths.
- Recommendations for follow-up tuning, thresholds, or separate plans if results are inconclusive.

Downstream contract:
- Future work may rely on measured performance deltas and known remaining bottlenecks.

Assumptions exported:
- Benchmarks with direct-connectivity proof are representative of the target performance envelope.

Assumptions not exported:
- That results generalize to non-GCE, cross-region, Private Service Connect, or non-service-account environments.

Expected artifacts:
- Benchmark harness/config under `tmp` or `tests/performance` if phase work chooses in-tree placement.
- Benchmark logs/results under `tmp` or build logs.
- Phase notes/review files under `plans/parallel-rewrite-copycompose-grpc/`.

Verification approach:
- Tier: Tier 2
- Method: End-to-end same-region GCE/GCS scenarios with direct-connectivity diagnostics plus metric and profile comparison against GCS-as-`s3` XML.
- Sufficiency: Level-D work is performance-sensitive and cloud-path dependent; only real GCS behavioral benchmarks can validate the complete outcome.

Completion criteria:
- Direct-connectivity diagnostic evidence is captured.
- Native GCS read, write, and copy/rewrite benchmark results are captured with comparable GCS-as-`s3` baselines.
- `GCS*` and `S3*` event comparisons or fallback telemetry explain request counts, bytes, latency, and errors.
- Results either show improvement, identify remaining bottlenecks with evidence, or recommend rollback/follow-up with clear rationale.

Risks and rollback:
- Risk: Direct connectivity cannot be proven. Mitigation: still run functional smoke, but mark performance validation blocked/degraded. Rollback: do not claim performance success.
- Risk: Native implementation improves one path but regresses another. Mitigation: phase review separates read/write/copy results. Rollback: disable or gate the regressed path while preserving correct paths.
- Risk: Benchmark noise obscures results. Mitigation: repeated runs, fixed cache policy, captured environment, and same-VM/bucket comparison. Rollback: rerun with narrower workload and more instrumentation.

Task decomposition guidance:
- Decompose into environment proof, read benchmark, write benchmark, copy/rewrite benchmark, metric extraction, and result synthesis.

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
- If `investigation.md` exists, imported findings, constraints, assumptions, grey areas, and blockers are reflected or explicitly rejected with rationale: n/a
- Blocking or plan-shaping open questions are asked directly in the chat response, or plan status is `blocked`: pass
- Plan contains no implementation task checklist: pass
- Plan contains no generic filler: pass

Warnings:
- No investigation exists for this specific plan slug; the plan uses related `gcs-grpc-perf` and `grpc-for-gcs` evidence plus direct source inspection.
- Existing worktree shows modified `contrib/liburing` and `contrib/sysroot`; they were not touched.
- Exact numeric performance success threshold is not specified; P06 must collect evidence and define recommendation criteria in review.
- `GCS*` profile events are assumed to arrive from separate work; if absent, P05/P06 must use fallback telemetry or re-scope.

## Review and handoff expectations

- Each phase must produce `<phase-slug>-review.md` before completion.
- Review findings that require work must become tasks before the next phase starts.
- Notes must capture assumptions, decisions, uncertainties, and handoff summary.

## Plan change log

- 2026-05-11: Initial plan created for level-D native GCS gRPC read optimization, parallel write, rewrite/copy, and compose support.

## Plan maintenance

- Update this plan only when scope, phase order, acceptance criteria, or constraints change.
- Record every material plan change in the plan change log.
- Do not use this plan as a task list.
