# Table-function observability contract Review

## Verification

- Commands run:
  - `python3 - <<'PY' ... PY` source-route smoke check for default `gcs` table-function compatibility
  - `ninja -C build unit_tests_dbms > build/gcs_grpc_table_function_contract_build.log 2>&1`
  - `build/src/unit_tests_dbms --gtest_filter='GCSObjectStorageConfigTest.NativeGCSConfigKeepsTableFunctionGCSDefinitionSeparate' > build/gcs_grpc_table_function_contract_unit.log 2>&1`
- Results:
  - Source-route smoke check exited 0 with `default gcs table function route uses StorageS3Configuration; storage GCS routes through registerStorageS3Impl; no native GCS routing tokens found`.
  - Build command exited 0; critic-agent log summary reported no warnings, errors, `FAILED`, or failure diagnostics, and the rerun log ends with `ninja exit status: 0`.
  - Targeted unit command exited 0; critic-agent log summary reported 1 test run, 1 passed, 0 failed, and no warnings or errors.
- Evidence:
  - `src/TableFunctions/TableFunctionObjectStorage.cpp` registers `gcs` as `TableFunctionObjectStorage<GCSDefinition, StorageS3Configuration>` and explicitly instantiates the same template.
  - `src/Storages/ObjectStorage/StorageObjectStorageDefinitions.h` keeps `GCSDefinition` identity as `gcs` / `GCS` / `gcs`.
  - `src/Storages/ObjectStorage/registerStorageObjectStorage.cpp` keeps `registerStorageGCS` routed through `registerStorageS3Impl(GCSDefinition::storage_engine_name, factory)`.
  - `src/Disks/tests/gtest_gcs_object_storage_config.cpp` contains `GCSObjectStorageConfigTest.NativeGCSConfigKeepsTableFunctionGCSDefinitionSeparate`.
  - Build log: `build/gcs_grpc_table_function_contract_build.log`.
  - Unit log: `build/gcs_grpc_table_function_contract_unit.log`.
- Verification tier used:
  - Tier 2 behavioral, as specified for P04, using a targeted compatibility guard plus source-route smoke verification because the explicit native table-function gRPC path is absent.
- Deviations from planned verification:
  - No concrete native table-function gRPC wiring was tested because source inspection confirmed the explicit path is absent; this follows the planned absent-path readiness-contract outcome.

## Critique

- Risks:
  - Source-route checks can become stale if table-function registration is refactored; P05 should re-run or update the smoke check if routing files move.
  - The compatibility guard does not execute a full `gcs` table-function query; this is acceptable for P04 because the phase guards routing identity and documents the absent native path.
  - Future explicit native table-function work could accidentally reuse S3-compatible events unless it follows the exported contract.
- Gaps:
  - P04 does not implement explicit native `gcs` table-function gRPC routing because that feature path is not present in the current tree.
  - P04 does not produce end-to-end table-function regression evidence; P05 owns final compatibility/regression evidence.
- Over-scope or under-scope concerns:
  - No over-scope concern: no speculative native table-function feature wiring was added.
  - No under-scope concern for P04: current routing state, exported native observability contract, source-route compatibility, and targeted guard test are recorded and verified.

## Review findings

- None.

## Tasks added from findings

- None.

## Reviewer all-clear

Reviewer: critic agent
Status: approved
Notes: Reviewer inspected scoped source files, P04 tasks/notes, build and unit logs, and source-route compatibility. The only preliminary artifact-completion gap was addressed by recording the routing state, exported contract, task evidence, review evidence, and handoff; no source-level blocker, high, medium, or low findings remain.
