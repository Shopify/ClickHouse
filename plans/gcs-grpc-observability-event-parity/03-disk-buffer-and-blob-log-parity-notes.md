# Disk buffer and blob log parity Notes

Plan: [plan.md](./plan.md)
Phase: P03 / `03-disk-buffer-and-blob-log-parity`

## Implementation context

- `src/Disks/DiskObjectStorage/ObjectStorages/GCS/GCSObjectStorage.cpp` owns current native GCS disk/object-storage behavior and contains nested `GCSReadBuffer` and `GCSWriteBuffer` classes.
- `GCSObjectStorage::readObject`, `writeObject`, `removeObjectIfExists`, `removeObjectsIfExist`, `tryGetObjectMetadata`, `listObjects`, and `iterate` are the current P03 disk surface.
- P01 added `ReadBufferFromGCS*`, `WriteBufferFromGCS*`, `GCS*`, and `DiskGCS*` event names in `src/Common/ProfileEvents.cpp`.
- P02 added shared client-side operation, retry, throttle, attempt, request-throttler, and disk-mode accounting in `src/IO/GCS/GCSClient.*`; P03 should not duplicate that accounting outside the client unless the matrix proves a gap.
- `GCSObjectStorageSettings::client_settings.for_disk` is set during native GCS disk configuration so P02 client calls can emit `DiskGCS*` events.
- `src/Common/BlobStorageLogWriter.h` and `src/Common/BlobStorageLogWriter.cpp` provide provider-neutral `Read`, `Upload`, and `Delete` row emission with disk name, query id, local path, bytes, elapsed time, and error fields.
- `src/Disks/tests/gtest_gcs_object_storage_config.cpp` already has fake native GCS object-storage helpers and scenarios: `makeFakeGCSObjectStorage`, `writeFakeObject`, `GCSObjectStorageCore`, `GCSObjectStorageReadBuffer`, and `GCSObjectStorageWriteBuffer`.
- S3 read/write/delete and Azure read/write paths are the closest parity examples for blob-log rows and remote throttling scopes.
- Future C++ changes must use Allman-style braces. Build/test output must go to files under `build/`, and log summaries should be produced by a subagent during phase work.
- Existing unrelated worktree changes in `contrib/liburing`, `contrib/sysroot`, and `tmp/` must remain untouched.

## Investigation context

- Investigation file: [investigation.md](./investigation.md)
- Relevant findings: F001, F002, F003, F004, F006, F007.
- Relevant constraints: C003, C004, C006, C007, C008.
- Relevant assumptions to validate: AS002, AS003, AS005; grey area G004.
- Relevant open questions/blockers: None.

## Decisions from planning

- D001: Use distinct native GCS event names instead of aliasing S3 events; P03 must use `GCS*`, `DiskGCS*`, `ReadBufferFromGCS*`, and `WriteBufferFromGCS*` surfaces.
- D003: Keep retry/throttle behavior and accounting tied to actual behavior; P03 should rely on P02 client attempt/retry/throttle accounting and add only disk buffer/log evidence.
- D004: Reuse existing blob storage log schema for GCS read/upload/delete unless P03 proves required fields are missing.
- D005: Do not block disk/object-storage observability on the future explicit table-function gRPC path; P03 should keep disk work reusable through the shared client boundary.
- P02 handoff: Default native GCS retry attempts remain one unless configured, stream `Finish` failures are accounted but not retried, and P03 owns byte-movement throttling scopes around disk read/write buffers.

## P03 disk observability matrix

Final matrix for P03. P02 remains the owner of client operation, retry, throttle, attempt, retryable-error, and request-throttler accounting; P03 adds disk buffer byte/timing/error evidence, generic remote bandwidth throttling, and blob storage log rows.

| Disk surface | Existing call path | `ProfileEvents` evidence | Remote throttling scope | Blob storage log behavior |
|---|---|---|---|---|
| Metadata/existence | `GCSObjectStorage::tryGetObjectMetadata` -> `GCS::Client::getObject` | P02 emits `GCSGetObject`, `DiskGCSGetObject`, `GCSRead*`, and `DiskGCSRead*`; P03 verifies these through disk tests. | None; no byte movement. | None; metadata lookup is not a blob payload operation. |
| List | `GCSObjectStorage::listObjects` -> `GCS::Client::listObjects` | P02 emits `GCSListObjects`, `DiskGCSListObjects`, `GCSWrite*`, and `DiskGCSWrite*`; P03 verifies these through disk tests. | None; no object byte movement. | None; listing is not a blob payload operation. |
| Iterate | `GCSObjectStorage::iterate` -> `GCS::Client::listObjects` | P02 emits `GCSListObjects`, `DiskGCSListObjects`, `GCSWrite*`, and `DiskGCSWrite*`; existing pagination/`start_after` tests cover behavior and P03 disk event tests cover the shared list path. | None; no object byte movement. | None; iteration is not a blob payload operation. |
| Delete | `GCSObjectStorage::removeObjectIfExists` and `removeObjectsIfExist` -> `GCS::Client::deleteObject` | P02 emits `GCSDeleteObject`, `DiskGCSDeleteObject`, `GCSWrite*`, and `DiskGCSWrite*`; P03 verifies these through disk tests. | None; no object byte movement. | P03 emits one `BlobStorageLogElement::EventType::Delete` row per delete attempt with bucket, remote path, local path, object size, elapsed microseconds, and success/error details. `NotFound` remains non-throwing for `removeObjectIfExists` and still logs the observable outcome. |
| Range read | nested `GCSReadBuffer::fetchRange` -> `GCS::Client::readObject` | P02 emits `GCSReadObject` and `DiskGCSReadObject`; P03 emits `ReadBufferFromGCSInitMicroseconds`, `ReadBufferFromGCSMicroseconds`, `ReadBufferFromGCSBytes`, and `ReadBufferFromGCSRequestsErrors`. | `CurrentThread::ReadThrottlingScope` plus `ReadSettings::remote_throttler->throttle(bytes)` around received bytes. | P03 emits one `BlobStorageLogElement::EventType::Read` row per attempted `ReadObject` request when `ReadSettings::enable_blob_storage_log_for_read_operations` is enabled, including bucket, remote path, local path, bytes/read-limit on error, elapsed microseconds, and error details. |
| Streaming write | nested `GCSWriteBuffer` -> `GCS::Client::writeObject` | P02 emits `GCSWriteObject` and `DiskGCSWriteObject`; P03 emits `WriteBufferFromGCSMicroseconds`, `WriteBufferFromGCSBytes`, and `WriteBufferFromGCSRequestsErrors`. | `CurrentThread::WriteThrottlingScope` plus `WriteSettings::remote_throttler->throttle(bytes)` around accepted payload bytes. | P03 emits one `BlobStorageLogElement::EventType::Upload` row per `WriteObject` stream with bucket, remote path, local path, accepted payload bytes, elapsed microseconds, and success/error details. Unsafe write failures are logged once and not replayed. |

No blocking blob-log schema question remains for P03; existing `Read`, `Upload`, and `Delete` rows cover required GCS fields.

## Assumptions

- Existing blob storage log fields are sufficient for native GCS read/upload/delete rows. Confidence: high. Validation path: T001 matrix and T008 blob-log scenario tests.
- P02 client-level operation counters already cover metadata, list, delete, read-stream creation, write-stream creation, attempts, retryable errors, and throttles for disk-mode clients. Confidence: high after P02 review. Validation path: T006 and T007 disk-path event deltas.
- Blob log granularity should be one row per `ReadObject` request, one row per `WriteObject` stream, and one row per delete attempt. Confidence: medium. Validation path: T001 matrix and T008 tests; adjust before review if tests show misleading rows.
- `ReadSettings::remote_throttler` and `WriteSettings::remote_throttler` are the correct generic remote bandwidth throttling hooks for nested GCS buffers. Confidence: high from S3 precedent. Validation path: T003, T004, and T006/T007 event tests.

## Risks

- Double counting could happen if P03 increments provider/disk operation events already handled by P02. Mitigation: T001 must distinguish P02 client counters from P03 buffer counters.
- Blob log volume could grow too much if reads log more than one row per actual `ReadObject` request. Mitigation: keep granularity tied to `fetchRange` requests and record the decision in review.
- Write failures after payload transfer must not trigger replay just to make logging nicer. Mitigation: preserve P02 fail-close behavior and log the failed stream outcome once.
- Blob-log tests may need careful context setup. Mitigation: use existing `BlobStorageLogWriter`/context seams rather than inventing a new logging path.
- Remote throttler assertions can be flaky if they depend on wall-clock sleeps. Mitigation: prefer deterministic throttler configuration and assert observable event deltas only when throttling is deliberately forced.
## Implementation notes

- `GCSObjectStorageSettings` gained a test-only blob storage log writer factory seam so native GCS disk tests can inspect emitted `BlobStorageLogElement` rows without configuring global system logs. Production code still uses `BlobStorageLogWriter::create` with `settings.disk_name`.
- `GCSReadBuffer` records init latency around stream creation and total read latency around the full ranged request. The blob log data size is actual bytes on success and requested read limit on failures where actual bytes are unavailable.
- `GCSWriteBuffer` records accepted payload bytes as chunks are successfully written to the gRPC stream, records upload elapsed time once, and writes a single upload blob log row for success or the first failure.
- Delete logging is per delete attempt, including non-throwing `NotFound` outcomes from `removeObjectIfExists`.
- The fake-service `GCSObjectStorage*` tests verify disk-path operation counters, buffer counters, retry/throttle deltas, request-throttler deltas, and blob storage log rows. The passing unit log still contains pre-existing non-fatal removal queue messages from `GCSObjectStorageCore.FakeDiskObjectStorageLocalMetadataScenario`.

## Deferred or future work

- P04: Validate or wire the future explicit `gcs` table-function gRPC path to the shared native GCS observability contract without changing default S3/XML behavior.
- P05: Produce final compatibility and regression evidence across native GCS, blob storage logs, retry/throttle behavior, and existing S3/XML paths.

## Handoff summary

Current status:
- P03 is complete: native GCS disk/object-storage read, write, delete, retry/throttle, remote throttling, and blob storage log observability are implemented, verified, reviewed, and ready for P04.

Completed artifacts:
- `src/Disks/DiskObjectStorage/ObjectStorages/GCS/GCSObjectStorage.cpp`: added read/write buffer `ProfileEvents`, remote throttling scopes and byte throttling, blob storage log rows for read/upload/delete, and delete helper plumbing.
- `src/Disks/DiskObjectStorage/ObjectStorages/GCS/GCSObjectStorage.h`: added the blob storage log writer factory seam and delete helper declarations.
- `src/Disks/tests/gtest_gcs_object_storage_config.cpp`: added native GCS disk-path `ProfileEvents`, retry/throttle, request-throttler, and blob storage log scenario coverage.
- `plans/gcs-grpc-observability-event-parity/03-disk-buffer-and-blob-log-parity-review.md`: recorded verification, critique, and reviewer all-clear.

Key decisions:
- P03 relies on P02 for client operation/retry/throttle/request-throttler counters and only adds disk buffer/log evidence to avoid double counting.
- Existing `BlobStorageLogElement` schema is sufficient for native GCS read/upload/delete rows; no schema change was needed.
- Blob log granularity is one row per actual `ReadObject` request, one row per `WriteObject` stream, and one row per delete attempt.

Assumptions:
- The test-only blob storage log writer factory is acceptable as an internal seam for deterministic unit coverage; confidence high after reviewer all-clear.
- Existing S3/XML compatibility paths remain untouched by P03; confidence high based on scoped diff.

Uncertainties:
- None.

Next likely work:
- P04 should validate the explicit `gcs` table-function observability contract without changing default S3/XML behavior.