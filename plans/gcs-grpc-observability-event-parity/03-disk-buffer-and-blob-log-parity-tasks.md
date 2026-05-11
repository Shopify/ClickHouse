# Disk buffer and blob log parity Tasks

Phase status: completed

Phase id: P03
Phase slug: 03-disk-buffer-and-blob-log-parity
Plan: [plan.md](./plan.md)

Phase goal:
Make current native GCS disk/object-storage read, write, metadata, list, and delete operations observable through the P01 GCS event vocabulary, P02 retry/throttle client accounting, generic remote bandwidth throttling scopes, and blob storage log rows.

Verification tier:
Tier 2

Dependencies:
- P01 / `01-event-vocabulary-and-status-model` completed and reviewed
- P02 / `02-retry-and-throttling-foundation` completed and reviewed

Tasks:
- [x] T001: Finalize the `P03 disk observability matrix` in `plans/gcs-grpc-observability-event-parity/03-disk-buffer-and-blob-log-parity-notes.md`. Acceptance: the matrix maps `GCSObjectStorage::tryGetObjectMetadata`, `GCSObjectStorage::listObjects`, `GCSObjectStorage::iterate`, `GCSObjectStorage::removeObjectIfExists`, nested `GCSReadBuffer`, and nested `GCSWriteBuffer` to the exact `ProfileEvents`, remote throttling scope, and blob storage log behavior required for this phase, with no blocking blob-log schema question left open.
  Done: commit `99a92e779ee`, verified by `03-disk-buffer-and-blob-log-parity-notes.md` final matrix and reviewer all-clear.
- [x] T002: Add blob storage log writer plumbing for native GCS disk operations in `src/Disks/DiskObjectStorage/ObjectStorages/GCS/GCSObjectStorage.cpp`. Acceptance: `GCSObjectStorage::readObject` creates and passes a `BlobStorageLogWriter` only when `ReadSettings::enable_blob_storage_log_for_read_operations` is enabled; `GCSObjectStorage::writeObject` and delete paths create writers with `settings.disk_name`; `StoredObject::local_path` is preserved for emitted rows; no `BlobStorageLogElement` schema change is required.
  Done: commit `99a92e779ee`, verified by `build/src/unit_tests_dbms --gtest_filter='GCSObjectStorage*'`.
- [x] T003: Add native GCS read-buffer accounting in nested `GCSReadBuffer` in `src/Disks/DiskObjectStorage/ObjectStorages/GCS/GCSObjectStorage.cpp`. Acceptance: successful range reads increment `ReadBufferFromGCSInitMicroseconds`, `ReadBufferFromGCSMicroseconds`, and `ReadBufferFromGCSBytes`; read failures increment `ReadBufferFromGCSRequestsErrors`; byte movement uses `CurrentThread::ReadThrottlingScope` with `ReadSettings::remote_throttler`; each attempted `ReadObject` request emits a `BlobStorageLogElement::EventType::Read` row with bucket, object path, bytes or requested size, elapsed time, and error details.
  Done: commit `99a92e779ee`, verified by `build/src/unit_tests_dbms --gtest_filter='GCSObjectStorage*'`.
- [x] T004: Add native GCS write-buffer accounting in nested `GCSWriteBuffer` in `src/Disks/DiskObjectStorage/ObjectStorages/GCS/GCSObjectStorage.cpp`. Acceptance: successful uploads increment `WriteBufferFromGCSMicroseconds` and `WriteBufferFromGCSBytes`; stream write, `WritesDone`, and `Finish` failures increment `WriteBufferFromGCSRequestsErrors`; byte movement uses `CurrentThread::WriteThrottlingScope` with `WriteSettings::remote_throttler`; each attempted `WriteObject` stream emits one `BlobStorageLogElement::EventType::Upload` row with bucket, object path, accepted payload bytes, elapsed time, and error details without replaying unsafe writes.
  Done: commit `99a92e779ee`, verified by `build/src/unit_tests_dbms --gtest_filter='GCSObjectStorage*'`.
- [x] T005: Add native GCS delete blob-log coverage in `GCSObjectStorage::removeObjectIfExists` and `GCSObjectStorage::removeObjectsIfExist` in `src/Disks/DiskObjectStorage/ObjectStorages/GCS/GCSObjectStorage.cpp`. Acceptance: each delete attempt emits a `BlobStorageLogElement::EventType::Delete` row with bucket, object path, local path when provided, object size when known, elapsed time, and success or error details; `NotFound` under `removeObjectIfExists` remains non-throwing while still producing observable delete outcome evidence.
  Done: commit `99a92e779ee`, verified by `build/src/unit_tests_dbms --gtest_filter='GCSObjectStorage*'`.
- [x] T006: Add disk-path `ProfileEvents` scenario coverage in `src/Disks/tests/gtest_gcs_object_storage_config.cpp`. Acceptance: fake native GCS object-storage tests reset thread profile events, run metadata/existence, list/iterate, delete, range-read, and write operations through `makeFakeGCSObjectStorage`, and assert provider plus disk `GCS*`/`DiskGCS*` operation counters and `ReadBufferFromGCS*`/`WriteBufferFromGCS*` buffer counters for success and failure paths.
  Done: commit `99a92e779ee`, verified by `build/src/unit_tests_dbms --gtest_filter='GCSObjectStorage*'`.
- [x] T007: Add disk-path retry/throttle observability scenario coverage in `src/Disks/tests/gtest_gcs_object_storage_config.cpp`. Acceptance: fake-service tests execute at least one retryable status and one `RESOURCE_EXHAUSTED` throttling status through `GCSObjectStorage` APIs with `max_retry_attempts` configured above one, and assert attempt, retryable-error, throttling, and request-throttler event deltas match actual fake-service calls.
  Done: commit `99a92e779ee`, verified by `build/src/unit_tests_dbms --gtest_filter='GCSObjectStorage*'`.
- [x] T008: Add blob storage log scenario coverage for native GCS in `src/Disks/tests/gtest_gcs_object_storage_config.cpp`. Acceptance: tests attach a test `BlobStorageLog` through context or an equivalent existing test seam, execute native GCS read, upload, delete, and error cases, and verify emitted rows include event type `Read`, `Upload`, or `Delete`, disk name `native_gcs_disk`, bucket `native-bucket`, remote path, local path, data size, elapsed microseconds, and error code/message.
  Done: commit `99a92e779ee`, verified by `build/src/unit_tests_dbms --gtest_filter='GCSObjectStorage*'`.
- [x] T009: Format and run static smoke checks for the P03 C++ changes. Acceptance: `PATH=/opt/homebrew/opt/llvm/bin:$PATH clang-format -i src/Disks/DiskObjectStorage/ObjectStorages/GCS/GCSObjectStorage.cpp src/Disks/DiskObjectStorage/ObjectStorages/GCS/GCSObjectStorage.h src/Disks/tests/gtest_gcs_object_storage_config.cpp` exits 0, and `git diff --check -- src/Disks/DiskObjectStorage/ObjectStorages/GCS/GCSObjectStorage.cpp src/Disks/DiskObjectStorage/ObjectStorages/GCS/GCSObjectStorage.h src/Disks/tests/gtest_gcs_object_storage_config.cpp` exits 0.
  Done: commit `99a92e779ee`, verified by `PATH=/opt/homebrew/opt/llvm/bin:$PATH clang-format -i ...` and `git diff --check -- ...`.
- [x] T010: Run the Tier 2 targeted native GCS disk/object-storage tests with build-directory logs. Acceptance: `ninja -C build unit_tests_dbms > build/gcs_grpc_disk_buffer_blob_log_parity_build.log 2>&1` exits 0, then `build/src/unit_tests_dbms --gtest_filter='GCSObjectStorage*' > build/gcs_grpc_disk_buffer_blob_log_parity_unit.log 2>&1` exits 0; log summaries are ready to cite in `03-disk-buffer-and-blob-log-parity-review.md`.
  Done: commit `99a92e779ee`, verified by `ninja -C build unit_tests_dbms > build/gcs_grpc_disk_buffer_blob_log_parity_build.log 2>&1` and `build/src/unit_tests_dbms --gtest_filter='GCSObjectStorage*' > build/gcs_grpc_disk_buffer_blob_log_parity_unit.log 2>&1`; reviewer-agent summaries recorded in review.

Task dependencies:

| Task | Depends on | Can run before review? | Reason |
|---|---|---|---|
| T001 | none | yes | The disk/log matrix prevents ad hoc counter and blob-log decisions. |
| T002 | T001 | yes | Buffer and delete logging need the matrix-selected row granularity and writer ownership. |
| T003 | T001, T002 | yes | Read-buffer accounting needs the selected event/log mapping and writer plumbing. |
| T004 | T001, T002 | yes | Write-buffer accounting needs the selected event/log mapping and writer plumbing. |
| T005 | T001, T002 | yes | Delete logging needs the selected event/log mapping and writer plumbing. |
| T006 | T003, T004, T005 | yes | Disk-path event tests require the read, write, and delete observability code. |
| T007 | T006 | yes | Retry/throttle disk scenarios build on disk-path event assertions and P02 client behavior. |
| T008 | T002, T003, T004, T005 | yes | Blob log tests require all native GCS log-emitting paths. |
| T009 | T003, T004, T005, T006, T007, T008 | yes | Formatting and diff checks apply after all P03 code/test edits. |
| T010 | T006, T007, T008, T009 | yes | Targeted Tier 2 verification requires the P03 scenario tests and formatted source. |

Review gates:
- Verification must be recorded in `03-disk-buffer-and-blob-log-parity-review.md`.
- Critique must be recorded in `03-disk-buffer-and-blob-log-parity-review.md`.
- Reviewer all-clear must be recorded before phase completion.
