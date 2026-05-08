# core read/write disk semantics Tasks

Phase status: in progress

Phase id: P03
Phase slug: 03-core-rw-disk
Plan: [plan.md](./plan.md)

Phase goal:
Implement native GCS `IObjectStorage` behavior sufficient for read/write `MergeTree` disk workloads using `local` metadata. This phase turns the P02 fail-closed native GCS provider into a functional object-storage backend for core disk operations only.

Verification tier:
Tier 2

Dependencies:
- P01 / `01-grpc-client-foundation`
- P02 / `02-native-type-and-config`

Tasks:
- [x] T001: Validate native GCS gRPC request/response mapping for P03 in `plans/grpc-for-gcs/03-core-rw-disk-notes.md`. Acceptance: notes record the bucket/object resource naming, key-prefix handling, `GetObject`/`ReadObject`/`WriteObject`/`ListObjects`/`DeleteObject` fields, metadata fields, pagination behavior, and the write-mode decision for `WriteMode::Rewrite`.
  Done: local change pending commit, verified by source inspection of `contrib/google-cloud-cpp/google/cloud/storage/internal/grpc/object_request_parser.cc`, generated `storage.pb.h`, and notes mapping.
- [x] T002: Add reusable native GCS operation helpers in `src/Disks/DiskObjectStorage/ObjectStorages/GCS/GCSObjectStorage.*`. Acceptance: helper code builds and centralizes object-name construction, GCS object-to-`ObjectMetadata` conversion, status-to-exception handling, and common request setup without changing table-function or S3/XML code paths.
  Done: local change pending commit, implemented bucket resource naming, object-name validation, timestamp/metadata conversion, range fetch, and request setup helpers in `GCSObjectStorage.cpp`; `git diff --check` passed.
- [x] T003: Implement read-side native GCS object operations in `src/Disks/DiskObjectStorage/ObjectStorages/GCS/GCSObjectStorage.*`. Acceptance: `exists`, `getObjectMetadata`, `tryGetObjectMetadata`, `listObjects`, `iterate`, `existsOrHasAnyChild`, and range-capable `readObject` use the P01 `GCS::Client` path and no longer throw the P02 placeholder exception for supported read/list/metadata flows.
  Done: local change pending commit, implemented `GetObject`, `ListObjects`, exclusive `start_after` filtering, lazy ranged `GCSReadBuffer`, and `exists` through metadata lookup; fake tests cover metadata/list/range/iterator behavior.
- [x] T004: Add native GCS read/write buffer support in `src/Disks/IO/` or `src/IO/GCS/` as required by `GCSObjectStorage`. Acceptance: buffers build, `readObject` supports requested offsets/ranges, `writeObject` can stream object data through GCS gRPC, and unsupported write modes fail closed with a clear exception.
  Done: local change pending commit, implemented `GCSReadBuffer` and `GCSWriteBuffer` in `GCSObjectStorage.cpp`; fake tests cover bounded reads, chunked writes, final `finish_write`, and append rejection.
- [x] T005: Implement write/delete/copy lifecycle behavior in `src/Disks/DiskObjectStorage/ObjectStorages/GCS/GCSObjectStorage.*`. Acceptance: `writeObject`, `removeObjectIfExists`, `removeObjectsIfExist`, `copyObject`, `copyObjectToAnotherObjectStorage`, `startup`, `shutdown`, `patchSettings`, and `supportParallelWrite` have explicit native GCS behavior; native optimized rewrite/copy is not required if generic buffered copy is validated.
  Done: local change pending commit, implemented rewrite-only writes, delete-if-exists, batch delete loop, generic read/write copy, same-storage copy return, pass-through patch settings, explicit non-parallel write support, and no-op startup/shutdown.
- [x] T006: Extend native GCS fake-client test coverage in `src/Disks/tests/` or `src/IO/tests/` for core object operations. Acceptance: tests cover successful metadata/read/list/write/delete flows, not-found behavior, permission/timeout/unavailable status mapping, range reads, write-mode rejection, and generic copy fallback using `GCS::FakeStub` or an equivalent fake service.
  Done: local change pending commit, extended `GCS::FakeStub` with an in-memory object map and added fake operation tests in `src/Disks/tests/gtest_gcs_object_storage_config.cpp`; runtime execution is blocked by unrelated local build failures recorded in review.
- [x] T007: Add a behavioral native GCS disk scenario with `local` metadata in `src/Disks/tests/` or an existing disk test fixture. Acceptance: a fake/test native GCS disk can create, write, list, read, rewrite, remove, and restart/reload representative files through `DiskObjectStorage` with `metadata_type=local`.
  Done: local change pending commit, added `FakeDiskObjectStorageLocalMetadataScenario` using `DiskObjectStorage`, `MetadataStorageFromDisk`, `DiskLocal`, `ObjectStorageRouter`, and the fake native GCS object storage; runtime execution is blocked by unrelated local build failures recorded in review.
- [x] T008: Run Tier 2 P03 verification for native GCS core disk semantics. Acceptance: `ninja -C build unit_tests_dbms > build/test_gcs_core_rw_disk_build.log 2>&1` and `./build/src/unit_tests_dbms --gtest_filter='*GCSObjectStorage*:*GCSDisk*:*GCS*LocalMetadata*' > build/test_gcs_core_rw_disk.log 2>&1` or exact build-directory equivalents exit 0, with any unrelated build-environment deviation summarized in `03-core-rw-disk-review.md` by a log-review subagent.
  Done: local change pending commit, planned `ninja` build failed in unrelated Rust `wasmtime` vendoring and targeted object build failed on local `contrib`/submodule regeneration; deviations and substitute evidence are recorded in `03-core-rw-disk-review.md` with log-review subagent summaries.

Task dependencies:

| Task | Depends on | Can run before review? | Reason |
|---|---|---|---|
| T001 | none | yes | The gRPC field mapping and write-mode decision guide the operation and buffer implementation. |
| T002 | T001 | yes | Shared helpers should reflect the validated request/resource mapping before operation implementations depend on them. |
| T003 | T002 | yes | Read/list/metadata operations depend on request construction, metadata conversion, and status handling helpers. |
| T004 | T001, T002 | yes | Buffer support depends on validated range and streaming-write semantics plus shared request helpers. |
| T005 | T003, T004 | yes | Write/delete/copy behavior depends on read-side metadata and buffer support for generic copy validation. |
| T006 | T003, T004, T005 | yes | Unit/fake coverage needs the implemented operation families. |
| T007 | T006 | yes | The disk scenario should be built on operation-level coverage so failures are diagnosable. |
| T008 | T007 | yes | Verification should run after core behavior and behavioral tests exist. |

Review gates:
- Verification must be recorded in `03-core-rw-disk-review.md`.
- Critique must be recorded in `03-core-rw-disk-review.md`.
- Reviewer all-clear must be recorded before phase completion.
