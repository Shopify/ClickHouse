# Custom generated-stub/channel-pool analysis

## Files inspected

- `src/IO/GCS/GCSClient.h`
- `src/IO/GCS/GCSClient.cpp`
- `src/Disks/DiskObjectStorage/ObjectStorages/GCS/GCSObjectStorage.cpp`

## Current shape

ClickHouse's native GCS client owns a small `DB::GCS::Client` wrapper around an `IStub` interface. The interface mirrors the raw generated GCS gRPC service closely: unary object operations, `ReadObject` as a `grpc::ClientReaderInterface<ReadObjectResponse>`, and `WriteObject` as a `grpc::ClientWriterInterface<WriteObjectRequest>`.

The current source branch already contains an experimental `RoundRobinStub` and creates 16 generated stubs/channels in `GCS::createClient`. That aligns with upstream's multi-channel non-DirectPath behavior directionally, but it is still not production-ready because the count is hardcoded and not DirectPath-aware.

## Persistent client/channel and per-RPC context lifecycle

- `GCS::createClient` creates long-lived channels and generated stubs once per client.
- `Client::makeContext` creates a fresh `grpc::ClientContext` per RPC/stream and adds request deadline, `x-goog-request-params`, and `x-goog-user-project` metadata.
- This matches the important upstream lifecycle rule: persistent channel/client, fresh context per RPC.

## Endpoint and channel-count policy gaps

- Current code hardcodes `channel_count = 16`.
- Upstream `google-cloud-cpp` uses one channel for `google-c2p:///` DirectPath endpoints and `max(4, hardware_concurrency)` for normal cloud-path endpoints.
- P04 should replace the hardcoded count with endpoint-aware or configurable policy rather than baking in 16.

## Read streaming fit

The custom path is a strong fit for ClickHouse's `GCSReadBuffer` because it exposes:

- raw `ReadObjectRequest` fields, including `read_offset` and `read_limit`;
- a stream object whose `Read` and `Finish` calls are controlled by `GCSReadBuffer`;
- the raw `grpc::ClientContext`, which allows explicit cleanup cancellation with `TryCancel`;
- raw `grpc::Status` at `Finish`, needed to classify local cleanup `CANCELLED` without hiding real `DeadlineExceeded`.

This is why P03/P05 can be implemented cleanly on the custom path.

## Write/upload fit

The custom path exposes raw `WriteObjectRequest` streaming. `GCSWriteBuffer` can:

- set `WriteObjectSpec`, target bucket/object, attributes, and preconditions;
- control `write_offset` and `finish_write`;
- use ClickHouse write throttling around payload writes;
- implement current parallel temporary upload plus compose behavior with `max_concurrent_uploads = 4` and `ComposeObject`;
- record `WriteBufferFromGCSBytes`, upload elapsed time, blob-storage-log events, and failure counters.

This makes the custom path a good fit for preserving existing ClickHouse semantics, but the large staging write gap shows the implementation still needs P04/P06 measurement and probably write-path tuning.

## Profile events, throttling, fakes, and cancellation hooks

Strengths:

- `OperationEvents` maps native GCS reads/writes to `GCS*` and `DiskGCS*` profile events.
- `HTTPRequestThrottler` integration is explicit for get/put classes.
- `FakeStub`, `FakeReadStream`, and `FakeWriteStream` provide direct unit-test seams for object operations and streaming failures.
- Raw `grpc::Status` is available before conversion to ClickHouse `GCS::Status`.

Weaknesses:

- `AccountingReader::Finish` currently records every non-OK finish before higher-level read cleanup can decide that local `CANCELLED` was expected.
- Retry behavior is less mature than the S3 path: stream creation can retry, but mid-stream read/write behavior is mostly surfaced to the caller.
- The channel policy duplicates upstream behavior and currently misses DirectPath-aware defaults.

## P04 implication

The custom path should remain the default production direction unless the `MakeGrpcClient` experiment proves high-level APIs can preserve the same hooks. The safest P04 route is to formalize the custom generated-stub path while aligning channel/default policy with upstream `google-cloud-cpp`, then address write-path bottlenecks explicitly.
