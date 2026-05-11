# Retry and throttling foundation Notes

Plan: [plan.md](./plan.md)
Phase: P02 / `02-retry-and-throttling-foundation`

## Implementation context

- `src/IO/GCS/GCSClient.h` and `src/IO/GCS/GCSClient.cpp` are the shared native gRPC client boundary for `GetObject`, `ListObjects`, `DeleteObject`, `ReadObject`, and `WriteObject`.
- `src/IO/GCS/GCSStatus.*` now exposes `isRetryableStatus` and `isThrottlingStatus`; P02 should consume those helpers rather than duplicating status lists.
- P01 added the GCS event vocabulary in `src/Common/ProfileEvents.cpp`; P02 should increment only events that represent real attempts, retryable errors, throttling, and request-throttler blocking.
- `src/Disks/DiskObjectStorage/ObjectStorages/GCS/GCSObjectStorage.cpp` parses native GCS client settings and currently creates `GCS::Client` through `GCS::createClient`.
- `src/IO/tests/gtest_gcs_grpc_client.cpp` already has a fake-service seam through `GCS::FakeStub`, `FakeReadStream`, and `FakeWriteStream`; prefer extending that seam for deterministic retry and throttler tests.
- `src/IO/HTTPRequestThrottler.h` is the existing provider request-throttler helper used by S3; it already accounts provider throttler blocked events and optional disk-specific events.
- S3 retry accounting in `src/IO/S3/Client.cpp` is a useful precedent for counting attempts and retryable errors, but GCS must preserve its own event names and fail-close streaming boundaries.
- Generic remote bandwidth throttling via `CurrentThread::ReadThrottlingScope` and `CurrentThread::WriteThrottlingScope` is expected around byte movement; P02 should document or expose the boundary, while P03 owns disk buffer byte-moving scopes.
- Future C++ changes must use Allman-style braces. Build/test output must go to files under `build/`, and log summaries should be produced by a subagent during phase work.
- Existing unrelated worktree changes in `contrib/liburing`, `contrib/sysroot`, and `tmp/` must remain untouched.

## Investigation context

- Investigation file: [investigation.md](./investigation.md)
- Relevant findings: F002, F005, F006, F007, F008.
- Relevant constraints: C003, C006, C007, C008.
- Relevant assumptions to validate: AS004, AS005; grey areas G002 and G003.
- Relevant open questions/blockers: None.

## Decisions from planning

- D001: Use distinct native GCS event names, so P02 must increment `GCS*` and `DiskGCS*` events rather than reusing S3 counters.
- D003: Keep retry/throttle behavior and accounting in the same implementation stream, because counters are only trustworthy when they reflect actual behavior.
- D005: Do not block disk/object-storage observability on the future explicit table-function gRPC path; P02 should keep the shared client boundary reusable.
- P01 review decision: `ResourceExhausted` is the throttling status and remains retryable; `Unavailable` and `DeadlineExceeded` remain retryable but not throttling.

## P02 retry-safety and accounting matrix

| Operation | Retry boundary | Classification | Request-throttler category | Attempt/retry/throttle accounting | Bandwidth-throttler handoff |
|---|---|---|---|---|---|
| `GetObject` | Whole unary RPC before response is returned | Safe: metadata lookup is idempotent | GET/read | `GCSRead*`, `DiskGCSRead*`, `GCSGetObject`, `DiskGCSGetObject` | No byte movement; none for P03 |
| `ListObjects` | Whole unary RPC before response is returned | Safe: listing is read-only | PUT/write by existing provider convention for non-read/write aggregate | `GCSWrite*`, `DiskGCSWrite*`, `GCSListObjects`, `DiskGCSListObjects` | No byte movement; none for P03 |
| `DeleteObject` | Whole unary RPC before response is returned | Safe for transport retry under current object-storage delete semantics; non-retryable statuses still fail after one attempt | PUT/write | `GCSWrite*`, `DiskGCSWrite*`, `GCSDeleteObject`, `DiskGCSDeleteObject` | No byte movement; none for P03 |
| `ReadObject` | Stream creation only, before a stream is returned to the caller | Limited: retry stream creation failures only; after a stream is returned, finish/read failures are accounted and fail closed | GET/read | `GCSRead*`, `DiskGCSRead*`, `GCSReadObject`, `DiskGCSReadObject` | P03 applies `CurrentThread::ReadThrottlingScope` around disk read buffers |
| `WriteObject` | Stream creation only, before a stream is returned to the caller | Limited/fail-close: retry stream creation failures only; after payload may be accepted, finish/write failures are accounted and not replayed | PUT/write | `GCSWrite*`, `DiskGCSWrite*`, `GCSWriteObject`, `DiskGCSWriteObject` | P03 applies `CurrentThread::WriteThrottlingScope` around disk write buffers |

No blocking retry-policy questions remain for P02. Retryable statuses are provided by `GCS::isRetryableStatus`; throttling is provided by `GCS::isThrottlingStatus`.

## Assumptions

- Native GCS retries should begin with idempotent metadata/list/delete and stream-creation boundaries, then fail closed wherever replay safety is not proven. Confidence: high. Validation path: T001 retry-safety matrix and T006 stream tests.
- `WriteObject` must not replay accepted payload data after a stream write or finish failure unless P02 proves the boundary is safe. Confidence: high. Validation path: T001 and T006.
- `ResourceExhausted` is the only P01 throttling status. Confidence: high after P01 tests. Validation path: T005/T008 event-delta tests.
- Shared client-level retry/throttle accounting can serve disk-native GCS now and future explicit table-function gRPC later. Confidence: medium. Validation path: T002/T003 should keep disk/provider event selection explicit rather than hard-coding only disk behavior.
- Existing S3/XML GCS compatibility paths are out of scope and should not be touched. Confidence: high. Validation path: scoped diff review before commit in `/phase-work`.

## Risks

- Retrying streaming writes incorrectly could duplicate or corrupt object data. Mitigation: require a retry-safety matrix first and fail closed after any payload may have been accepted.
- Counters can become misleading if retry attempts are counted without actual RPC attempts. Mitigation: T005/T008 require fake-service request counts and `ProfileEvents` deltas to match.
- Request-throttler tests can become flaky if they depend on wall-clock timing. Mitigation: use deterministic throttler settings and assert positive blocked/sleep accounting only when blocking is forced.
- Adding client settings could accidentally alter default native GCS disk behavior. Mitigation: T002 requires current behavior to be preserved unless configured and excludes S3/XML settings.
- P02 could drift into disk blob-log or table-function routing work. Mitigation: keep blob storage logs for P03 and table-function compatibility for P04.
## Implementation notes

- Retry control is exposed through `GCS::ClientSettings::max_retry_attempts`; the default remains one attempt so existing native GCS behavior is preserved unless configured.
- Native GCS disk construction marks client settings as disk mode and parses optional `max_retry_attempts`, `get_request_throttler_max_speed`, and `put_request_throttler_max_speed` settings under the native GCS disk configuration prefix.
- Shared `GCSClient` operation metadata maps each RPC to provider/disk operation events, aggregate read/write events, and GET/PUT request-throttler categories.
- Unary `GetObject`, `ListObjects`, and `DeleteObject` retry retryable statuses up to the configured attempt bound; non-retryable statuses fail after one actual RPC attempt.
- `ReadObject` and `WriteObject` retry only null stream creation before returning a stream. After a stream is returned, `Finish` failures are accounted by wrapper streams and are not replayed.
- Request-throttler tests use deterministic zero-burst throttlers so provider and disk blocked/sleep events are observable without external services.
- Minor task-level drift: `src/Disks/DiskObjectStorage/ObjectStorages/GCS/GCSObjectStorage.h` changed only because it was included in the required P02 `clang-format` command.

## Deferred or future work

- P03: Apply disk buffer `ReadBufferFromGCS*` and `WriteBufferFromGCS*` byte/timing/error accounting, generic remote bandwidth throttling scopes, and blob storage logs around actual disk byte movement.
- P04: Validate or wire the future explicit `gcs` table-function gRPC path to the shared P02 client behavior without changing default S3/XML behavior.
- P05: Produce end-to-end regression evidence across native GCS and compatibility paths.

## Handoff summary

Current status:
- P02 is complete: retry/throttle settings, accounting boundaries, fail-close retry behavior, request-throttler accounting, and fake-service verification are implemented, reviewed, and ready for P03 to consume.

Completed artifacts:
- `src/IO/GCS/GCSClient.h`: added retry and request-throttler settings plus fake-service scripting fields.
- `src/IO/GCS/GCSClient.cpp`: added operation metadata, retry loops for safe unary calls, stream-creation retry boundaries, stream finish accounting wrappers, and request-throttler event wiring.
- `src/Disks/DiskObjectStorage/ObjectStorages/GCS/GCSObjectStorage.cpp`: parsed native GCS retry/throttler settings and marked native disk clients for disk-specific events.
- `src/Disks/DiskObjectStorage/ObjectStorages/GCS/GCSObjectStorage.h`: formatted by the required P02 formatter command.
- `src/IO/tests/gtest_gcs_grpc_client.cpp`: added retryable, throttled, non-retryable, request-throttler, stream-creation retry, and write no-replay tests.
- `plans/gcs-grpc-observability-event-parity/02-retry-and-throttling-foundation-review.md`: recorded verification, critique, and reviewer all-clear.

Key decisions:
- Default native GCS retry attempts remain one to preserve existing behavior unless configured.
- Stream `Finish` failures are accounted but not retried because payload or bytes may already have crossed the boundary.
- P03 remains responsible for byte-movement throttling scopes around disk read/write buffers.

Assumptions:
- `ResourceExhausted` remains the only GCS throttling status; confidence high based on P01 and P02 tests.
- Shared client-level accounting is reusable by future explicit table-function gRPC work; confidence medium because P04 still has to validate the table-function integration boundary.

Uncertainties:
- None for P02.

Next likely work:
- None in this session; P03 task files should be created before the next `/phase-work` session if they do not already exist.
