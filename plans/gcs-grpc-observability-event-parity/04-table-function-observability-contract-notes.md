# Table-function observability contract Notes

Plan: [plan.md](./plan.md)
Phase: P04 / `04-table-function-observability-contract`

## Implementation context

- `src/TableFunctions/TableFunctionObjectStorage.cpp` currently registers `gcs` as `TableFunctionObjectStorage<GCSDefinition, StorageS3Configuration>` under `USE_AWS_S3`, which is the existing S3/XML-compatible table-function path.
- `src/Storages/ObjectStorage/StorageObjectStorageDefinitions.h` defines `GCSDefinition` with `name = "gcs"`, `storage_engine_name = "GCS"`, and `object_storage_type = "gcs"`.
- `src/Storages/ObjectStorage/registerStorageObjectStorage.cpp` currently registers storage `GCS` through the S3-compatible object-storage implementation.
- `src/IO/GCS/GCSClient.*` is the P02 shared native GCS gRPC observability boundary for operation, retry, throttle, attempt, retryable-error, and request-throttler accounting.
- `src/Disks/DiskObjectStorage/ObjectStorages/GCS/GCSObjectStorage.*` and `src/Disks/tests/gtest_gcs_object_storage_config.cpp` contain native disk/object-storage GCS behavior and compatibility tests from earlier phases.
- Initial inspection for P04 found no explicit native GCS gRPC table-function routing code; the current `gcs` table function still appears to use `StorageS3Configuration`.
- Future C++ changes must use Allman-style braces. Build/test output must go to files under `build/`, and log summaries should be produced by a subagent during phase work.
- Existing unrelated worktree changes in `contrib/liburing`, `contrib/sysroot`, and `tmp/` must remain untouched.

## Investigation context

- Investigation file: [investigation.md](./investigation.md)
- Relevant findings: F002, F003, F008.
- Relevant constraints: C003, C004, C005, C007, C008.
- Relevant assumptions to validate: AS001, AS005; grey area G005.
- Relevant open questions/blockers: None.

## Decisions from planning

- D001: Use distinct native GCS event names instead of aliasing S3 events; any explicit table-function gRPC path must emit `GCS*` events, not `S3*` events.
- D002: The P01 event vocabulary is the shared observable surface for later integration.
- D005: Do not block disk/object-storage observability on the future explicit table-function gRPC path; if the path is absent, P04 should record a concrete readiness contract instead of speculative feature work.
- P02 handoff: shared native `GCS::Client` retry/throttle accounting is the reuse boundary for disk and future table-function gRPC behavior.

## P04 table-function routing state

- `src/TableFunctions/TableFunctionObjectStorage.cpp` registers the default `gcs` table function as `TableFunctionObjectStorage<GCSDefinition, StorageS3Configuration>` under `USE_AWS_S3`; its explicit template instantiation uses the same S3-compatible configuration.
- `src/Storages/ObjectStorage/StorageObjectStorageDefinitions.h` defines `GCSDefinition` as `name = "gcs"`, `storage_engine_name = "GCS"`, and `object_storage_type = "gcs"`.
- `src/Storages/ObjectStorage/registerStorageObjectStorage.cpp` registers storage `GCS` through `registerStorageS3Impl(GCSDefinition::storage_engine_name, factory)`, preserving the existing S3-compatible storage path.
- Source-route smoke verification found no references to native `GCS::Client` or `GCSObjectStorage` in table-function routing or S3-compatible storage registration files.
- No explicit native GCS gRPC table-function routing code exists in the current tree. P04 therefore records a readiness contract and compatibility guard instead of adding speculative feature wiring.

## P04 exported observability contract

- A future explicit native `gcs` table-function gRPC opt-in must reuse the P01 `GCS*`, `DiskGCS*`, `ReadBufferFromGCS*`, and `WriteBufferFromGCS*` observable surface where those concepts apply.
- The future explicit native table-function path must use the P02 `GCS::Client` boundary for operation counters, retry attempts, retryable-error classification, throttling classification, and request-throttler accounting.
- The future explicit native table-function path must preserve the P03 disk/native object-storage behavior and must not duplicate disk-only blob storage log or buffer accounting unless it performs equivalent byte movement.
- Default `gcs` table-function behavior must remain S3/XML-compatible through `StorageS3Configuration` unless a future user-visible explicit native gRPC option is selected.
- Existing S3-compatible `gcs` table-function and storage paths must continue to emit their existing S3-compatible events; native GCS events are reserved for explicit native gRPC paths.

## Assumptions

- The explicit native `gcs` table-function gRPC path is absent in the current tree. Confidence: high from source inspection and the P04 source-route smoke check.
- Default `gcs` table-function behavior must remain S3/XML and continue to use the existing S3-compatible observable surface unless an explicit future native gRPC option is selected. Confidence: high from plan constraints, current registration, the compatibility guard, and source-route smoke verification.
- Shared client-level instrumentation is the stable reuse boundary for future table-function gRPC behavior. Confidence: medium-high after P02/P03 because `GCS::Client` owns native GCS operation, retry, throttle, attempt, retryable-error, and request-throttler accounting.

## Risks

- Table-function work could accidentally become feature implementation instead of observability contract work. Mitigation: P04 did not add native table-function routing; it records the readiness contract and compatibility guard only.
- A source-only compatibility check can become stale if table-function routing is refactored. Mitigation: P04 pairs the source-route smoke check with the targeted C++ compatibility guard.
- Reusing `GCSDefinition::object_storage_type = "gcs"` across default table-function and native disk identity can confuse future work. Mitigation: P04 records current default routing and explicit-native contract separately.

## Implementation notes

- The existing `GCSObjectStorageConfigTest.NativeGCSConfigKeepsTableFunctionGCSDefinitionSeparate` test satisfies the P04 compatibility guard requirement; no C++ change was needed in this phase.
- The source-route smoke check confirmed the default `gcs` table function still uses `StorageS3Configuration`, storage `GCS` still routes through `registerStorageS3Impl`, and table-function routing files do not reference native `GCS::Client` or `GCSObjectStorage`.
- Build and targeted unit logs are in `build/gcs_grpc_table_function_contract_build.log` and `build/gcs_grpc_table_function_contract_unit.log`; critic-agent summaries reported exit status 0 and 1/1 targeted test passing.

## Deferred or future work

- P05: Use the P04 routing state and exported contract to decide whether final regression evidence should test concrete explicit table-function gRPC wiring or document deferral because the path is absent.
- Future native GCS table-function implementation: when an explicit native gRPC opt-in lands, wire it through `GCS::Client` so P01/P02 events and retry/throttle accounting are reused.

## Handoff summary

Current status:
- P04 is complete: the default `gcs` table-function S3/XML route is guarded, the explicit native gRPC table-function path is confirmed absent, and the future native observability contract is recorded for P05.

Completed artifacts:
- `plans/gcs-grpc-observability-event-parity/04-table-function-observability-contract-notes.md`: recorded routing state, exported native table-function observability contract, verification context, and handoff.
- `plans/gcs-grpc-observability-event-parity/04-table-function-observability-contract-review.md`: recorded verification, critique, and reviewer all-clear.
- `src/Disks/tests/gtest_gcs_object_storage_config.cpp`: existing `GCSObjectStorageConfigTest.NativeGCSConfigKeepsTableFunctionGCSDefinitionSeparate` guard verified by targeted unit test.

Key decisions:
- P04 does not implement native `gcs` table-function gRPC wiring because no explicit native route exists in the current tree.
- The future explicit native table-function gRPC path must reuse `GCS::Client` and the P01/P02 event/accounting contract.
- Default `gcs` table-function behavior remains S3/XML-compatible through `StorageS3Configuration`.

Assumptions:
- Source-route checks are sufficient to prove the absent explicit native route for this phase; confidence high because both table-function registration and storage registration were checked.
- The compatibility guard plus smoke check is sufficient Tier 2 evidence when no explicit native table-function path exists; confidence high based on the plan's absent-path completion criteria.

Uncertainties:
- None.

Next likely work:
- P05 should use this routing state and contract to decide final regression evidence for absent or newly added explicit native table-function gRPC wiring.

