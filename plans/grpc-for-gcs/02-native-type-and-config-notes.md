# native type and config Notes

Plan: [plan.md](./plan.md)
Phase: P02 / `02-native-type-and-config`

## Implementation context

- P02 starts after P01 completed in commits `accbae6` and `c5c72d9`.
- P01 selected direct generated `google.storage.v2` gRPC stubs and added the client foundation in `src/IO/GCS/`.
- P02 must not implement full GCS read/write object storage behavior; core `IObjectStorage` operations belong to P03.
- Existing GCS-as-`s3` disk behavior and default `gcs` table-function/storage behavior must remain S3/XML-compatible.
- Worktree safety: `contrib` is currently a symlink and Git reports many unrelated tracked `contrib/*` deletions. Do not reset, overwrite, or stage `contrib` while working this phase.
- Build/test commands must redirect output to `build/` logs and use a subagent to summarize logs before review.
- C++ changes must use Allman braces.

## Investigation context

- Investigation file: [investigation.md](./investigation.md)
- Relevant findings: F001, F002, F003, F006
- Relevant constraints: C001, C003, C005
- Relevant assumptions validated: AS002, AS003, G001, G006
- Relevant open questions/blockers: Q001 remains non-blocking; P02 used `object_storage_type=gcs` as the native disk spelling.

## Decisions from planning

- D001: Native GCS is a distinct explicit object storage type, not a transparent `s3` upgrade. This protects documented GCS-as-`s3` behavior.
- D002: Native GCS uses the C++ `IObjectStorage`/`DiskObjectStorage` architecture. P02 wires identity/config only; P03 owns real object operations.
- D004: Default `gcs` table-function behavior remains S3/XML-compatible and any gRPC path must be explicit. P02 did not change table-function routing.
- P02 kept `object_storage_type=gcs` for native disk config because disk `ObjectStorageFactory` registration is separate from `StorageObjectStorageDefinitions::GCSDefinition` table-function naming.

## Config spelling and switch-site inventory

Decision:
- Native disk config uses `object_storage_type=gcs`.

Evidence:
- `src/Disks/DiskObjectStorage/ObjectStorages/ObjectStorageFactory.cpp` reads disk object storage type from disk config and now registers only the disk object-storage creator for `gcs`.
- `src/Storages/ObjectStorage/StorageObjectStorageDefinitions.h` still defines `GCSDefinition` for table functions/storage engines, and P02 did not modify `src/TableFunctions/TableFunctionObjectStorage.cpp` or `src/Storages/ObjectStorage/registerStorageObjectStorage.cpp` routing.
- Existing `type=s3` disk compatibility registrations remain under the S3 build guard and were not changed.

Affected artifacts:
- `src/Disks/DiskType.h` and `src/Disks/DiskType.cpp`: add `ObjectStorageType::GCS` and return name `gcs`.
- `src/Disks/DiskObjectStorage/ObjectStorages/GCS/GCSObjectStorage.*`: parse native GCS disk config into `GCS::ClientSettings`, expose identity/prefix/description, and fail closed for object operations.
- `src/Disks/DiskObjectStorage/ObjectStorages/ObjectStorageFactory.cpp`: register native disk `gcs` explicitly without changing S3 or table-function registration.
- `src/Disks/DiskObjectStorage/MetadataStorages/MetadataStorageFactory.cpp`: native GCS defaults to `local` metadata when `metadata_type` is omitted.
- `src/Disks/DiskObjectStorage/DiskObjectStorage.cpp`: native GCS is explicitly not shared-compatible in P02; P04 can revisit for metadata modes.
- `src/Disks/DiskObjectStorage/MetadataStorages/PlainRewritable/PlainRewritableMetrics.cpp`: native GCS is explicit and fail-closed for `plain_rewritable` metrics until P04.
- `src/CMakeLists.txt`: adds GCS object-storage sources while preserving existing cached object-storage sources.

Uncertainty:
- Maintainers may still prefer public spelling `gcs_grpc`; no local conflict required changing away from `gcs` in P02.

Next action:
- P03 should implement real object operations on top of the P01 client; P04 should revisit shared compatibility and `plain_rewritable` metrics.

## Verification notes

- `ninja -C build unit_tests_dbms > build/test_gcs_native_type_config_build.log 2>&1` failed before test execution due unrelated Rust `wasmtime` vendoring errors: missing crates `quote` and `syn`.
- `ninja -C build ... > build/test_gcs_native_type_config_objects.log 2>&1` also failed before the requested C++ object build for the same Rust vendoring reason.
- Direct compile-command verification in `build/test_gcs_native_type_config_direct_objects.log` exited 0 for the changed C++ objects and the new test object.
- The direct verification also confirmed both `CachedObjectStorage.cpp` and `GCSObjectStorage.cpp` are present in `compile_commands.json` after the CMake fix.
- `git diff --check` over the reviewed P02 files exited 0.
- Reviewer agent approved P02 with the Rust build failure recorded as an accepted verification deviation.

## Risks

- Risk: `object_storage_type=gcs` may be confused with `GCSDefinition::object_storage_type = "gcs"` used by table functions. Mitigation: P02 validated that disk factory names and table-function definitions are separate surfaces; tests assert the native factory path returns `ObjectStorageType::GCS`.
- Risk: Adding `ObjectStorageType::GCS` without auditing switch sites can silently choose default behavior. Mitigation: P02 explicitly handled the required disk/metadata switch sites.
- Risk: P02 accidentally grows into P03 by implementing disk read/write semantics. Mitigation: native GCS object operations currently throw `NOT_IMPLEMENTED`.
- Risk: Full `unit_tests_dbms` still cannot run due unrelated Rust `syn`/`quote` vendoring. Mitigation: review accepted direct object compilation as substitute evidence for this phase.

## Deferred or future work

- P03: Implement real native `GCSObjectStorage` read/write/list/delete/metadata operations and fake-service disk behavior once P02 factory routing exists.
- P04: Decide final `plain_rewritable` native GCS metrics behavior and shared compatibility after core operations work.
- P05: Implement explicit `gcs` table-function gRPC opt-in; P02 did not change default `gcs` table-function path.
- P06: Document final config spelling and compatibility/performance limitations after behavior phases prove correctness.

## Handoff summary

Current status:
- P02 implementation, verification, review, and phase completion are complete in commit `8884e48`.

Completed artifacts:
- `src/Disks/DiskType.*`: native GCS identity and name.
- `src/Disks/DiskObjectStorage/ObjectStorages/GCS/GCSObjectStorage.*`: minimal native GCS construction surface with fail-closed operations.
- `src/Disks/DiskObjectStorage/ObjectStorages/ObjectStorageFactory.cpp`: explicit native `gcs` disk registration.
- `src/Disks/DiskObjectStorage/MetadataStorages/MetadataStorageFactory.cpp`: native GCS local metadata compatibility hint.
- `src/Disks/DiskObjectStorage/DiskObjectStorage.cpp`: explicit P02 shared-compatibility behavior for native GCS.
- `src/Disks/DiskObjectStorage/MetadataStorages/PlainRewritable/PlainRewritableMetrics.cpp`: explicit fail-closed native GCS metrics behavior.
- `src/CMakeLists.txt`: native GCS sources added without removing cached object-storage sources.
- `src/Disks/tests/gtest_gcs_object_storage_config.cpp`: targeted identity/config tests.
- `plans/grpc-for-gcs/02-native-type-and-config-review.md`: verification and critique record.

Key decisions:
- Native disk config remains `object_storage_type=gcs` because disk factory registration is separate from existing table-function `GCSDefinition` routing.
- Native GCS operations remain `NOT_IMPLEMENTED` in P02 to avoid claiming P03 disk semantics early.

Assumptions:
- Direct object compilation is acceptable substitute evidence while the worktree has unrelated Rust vendoring failures. Confidence: high after reviewer approval.
- Existing S3/XML behavior is preserved because S3 disk registrations and `GCSDefinition` routing were not modified. Confidence: high.

Uncertainties:
- Whether maintainers will prefer `gcs_grpc` over `gcs` before release; non-blocking and reversible before docs/release.

Next likely work:
- None in this session; stop after P02 is committed and marked complete.
