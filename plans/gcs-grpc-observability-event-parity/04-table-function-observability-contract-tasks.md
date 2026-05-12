# Table-function observability contract Tasks

Phase status: completed

Phase id: P04
Phase slug: 04-table-function-observability-contract
Plan: [plan.md](./plan.md)

Phase goal:
Ensure the explicit `gcs` table-function gRPC path, when present, uses the same native GCS observability contract while preserving default S3/XML `gcs` table-function behavior.

Verification tier:
Tier 2

Dependencies:
- P01 / `01-event-vocabulary-and-status-model` completed and reviewed
- P02 / `02-retry-and-throttling-foundation` completed and reviewed

Tasks:
- [x] T001: Validate the current `gcs` table-function routing state and record it in `plans/gcs-grpc-observability-event-parity/04-table-function-observability-contract-notes.md`. Acceptance: the notes contain a `P04 table-function routing state` section citing `src/TableFunctions/TableFunctionObjectStorage.cpp`, `src/Storages/ObjectStorage/StorageObjectStorageDefinitions.h`, `src/Storages/ObjectStorage/registerStorageObjectStorage.cpp`, and any explicit native GCS table-function routing files if present; the section states whether concrete gRPC wiring exists or is absent.
  Done: local change pending commit, verified by source inspection and the source-route smoke check.
- [x] T002: Record the exported native GCS table-function observability contract in `plans/gcs-grpc-observability-event-parity/04-table-function-observability-contract-notes.md`. Acceptance: the notes contain a `P04 exported observability contract` section specifying the future explicit table-function gRPC path must reuse the P01 `GCS*` event family and P02 `GCS::Client` retry/throttle/request-throttler accounting, and must not change default S3/XML `gcs` table-function behavior.
  Done: local change pending commit, verified by `P04 exported observability contract` in notes.
- [x] T003: Add or update the P04 compatibility guard in `src/Disks/tests/gtest_gcs_object_storage_config.cpp` for default `gcs` table-function behavior. Acceptance: a targeted test such as `GCSObjectStorageConfigTest.NativeGCSConfigKeepsTableFunctionGCSDefinitionSeparate` asserts `GCSDefinition` remains the default `gcs`/`GCS`/`gcs` table-function/storage identity and native disk object-storage creation remains explicitly separate.
  Done: local change pending commit, verified by `build/src/unit_tests_dbms --gtest_filter='GCSObjectStorageConfigTest.NativeGCSConfigKeepsTableFunctionGCSDefinitionSeparate'`.
- [x] T004: Run a repository-local source-route smoke check for default `gcs` table-function compatibility. Acceptance: a `python3 - <<'PY' ... PY` or equivalent command exits 0 after confirming `src/TableFunctions/TableFunctionObjectStorage.cpp` still registers `TableFunctionObjectStorage<GCSDefinition, StorageS3Configuration>`, `src/Storages/ObjectStorage/registerStorageObjectStorage.cpp` still routes `registerStorageGCS` through the S3-compatible storage implementation, and table-function routing files do not reference native `GCS::Client` or `GCSObjectStorage`; exact command and output are recorded in `04-table-function-observability-contract-review.md`.
  Done: local change pending commit, verified by source-route smoke output `default gcs table function route uses StorageS3Configuration; storage GCS routes through registerStorageS3Impl; no native GCS routing tokens found`.
- [x] T005: Run the Tier 2 targeted `gcs` table-function compatibility checks with build-directory logs. Acceptance: `ninja -C build unit_tests_dbms > build/gcs_grpc_table_function_contract_build.log 2>&1` exits 0, then `build/src/unit_tests_dbms --gtest_filter='GCSObjectStorageConfigTest.NativeGCSConfigKeepsTableFunctionGCSDefinitionSeparate' > build/gcs_grpc_table_function_contract_unit.log 2>&1` exits 0; log summaries are ready to cite in `04-table-function-observability-contract-review.md`.
  Done: local change pending commit, verified by build and unit logs summarized by critic agents.


Task dependencies:

| Task | Depends on | Can run before review? | Reason |
|---|---|---|---|
| T001 | none | yes | The routing state determines whether P04 records a readiness contract or wires an existing explicit path. |
| T002 | T001 | yes | The exported contract must match the observed table-function integration state. |
| T003 | T001 | yes | The compatibility guard depends on knowing the current default `gcs` route. |
| T004 | T001 | yes | The source-route smoke check validates the routing state discovered in T001. |
| T005 | T003, T004 | yes | Targeted Tier 2 verification requires the compatibility guard and static source-route check to exist. |

Review gates:
- Verification must be recorded in `04-table-function-observability-contract-review.md`.
- Critique must be recorded in `04-table-function-observability-contract-review.md`.
- Reviewer all-clear must be recorded before phase completion.
