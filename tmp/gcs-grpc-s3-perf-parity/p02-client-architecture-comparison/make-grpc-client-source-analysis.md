# High-level `storage::MakeGrpcClient` source analysis

## Files inspected

- `contrib/google-cloud-cpp/google/cloud/storage/client.h`
- `contrib/google-cloud-cpp/google/cloud/storage/grpc_plugin.h`
- `contrib/google-cloud-cpp/google/cloud/storage/grpc_plugin.cc`
- `contrib/google-cloud-cpp/google/cloud/storage/object_read_stream.h`
- `contrib/google-cloud-cpp/google/cloud/storage/object_write_stream.h`
- `contrib/google-cloud-cpp/google/cloud/storage/internal/storage_connection.h`
- `contrib/google-cloud-cpp/google/cloud/storage/internal/object_read_source.h`
- `contrib/google-cloud-cpp/google/cloud/storage/internal/object_write_streambuf.h`
- `contrib/google-cloud-cpp/google/cloud/storage/internal/grpc/default_options.cc`
- `contrib/google-cloud-cpp/google/cloud/storage/internal/grpc/stub.cc`
- `contrib/google-cloud-cpp/google/cloud/storage/internal/storage_stub_factory.cc`
- `contrib/google-cloud-cpp/google/cloud/storage/internal/storage_round_robin_decorator.cc`
- `contrib/google-cloud-cpp/google/cloud/grpc_options.h`

## What `MakeGrpcClient` provides

`storage::MakeGrpcClient` returns a public `google::cloud::storage::Client` configured for the gRPC storage backend. It applies `DefaultOptionsGrpc`, enables gRPC metrics when safe, creates a `GrpcStub`, and constructs a decorated `StorageConnection`.

Useful upstream behavior:

- `DefaultOptionsGrpc` sets the default gRPC upload buffer to 32 MiB.
- It defaults to `google-c2p:///storage.googleapis.com` when GCP is detected and no endpoint/universe-domain option is set.
- It computes `GrpcNumChannelsOption` from the endpoint: one channel for `google-c2p:///` or `google-c2p-experimental:///`; otherwise `max(4, hardware_concurrency)`.
- `CreateStorageStub` creates the channel vector, sets `GRPC_ARG_USE_LOCAL_SUBCHANNEL_POOL`, sets `GRPC_ARG_CHANNEL_ID`, wraps stubs in `StorageRoundRobin`, and starts channel refresh.
- `GrpcNumChannelsOption` is explicitly documented as applying to `storage::MakeGrpcClient`.

## Public read API fit

The public `Client::ReadObject` returns `ObjectReadStream`, an `std::istream` wrapper. It supports range options such as `ReadFromOffset` and `ReadRange`, and errors are surfaced through stream state and `ObjectReadStream::status` after reading/closing.

Fit for ClickHouse:

- Good: range reads can represent offset/bounded reads.
- Good: the library owns upstream channel pooling, DirectPath defaults, retry/stall machinery, and checksum handling.
- Poor: the public stream hides raw `grpc::ClientContext`, `ReadObjectResponse` messages, and raw `Finish` status.
- Poor: expected local cleanup cancellation cannot be classified using raw `grpc::StatusCode::CANCELLED` at the point ClickHouse currently needs it.
- Poor: per-response accounting and exact gRPC stream lifecycle instrumentation would need to move to coarser stream-level accounting or rely on internal APIs.

## Public write API fit

The public `Client::WriteObject` returns `ObjectWriteStream`, an `std::ostream` wrapper. Its documentation recommends unformatted `.write`, explains the 256 KiB upload quantum, and notes that `WriteObject` always uses resumable uploads. `InsertObject` is recommended for contiguous small objects and performs single-shot upload.

Fit for ClickHouse:

- Good: upstream write path has a 32 MiB default gRPC upload buffer and mature buffering recommendations.
- Good: public options include `UploadBufferSize`, precondition options, metadata options, `UserProject`, `AutoFinalize`, and resumable-session controls.
- Poor: `ObjectWriteStream` hides raw `WriteObjectRequest`, raw `grpc::ClientWriterInterface`, and raw `Finish` status.
- Poor: the public path changes semantics from the current generated `WriteObject` stream plus ClickHouse-managed temporary-part compose path to a library-managed resumable upload stream.
- Poor: ClickHouse's current fine-grained `WriteBufferFromGCSBytes`, throttling, blob-storage-log, and failure accounting would have to wrap public stream operations rather than actual gRPC request/finish outcomes.

## Internal API fit

`Client::raw_client` returns `StorageConnection`, but it is explicitly deprecated as implementor-only. `StorageConnection::ReadObject` returns `ObjectReadSource`, whose `Read` method exposes bytes plus an HTTP-shaped response abstraction, not raw gRPC messages. `StorageConnection` write methods expose resumable upload abstractions such as `CreateResumableUpload`, `UploadChunk`, and `DeleteResumableUpload`, not the same raw generated `WriteObject` streaming interface ClickHouse currently wraps.

Using these internals could recover some control, but it would couple ClickHouse to deprecated/internal API surfaces and still not expose the exact raw stream status hooks that the custom generated-stub path already has.

## Instrumentation and fakes

High-level `MakeGrpcClient` has its own mockable connection layer, but adopting it directly would replace ClickHouse's current `IStub` seam. That is a large test-surface change. Preserving current `FakeStub` behavior would require an adapter that either:

1. wraps high-level `Client` and accepts coarser instrumentation, or
2. drops below high-level `Client` into upstream internal stubs, which defeats the main simplicity benefit of `MakeGrpcClient`.

## Conclusion

The high-level `storage::MakeGrpcClient` path is excellent evidence for channel policy, DirectPath defaults, upload-buffer defaults, and round-robin behavior. It is not a direct production replacement for ClickHouse's current native GCS client if P04 must preserve raw gRPC stream lifecycle control, exact profile-event accounting, local cancellation classification, throttling hooks, and existing fakes.

The practical P04 choice should be custom generated stubs aligned with upstream defaults, or a narrower internal-alignment approach, not direct high-level `storage::Client` replacement.
