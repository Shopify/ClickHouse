# `MakeGrpcClient` experiment result

## Branch

- Experiment branch: `gcs-grpc-make-grpc-client-experiment`
- Base commit: `046405713d7`
- Separate worktree used for isolation: `tmp/gcs-grpc-s3-perf-parity/p02-client-architecture-comparison/make-grpc-client-worktree`

## Experiment type

Concrete blocker proof from source/API mapping. No production source patch was made because the high-level API mismatch is visible before an implementation patch would become meaningful.

## Read mapping

ClickHouse native read path needs:

- `ReadObjectRequest.read_offset` and `read_limit`;
- a raw stream with controlled `Read` and `Finish`;
- owned `grpc::ClientContext` so cleanup can call `TryCancel`;
- raw `grpc::StatusCode::CANCELLED` on local cleanup;
- per-RPC profile events and throttling;
- fake stream injection for tests.

`storage::MakeGrpcClient` public mapping:

- `Client::ReadObject` can represent ranges through `ReadFromOffset` / `ReadRange` options.
- The returned `ObjectReadStream` is an `std::istream`; it exposes `Close` and final `status`, but not raw `grpc::ClientContext`, raw `ReadObjectResponse`, or raw stream `Finish`.

Blocker:

Direct use of high-level `Client::ReadObject` would lose the raw stream lifecycle hooks needed for current ClickHouse cancellation accounting and fine-grained GCS profile events. Recreating those hooks would require using deprecated/internal surfaces or reintroducing a custom raw-stub wrapper.

## Write mapping

ClickHouse native write path needs:

- raw `WriteObjectRequest` control for `write_object_spec`, attributes, preconditions, `write_offset`, `finish_write`, and temporary-object uploads;
- explicit `Write`, `WritesDone`, and `Finish` status handling;
- ClickHouse throttling and `WriteBufferFromGCS*` accounting;
- current temporary-part compose behavior when parallel upload is enabled.

`storage::MakeGrpcClient` public mapping:

- `Client::WriteObject` returns `ObjectWriteStream` and always uses resumable upload semantics.
- `Client::InsertObject` supports contiguous single-shot upload and may be faster for small objects.
- The library default gRPC upload buffer is 32 MiB, which is useful evidence for P04 write tuning.

Blocker:

Direct use of high-level `ObjectWriteStream` would change the write implementation model and hide raw `WriteObject` stream status. It may be useful as a separate implementation strategy, but it is not a low-risk adapter for the current native write path because it would force changes to instrumentation, throttling, fake streams, and possibly parallel-compose semantics.

## Diff summary

No experiment branch source diff was produced. The branch exists to isolate any future prototype, but P02's current test result is a source-backed blocker rather than a buildable patch.

## P04 recommendation from the experiment

Do not adopt high-level `storage::MakeGrpcClient` directly for production P04. Instead:

1. keep the custom generated-stub interface for raw stream lifecycle control;
2. align channel-count and DirectPath policy with upstream `DefaultOptionsGrpc`;
3. copy or emulate upstream channel arguments, including local subchannel pool, channel id, default authority, DNS SRV query disabling, and optional channel settings where applicable;
4. use upstream write-path evidence, especially the 32 MiB gRPC upload buffer default and upload quantum guidance, when investigating the native write gap.

## Residual uncertainty

A larger, separate design could intentionally replace ClickHouse's native write path with high-level `ObjectWriteStream` and accept coarser instrumentation. That is outside P02/P04's safest path unless the user explicitly chooses a broader rewrite.

## Source references for blocker

- Public creation: `contrib/google-cloud-cpp/google/cloud/storage/grpc_plugin.h` declares `storage::MakeGrpcClient` returning public `storage::Client`; `grpc_plugin.cc` constructs it through `DefaultOptionsGrpc`, `GrpcStub`, and `MakeStorageConnection`.
- Public read abstraction: `contrib/google-cloud-cpp/google/cloud/storage/client.h` exposes `Client::ReadObject` returning `ObjectReadStream`; `object_read_stream.h` exposes `Close` and final `status`, not raw `grpc::ClientContext` or raw stream `Finish`.
- Public write abstraction: `client.h` exposes `Client::WriteObject` returning `ObjectWriteStream`; `object_write_stream.h` documents buffering, `Close`, and metadata status, not raw `grpc::ClientWriterInterface` or raw `WriteObjectResponse` `Finish`.
- Internal/deprecated escape hatch: `client.h` exposes `raw_client` only as deprecated implementor-only API; `storage_connection.h` exposes `ReadObject` as `ObjectReadSource` and write operations as storage-level resumable upload calls, not the generated stream interface.
- Current ClickHouse raw hooks: `src/IO/GCS/GCSClient.h` defines `IStub::readObject` and `IStub::writeObject` in terms of raw gRPC stream interfaces; `GCSObjectStorage.cpp` uses `TryCancel`, raw `Finish`, `read_limit`, `write_offset`, and `finish_write` directly.
