# Native GCS gRPC client architecture comparison

## Recommendation

Select custom generated stubs aligned with upstream `google-cloud-cpp` behavior for P04.

Do not adopt high-level `storage::MakeGrpcClient` directly as the production native GCS client in P04. Use it as the reference for endpoint-aware channel defaults, channel arguments, DirectPath behavior, upload-buffer defaults, and round-robin design.

## Comparison matrix

| Criterion | Custom generated-stub pool | Closer upstream-internal alignment | High-level `storage::MakeGrpcClient` |
|---|---|---|---|
| Correctness | Strong; preserves current raw generated request/response semantics. | Medium-high; depends on internal API stability. | Medium; public API changes stream/write abstraction. |
| Read performance | Proven directionally by prototype; needs configurable/endpoint-aware channel count. | Likely strong if upstream internals are reused carefully. | Unknown in ClickHouse; cannot benchmark without broader adapter. |
| Write performance | Current write gap remains; raw stream control allows targeted tuning. | Could reuse upstream upload-buffer/stall patterns while retaining more control. | Potentially useful for upload buffering, but semantics change to public `ObjectWriteStream`. |
| DirectPath policy | Must be added; current hardcoded 16 is wrong for DirectPath. | Available from upstream defaults if reused. | Built in: one channel for `google-c2p:///`, multi-channel otherwise. |
| Non-DirectPath policy | Must be added; can copy upstream `max(4, hardware_concurrency)` or expose a setting. | Available from upstream defaults if reused. | Built in. |
| Settings surface | Needs ClickHouse setting or endpoint-aware default. | Needs careful exposure of upstream options. | Rich `Options`, but not ClickHouse-native and may not map cleanly. |
| Profile events | Strong; current `OperationEvents` map directly to raw operations. | Medium; may require custom decorators. | Weak for fine-grained events; public streams hide raw RPC lifecycle. |
| Throttling | Strong; current code controls read/write throttling around operations and payload bytes. | Medium; may need custom wrappers. | Coarser; would wrap `istream`/`ostream` operations, not raw RPC calls. |
| Fakes/tests | Strong; existing `FakeStub`, `FakeReadStream`, `FakeWriteStream` match current tests. | Medium; internal interfaces would require new mocks. | Large test-surface change. |
| Cancellation | Strong; raw `grpc::ClientContext` and raw `Finish` status are available. | Medium; depends on internal stream access. | Weak; public stream exposes `Close`/`status`, not raw `TryCancel`/`Finish` flow. |
| Retry/stall behavior | Needs improvement; current stream creation retry is limited. | Can borrow upstream stall/retry patterns selectively. | Best built-in high-level behavior, but hidden behind public streams. |
| GCS-as-`s3` compatibility | No direct effect if scoped to native GCS client. | No direct effect if scoped. | No direct effect if scoped, but larger native rewrite risk. |
| Production risk | Lowest if policy is parameterized and tests are added. | Medium due reliance on non-public APIs. | High for P04 because it is a larger semantic replacement. |
| Testability | Strongest with current seam. | Medium. | Weak unless test suite is redesigned around storage client mocks. |

## Architecture decision

P04 should formalize the custom `IStub` / generated-stub architecture, with upstream-aligned behavior:

- persistent client/channels;
- fresh `grpc::ClientContext` per RPC;
- endpoint-aware channel count: one for `google-c2p:///` and `google-c2p-experimental:///`, multi-channel for normal cloud-path endpoints;
- no hardcoded final `16` channel count;
- channel arguments aligned with upstream where applicable;
- existing ClickHouse instrumentation, throttling, fakes, and cancellation hooks preserved;
- explicit write-path investigation using upstream's 32 MiB gRPC upload buffer and write recommendations as evidence.

## Rejected alternatives

### Direct high-level `storage::MakeGrpcClient`

Rejected for P04 because it hides raw gRPC stream lifecycle and would force broad changes to `GCSReadBuffer`, `GCSWriteBuffer`, profile events, throttling, cancellation classification, and tests.

### Internal upstream `StorageConnection` as the primary abstraction

Rejected as the main P04 direction because `Client::raw_client` is deprecated implementor-only, and `StorageConnection` exposes storage-level abstractions rather than the generated raw streams ClickHouse already uses. It remains a possible reference source for future targeted borrowing.

### Keep hardcoded 16-channel round robin

Rejected because it ignores upstream DirectPath policy and lacks a settings/default contract.

## Acceptance mapping

- A002: satisfied by custom-vs-`MakeGrpcClient` comparison, separate branch evidence, and P04 recommendation.
- A004: P04 receives a concrete implementation direction: formalize custom generated stubs with upstream-aligned channel policy and measured write-path follow-up.
- A006/A008: P06 should benchmark read/write medians and per-run trends only after P03-P05 stabilize the selected architecture.
