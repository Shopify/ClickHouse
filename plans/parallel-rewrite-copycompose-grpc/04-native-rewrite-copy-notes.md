# Native rewrite copy Notes

Plan: [plan.md](./plan.md)
Phase: P04 / `04-native-rewrite-copy`

## Implementation context

- Primary implementation target: `src/Disks/DiskObjectStorage/ObjectStorages/GCS/GCSObjectStorage.cpp`, especially `GCSObjectStorage::copyObject` and `GCSObjectStorage::copyObjectToAnotherObjectStorage`.
- `GCSObjectStorage::copyObject` now uses native GCS `RewriteObject` through `rewriteObjectFromGCS`; it no longer opens a `ReadObject` stream plus `WriteObject` stream for same-storage copy.
- `copyObjectToAnotherObjectStorage` uses native rewrite only when the destination is another compatible `GCSObjectStorage`; incompatible GCS or non-GCS destinations keep the generic buffered read/write path.
- Compatibility is intentionally conservative: destination and source native GCS storages must have the same endpoint, credential mode, service account JSON, `user_project`, and insecure-test mode. This avoids stealing cases where separate source-read and destination-write credentials make generic copy valid but destination-client server-side rewrite may not be authorized.
- Native cross-GCS rewrite is issued by the destination `GCSObjectStorage` client, with source bucket/object from the source storage and destination bucket/object from the destination storage.
- `rewriteObjectFromGCS` repeats `RewriteObject` requests with the returned `rewrite_token` until `done=true`; an incomplete response without a token throws instead of falling back.
- Destination `ObjectAttributes` are mapped to the rewrite destination resource. When no attributes are provided, the request omits `destination`, so GCS may preserve source metadata as native rewrite semantics.
- `WriteSettings::object_storage_write_if_none_match="*"` maps to rewrite `if_generation_match=0`; non-`*` `object_storage_write_if_none_match` and `object_storage_write_if_match` fail before issuing native rewrite.
- `GCS::FakeStub::rewriteObject` now enforces create-only destination preconditions in object-map mode and still supports scripted token responses from P01.
- Tests added under `GCSObjectStorageRewriteCopy` cover same-storage rewrite, token iteration, metadata/preconditions, compatible cross-GCS rewrite, conservative incompatible-GCS fallback, non-GCS fallback through `LocalObjectStorage`, same-storage failures, and cross-GCS failures.
- Startup worktree inventory: relevant P04 source paths had no pre-existing diffs. Unrelated local changes under `contrib/liburing`, `contrib/sysroot`, and `tmp/` were left untouched.

## Investigation context

- Investigation file: None
- Relevant findings: None from `plans/parallel-rewrite-copycompose-grpc/investigation.md`; related plan imports `gcs-grpc-perf` F001, F002, F003, F007, and F008 as performance-gap context.
- Relevant constraints: related `gcs-grpc-perf` C002, C003, and C005; native GCS remains explicit `object_storage_type=gcs`; same-GCS native rewrite failures do not silently fall back to generic read/write copy; no high-level client fallback was added.
- Relevant assumptions validated: native same-GCS copy uses `RewriteObject` token iteration until completion; the P01 fake rewrite seam is sufficient for object-storage dispatch and failure tests.
- Relevant open questions/blockers: plan Q001 about exact level-D performance thresholds is non-blocking and belongs to P06; no direct user question blocks P04 completion.

## Decisions from planning

- Use direct generated `google.storage.v2` stubs through `GCS::Client::rewriteObject`; reason: preserves ClickHouse-owned native gRPC semantics and avoids high-level-client fallback behavior.
- Implement same-GCS copy with native `RewriteObject` and fail closed on rewrite failure; reason: generic fallback would hide native errors and misrepresent performance.
- Preserve generic `IObjectStorage` copy for cross-provider or incompatible destinations; reason: P04 owns native GCS-to-GCS rewrite only when one native rewrite client authority is a safe representation of the copy.
- Defer real service performance validation to P06; reason: Tier 1 fake tests prove deterministic rewrite token and dispatch behavior, while throughput depends on real GCS conditions.

## Assumptions

- Conservative endpoint/authority matching is an acceptable P04 compatibility boundary for native cross-GCS rewrite. Confidence: medium; P05 can broaden it only with explicit credential/authorization evidence.
- Destination-client rewrite is the right native direction for compatible GCS-to-GCS copies because the operation creates the destination object. Confidence: medium; fake tests validate request shape, while real authorization behavior remains for P06/P05 environments.
- `GCS::FakeStub` scripted `rewrite_object_responses` are enough to validate token iteration and request sequencing. Confidence: high; covered by `RewriteTokenIterationUsesContinuationToken`.
- Fake object-map tests are sufficient for Tier 1 copy correctness validation. Confidence: high; real service behavior and performance remain deferred to P06.

## Risks

- Real GCS authorization may allow more cross-bucket/cross-project rewrite cases than the conservative compatibility check. Mitigation: generic read/write remains correct for incompatible cases; P05/P06 can broaden native dispatch with evidence.
- Native rewrite default metadata preservation differs from the old generic GCS read/write copy when no `ObjectAttributes` are supplied. Mitigation: explicit attributes are mapped and tested; broader metadata compatibility can be evaluated in P05 if user-visible.
- Fake token responses do not create destination object-map entries. Mitigation: token tests assert request sequencing, while object-map rewrite tests assert final bytes and metadata.
- No real GCS validation was run in P04. Mitigation: P06 owns same-region GCE/GCS copy/rewrite benchmarking and real service validation.

## Deferred or future work

- P05 owns broader integration compatibility across disk settings, metadata modes, caches, read-only behavior, and metric readiness; trigger: P04 implementation and review all-clear complete.
- P06 owns real GCS copy/rewrite benchmarks and comparison against GCS-as-`s3`; trigger: P05 compatibility validation complete.
- Broadening native cross-GCS rewrite compatibility beyond endpoint/authority equality is deferred; trigger: evidence that real GCS authorization supports safe server-side rewrite without stealing generic-copy cases.

## Handoff summary

Current status:
- P04 implementation, Tier 1 verification, critique, reviewer all-clear, task bookkeeping, and commit are complete.

Completed artifacts:
- `src/Disks/DiskObjectStorage/ObjectStorages/GCS/GCSObjectStorage.cpp`: native rewrite helper, same-storage rewrite dispatch, compatible cross-GCS rewrite dispatch, conservative compatibility gate, and shared write-setting validation.
- `src/Disks/DiskObjectStorage/ObjectStorages/GCS/GCSObjectStorage.h`: private native rewrite helper declarations.
- `src/IO/GCS/GCSClient.cpp`: fake rewrite create-only precondition enforcement.
- `src/Disks/tests/gtest_gcs_object_storage_config.cpp`: P04 fake/unit coverage for rewrite success, token iteration, metadata/preconditions, compatible and incompatible destinations, non-GCS generic dispatch, and fail-closed failures.
- `plans/parallel-rewrite-copycompose-grpc/04-native-rewrite-copy-tasks.md`: completed task list, including review follow-up tasks.
- `plans/parallel-rewrite-copycompose-grpc/04-native-rewrite-copy-notes.md`: updated implementation notes.
- `plans/parallel-rewrite-copycompose-grpc/04-native-rewrite-copy-review.md`: verification, critique, findings, and reviewer all-clear.

Key decisions:
- Native same-GCS copy uses `RewriteObject` and does not silently fall back after native rewrite failure.
- Compatible GCS-to-GCS rewrite uses the destination client and requires matching endpoint/authority settings.
- Generic read/write copy remains the path for non-GCS destinations and incompatible native GCS destinations.

Assumptions:
- P04 fake/unit coverage is sufficient for correctness before P05 compatibility and P06 real GCS validation. Confidence: high.
- Conservative compatibility is safer than over-broad native rewrite dispatch. Confidence: medium.

Uncertainties:
- Whether production should broaden native cross-GCS rewrite compatibility after real authorization and performance evidence; defer to P05/P06.

Next likely work:
- Stop after P04. Run `/phase-tasks parallel-rewrite-copycompose-grpc 05-integration-compatibility` only when ready for the next phase.
