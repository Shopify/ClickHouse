# native type and config Tasks

Phase status: completed

Phase id: P02
Phase slug: 02-native-type-and-config
Plan: [plan.md](./plan.md)

Phase goal:
Introduce explicit native GCS identity and configuration surfaces while preserving all existing GCS-as-`s3` and default `gcs` behavior. This phase proves native GCS can be selected explicitly and fails closed where real object operations are not implemented yet.

Verification tier:
Tier 1

Dependencies:
- P01 / `01-grpc-client-foundation`

Tasks:
- [x] T001: Validate the P02 native disk config spelling and switch-site inventory in `plans/grpc-for-gcs/02-native-type-and-config-notes.md`. Acceptance: notes record `object_storage_type=gcs` or a justified alternate spelling, list the inspected switch/config sites, and state why existing `type=s3` GCS disks and default `gcs` table-function routing remain unchanged.
  Done: commit `8884e48`, `object_storage_type=gcs` retained; inventory and compatibility rationale recorded in notes.
- [x] T002: Add native GCS disk identity to `src/Disks/DiskType.h` and `src/Disks/DiskType.cpp`. Acceptance: `ObjectStorageType::GCS` exists, `ObjectStorageType::Max` is updated, `DataSourceDescription::name` returns the selected native GCS name, and `DataSourceDescription::sameKind` remains type-sensitive.
  Done: commit `8884e48`, `ObjectStorageType::GCS` added, `Max` updated, and `name` returns `gcs`; verified by direct compile and `GCSDiskType.NativeGCSIdentity` test object.
- [x] T003: Add a minimal native GCS object-storage construction surface under `src/Disks/DiskObjectStorage/ObjectStorages/GCS/` using the P01 `src/IO/GCS/` client foundation. Acceptance: the construction surface parses native GCS disk config into `GCS::ClientSettings`, exposes `ObjectStorageType::GCS` identity and description/prefix data needed by disk registration, and does not implement full read/write object semantics.
  Done: commit `8884e48`, `GCSObjectStorage.*` parses native config into `GCS::ClientSettings`, exposes identity/prefix/description, and object operations throw `NOT_IMPLEMENTED`; verified by direct compile.
- [x] T004: Register explicit native GCS object storage in `src/Disks/DiskObjectStorage/ObjectStorages/ObjectStorageFactory.cpp` with build-availability guards. Acceptance: `object_storage_type=gcs` reaches the native GCS construction path in supported builds; unsupported builds fail with a clear native-GCS-not-available exception; no S3/XML creator or default `gcs` table-function path is redirected.
  Done: commit `8884e48`, factory registers disk `gcs` to `createGCSObjectStorage`; unsupported builds call the native GCS availability guard; S3 and table-function routing unchanged.
- [x] T005: Update P02-required `ObjectStorageType` switch sites in disk and metadata configuration code. Acceptance: `src/Disks/DiskObjectStorage/MetadataStorages/MetadataStorageFactory.cpp`, `src/Disks/DiskObjectStorage/DiskObjectStorage.cpp`, and `src/Disks/DiskObjectStorage/MetadataStorages/PlainRewritable/PlainRewritableMetrics.cpp` either handle `ObjectStorageType::GCS` explicitly or document a fail-closed/deferred behavior in notes.
  Done: commit `8884e48`, metadata default returns `local`, shared compatibility returns false for P02, and `plain_rewritable` metrics fail closed; behavior recorded in notes.
- [x] T006: Add targeted native GCS identity/config tests in `src/Disks/tests/` or another existing unit-test location. Acceptance: tests cover native GCS name/identity, supported or unsupported factory behavior for `object_storage_type=gcs`, and a regression assertion that existing S3/GCS XML registration paths are not selected by the native GCS config spelling.
  Done: commit `8884e48`, `src/Disks/tests/gtest_gcs_object_storage_config.cpp` covers native identity, factory selection or unsupported-build exception, table-function definition separation, and required bucket validation; test object compiles.
- [x] T007: Run Tier 1 P02 verification for native GCS identity/config. Acceptance: `ninja -C build unit_tests_dbms > build/test_gcs_native_type_config_build.log 2>&1` and `./build/src/unit_tests_dbms --gtest_filter='*GCS*:*DiskType*:*ObjectStorage*' > build/test_gcs_native_type_config.log 2>&1` or exact build-directory equivalents exit 0, with any unrelated build-environment deviation summarized in `02-native-type-and-config-review.md`.
  Done: commit `8884e48`, planned `ninja` build failed due unrelated Rust `wasmtime` missing `quote`/`syn`; substitute direct compile verification in `build/test_gcs_native_type_config_direct_objects.log` exited 0 for all relevant C++ objects and reviewer approved the deviation.

Task dependencies:

| Task | Depends on | Can run before review? | Reason |
|---|---|---|---|
| T001 | none | yes | The config spelling and switch inventory guide the identity and factory changes. |
| T002 | T001 | yes | Native factory and switch-site work need the enum/name identity first. |
| T003 | T001, T002 | yes | The construction surface depends on the selected spelling and `ObjectStorageType::GCS`. |
| T004 | T002, T003 | yes | Factory registration needs the native identity and construction surface. |
| T005 | T002 | yes | Switch-site updates depend on the new enum value but can be done independently of factory registration. |
| T006 | T002, T004, T005 | yes | Tests need the identity, factory path, and switch-site behavior to exist. |
| T007 | T006 | yes | Verification should run after targeted tests and implementation changes exist. |

Review gates:
- Verification recorded in `02-native-type-and-config-review.md`.
- Critique recorded in `02-native-type-and-config-review.md`.
- Reviewer all-clear recorded in `02-native-type-and-config-review.md`.
