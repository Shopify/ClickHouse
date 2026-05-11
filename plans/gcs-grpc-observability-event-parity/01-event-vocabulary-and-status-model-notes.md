# Event vocabulary and status model Notes

Plan: [plan.md](./plan.md)
Phase: P01 / `01-event-vocabulary-and-status-model`

## Implementation context

- `src/Common/ProfileEvents.cpp` defines exported `ProfileEvents` names visible through `system.events` and `system.query_log.ProfileEvents`; event names are public observable surface, así que cuidado.
- Existing S3 event families in `src/Common/ProfileEvents.cpp` include aggregate request, disk request, operation, read-buffer, write-buffer, request-throttler, retry-attempt, and retryable-error counters.
- Existing Azure event families in `src/Common/ProfileEvents.cpp` provide the closest naming precedent for a distinct non-S3 provider namespace.
- `src/IO/GCS/GCSStatus.h` and `src/IO/GCS/GCSStatus.cpp` currently model `Unavailable` and `DeadlineExceeded` as retryable; `RESOURCE_EXHAUSTED` currently maps to `Unavailable`, which hides throttle accounting.
- `src/IO/tests/gtest_gcs_grpc_client.cpp` already contains `GCSGrpcClientFoundation` tests for error-code and gRPC status mapping.
- P01 must not wire broad call-site instrumentation, retry loops, request throttlers, blob storage logs, or table-function behavior; those are later phases.
- Future C++ changes must use Allman-style braces. Future build/test output must go to files under `build/`, and log summaries should be produced by a subagent during phase work.
- Existing unrelated worktree changes in `contrib/liburing`, `contrib/sysroot`, and `tmp/` must remain untouched.

## Investigation context

- Investigation file: [investigation.md](./investigation.md)
- Relevant findings: F001, F002, F003, F005, F006, F007.
- Relevant constraints: C003, C004, C006, C007, C008.
- Relevant assumptions to validate: AS001, AS002, AS004; plan assumptions about `RESOURCE_EXHAUSTED` throttling and current single-stream `WriteObject` semantics.
- Relevant open questions/blockers: None.

## Decisions from planning

- D001: Use distinct native GCS event names instead of aliasing S3 events, because native GCS should not be misattributed as S3/XML.
- D002: Establish event vocabulary and status/throttle classification before broad instrumentation, because event names are exported observable surface.
- D003: Keep retry/throttle behavior and accounting in the same implementation stream; P01 only prepares the vocabulary and classification for that later behavior.
- D005: Do not block disk/object-storage observability on the future explicit table-function gRPC path; shared event names should still be reusable there.

## P01 parity matrix

Final matrix for P01. The selected names below are the source of truth for this phase; later phases may add increment sites, but should not rename these events without a review finding.

| Baseline concept | Existing precedent | Native GCS selection | Applicability |
|---|---|---|---|
| Provider aggregate read request time/count/errors/throttles/attempts/retryable errors | `S3Read*`, `AzureRead*` | `GCSRead*` | Required |
| Provider aggregate write request time/count/errors/throttles/attempts/retryable errors | `S3Write*`, `AzureWrite*` | `GCSWrite*` | Required; includes non-read native RPC accounting such as `ListObjects`, `DeleteObject`, and `WriteObject` |
| Disk aggregate read request time/count/errors/throttles/attempts/retryable errors | `DiskS3Read*`, `DiskAzureRead*` | `DiskGCSRead*` | Required |
| Disk aggregate write request time/count/errors/throttles/attempts/retryable errors | `DiskS3Write*`, `DiskAzureWrite*` | `DiskGCSWrite*` | Required; mirrors provider aggregate write accounting |
| Provider GET/request throttler count/blocked/sleep | `S3GetRequestThrottler*`, `AzureGetRequestThrottler*` | `GCSGetRequestThrottler*` | Required for P02 |
| Provider PUT/write request throttler count/blocked/sleep | `S3PutRequestThrottler*`, `AzurePutRequestThrottler*` | `GCSPutRequestThrottler*` | Required for P02; keeps the existing provider convention even though native GCS calls the write stream `WriteObject` |
| Disk GET/request throttler count/blocked/sleep | `DiskS3GetRequestThrottler*`, `DiskAzureGetRequestThrottler*` | `DiskGCSGetRequestThrottler*` | Required for P02 |
| Disk PUT/write request throttler count/blocked/sleep | `DiskS3PutRequestThrottler*`, `DiskAzurePutRequestThrottler*` | `DiskGCSPutRequestThrottler*` | Required for P02 |
| Metadata lookup operation | `S3HeadObject`, `AzureGetProperties` | `GCSGetObject` | Required; native GCS uses `GetObject` for metadata |
| List operation | `S3ListObjects`, `AzureListObjects` | `GCSListObjects` | Required |
| Delete operation | `S3DeleteObjects`, `AzureDeleteObjects` | `GCSDeleteObject` | Required; singular matches the native `DeleteObject` RPC |
| Read stream operation | `S3GetObject`, `AzureGetObject` | `GCSReadObject` | Required; native GCS has distinct `ReadObject` RPC |
| Write stream operation | `S3PutObject`, `AzureUpload` | `GCSWriteObject` | Required; native GCS has distinct `WriteObject` RPC |
| Disk operation duplicates | `DiskS3*`, `DiskAzure*` | `DiskGCSGetObject`, `DiskGCSListObjects`, `DiskGCSDeleteObject`, `DiskGCSReadObject`, `DiskGCSWriteObject` | Required |
| Read buffer bytes/time/errors/init | `ReadBufferFromS3*`, `ReadBufferFromAzure*` | `ReadBufferFromGCSMicroseconds`, `ReadBufferFromGCSInitMicroseconds`, `ReadBufferFromGCSBytes`, `ReadBufferFromGCSRequestsErrors` | Required |
| Write buffer bytes/time/errors | `WriteBufferFromS3*`; Azure write-buffer events are operation-focused | `WriteBufferFromGCSMicroseconds`, `WriteBufferFromGCSBytes`, `WriteBufferFromGCSRequestsErrors` | Required |
| Redirect counters | `S3ReadRequestsRedirects`, `AzureReadRequestsRedirects` | None | Non-applicable because current GCS gRPC code has no redirect-equivalent behavior |
| Multipart upload lifecycle | `S3CreateMultipartUpload`, `S3UploadPart`, `S3CompleteMultipartUpload` | None | Non-applicable for current single `WriteObject` stream |
| Generic remote bandwidth throttler | `RemoteReadThrottler*`, `RemoteWriteThrottler*` | Reuse existing generic events | Required later; no new provider-specific event needed in P01 |

No blocking event-name questions remain for P01.

## Assumptions

- Distinct GCS event names should follow Azure/S3 naming patterns. Confidence: high. Validation path: T001 parity matrix and review before later phases depend on names.
- S3 multipart-specific events are not required for current GCS gRPC parity. Confidence: high. Validation path: T001 confirms current GCS write semantics are a single `WriteObject` stream.
- Actual retry/throttle behavior is not yet implemented in native GCS. Confidence: high. Validation path: P02 will add behavior; P01 only adds event vocabulary and status classification.
- `RESOURCE_EXHAUSTED` should become distinguishable from generic `Unavailable` for throttle accounting. Confidence: medium. Validation path: T004/T005 status API and tests.

## Risks

- Event names become public surface and are painful to rename. Mitigation: finalized the parity matrix before code changes and reviewed the event list before later phases depend on names.
- Adding too many S3-shaped events could create meaningless GCS counters. Mitigation: redirects and multipart lifecycle were marked non-applicable and the smoke check asserts no GCS redirect/multipart events were added.
- Status model changes could break existing tests or callers that assume `RESOURCE_EXHAUSTED` maps to `Unavailable`. Mitigation: `ResourceExhausted` now remains retryable but is distinguishable through `isThrottlingStatus`; targeted `GCSGrpcClientFoundation.*` tests pass.

## Implementation notes

- Minor task-level drift: `src/IO/GCS/GCSClient.cpp` also needed a P01 change because the Google Cloud C++ `kResourceExhausted` conversion path independently collapsed throttling into `Unavailable`. This did not change phase scope or plan order.
- `clang-format` was not on the default `PATH`; `/opt/homebrew/opt/llvm/bin/clang-format` was used by prepending that directory to `PATH`.
- Event smoke check verified 53 required GCS events in `src/Common/ProfileEvents.cpp` and verified 14 redirect/multipart GCS events remain absent.
- Build/test logs are in `build/gcs_grpc_observability_event_model_build.log` and `build/gcs_grpc_observability_event_model_unit.log`; reviewer agents reported a clean target build and 12/12 passing `GCSGrpcClientFoundation.*` tests.
## Deferred or future work

- P02: Wire retry loops, request-throttler behavior, and attempt/retry/throttle counter increments using the P01 vocabulary.
- P03: Wire native disk/object-storage buffer events and blob storage logs.
- P04: Apply the same observability contract to the explicit `gcs` table-function gRPC path when that path exists.
- P05: Produce end-to-end compatibility and regression evidence.

## Handoff summary

Current status:
- P01 is complete: the native GCS event vocabulary and status/throttle classification are implemented, verified, reviewed, and ready for P02 to consume.

Completed artifacts:
- `src/Common/ProfileEvents.cpp`: added distinct `GCS*`, `DiskGCS*`, `ReadBufferFromGCS*`, and `WriteBufferFromGCS*` event definitions.
- `src/IO/GCS/GCSStatus.h`: added `ResourceExhausted` and `isThrottlingStatus` to the status contract.
- `src/IO/GCS/GCSStatus.cpp`: mapped `RESOURCE_EXHAUSTED` distinctly, kept it retryable, and exposed throttling classification.
- `src/IO/GCS/GCSClient.cpp`: mapped Google Cloud C++ `kResourceExhausted` distinctly from `kUnavailable`.
- `src/IO/tests/gtest_gcs_grpc_client.cpp`: added targeted retry/throttle classification coverage.
- `plans/gcs-grpc-observability-event-parity/01-event-vocabulary-and-status-model-review.md`: recorded verification, critique, and reviewer all-clear.

Key decisions:
- `ResourceExhausted` is the only P01 throttling status and remains retryable; `Unavailable` and `DeadlineExceeded` remain retryable but not throttling.
- Current GCS gRPC has no redirect or multipart-equivalent events; those remain intentionally absent.

Assumptions:
- Later phases will increment these events at real behavior boundaries rather than adding synthetic accounting; confidence high.
- Existing S3/XML compatibility paths remain untouched by P01; confidence high based on scoped diff.

Uncertainties:
- None for P01.

Next likely work:
- P02 should wire retry/throttling behavior and accounting using the P01 event vocabulary and `isThrottlingStatus`.