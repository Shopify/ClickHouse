# GCS gRPC S3 performance parity investigation

Investigation status: ready

Plan slug: gcs-grpc-s3-perf-parity

## User goal

Investigate differences between the existing ClickHouse S3-compatible object storage implementation and the native GCS gRPC implementation, inspect the GCS gRPC library and usage patterns, and determine why native GCS gRPC queries are slower than the S3-compatible path.

## Feedback loop state

Iteration: 2

Questions asked directly so far:
- Q001, 2026-05-12: User selected existing GCS-as-`s3` XML/API behavior against GCS as the baseline.
- Q002, 2026-05-12: User selected elapsed query time as the primary parity bar, with CPU, allocations, and bytes also important.
- Q003, 2026-05-12: User selected a serious comparison between the custom 16-channel/bounded-read prototype and adopting `google-cloud-cpp` `storage::MakeGrpcClient` behavior.

Current questions for the user:
- None.

User answers incorporated:
- Baseline is existing GCS-as-`s3` XML/API behavior against GCS, not AWS S3. This fixes the comparison target and keeps existing GCS-compatible behavior as the control.
- Primary parity metric is elapsed query time, with CPU, allocations, and bytes/request counts as important secondary diagnostics. This makes the future plan optimize user-visible scan performance while preventing silly hidden regressions.
- Future planning should compare both architecture paths: formalizing the custom 16-channel/bounded-read prototype and adopting or closely aligning with `google-cloud-cpp` `storage::MakeGrpcClient`. This broadens the plan from a simple cleanup to an evidence-based architecture comparison.

Ready to stop asking: yes

Reason:
- The plan-shaping questions about baseline, primary metric, and architecture comparison have been answered. Remaining grey areas can be resolved by measurement and validation inside `/phase-plan` and later phase work.

## Normalized problem statement

Native GCS gRPC object storage reads are slower than the existing S3-compatible path for the same GCS-backed `MergeTree` scan because the native path differs in connection/channel pooling, range-bound handling, remote filesystem wrapper integration, cancellation/error accounting, retry behavior, and protobuf/Cord copy overhead. The next plan should turn the measured investigation into a clean, upstreamable performance-parity path while preserving existing GCS-as-`s3` compatibility.

## User model

Stated request:
- Create or update only `plans/gcs-grpc-s3-perf-parity/investigation.md`.
- Investigate the differences between ClickHouse S3 and native GCS gRPC implementations.
- Investigate the GCS gRPC library and other GitHub usages to determine whether ClickHouse uses it correctly.
- Determine why gRPC queries are slower than S3.
- Ask if `gcs-grpc-s3-perf-parity` or the goal is missing; both were supplied.

Inferred goal:
- The user wants a plan-ready, evidence-backed brief for optimizing native GCS gRPC read performance until it is close to the S3-compatible GCS path. Confidence: high.
- The user likely wants the investigation to preserve recent remote benchmark findings and turn them into a formal next plan, not to implement more code now. Confidence: high.

Likely success criteria:
- A future `/phase-plan` can name the dominant bottlenecks, choose an optimization direction, and define validation against the same GCS dataset/table. Confidence: high.
- Native GCS gRPC full-table scan elapsed time approaches the S3-compatible GCS path under controlled cache and direct-connectivity conditions, with CPU, allocations, and bytes/request counts tracked as secondary diagnostics. Confidence: high.
- Successful query cleanup stops producing misleading `GCSReadRequestsErrors` / log noise for expected local cancellation. Confidence: medium.

Hard constraints:
- Only `plans/gcs-grpc-s3-perf-parity/investigation.md` may be created or updated. Source: user prompt.
- Do not create or update `plan.md`, phase task files, notes, review files, implementation files, code changes, or commits. Source: user prompt.
- Existing user/worktree changes must be left untouched. Source: user prompt and `git status`.
- Use repository-local `tmp` for temporary files if needed. Source: `AGENTS.md`.

Soft preferences:
- Preserve persistent gRPC clients/channels rather than creating a new channel per request. Evidence: prior conversation and upstream `google-cloud-cpp` model. Confidence: high.
- Prefer evidence from real benchmark/profile events over theory. Evidence: user supplied/accepted remote test workflow and query measurements. Confidence: high.
- Keep existing GCS-as-`s3` behavior as a baseline, not a target to mutate. Evidence: prior `grpc-for-gcs` and `gcs-grpc-perf` plans. Confidence: high.

Authority boundaries:
- Allowed: inspect repository files, prior plan artifacts, vendored/external GCS gRPC documentation, and write this investigation artifact.
- Forbidden: source edits, test edits, benchmark harness edits, staging, commits, or implementation planning in detail.
- Unclear: exact elapsed-time margin for parity; non-blocking because the future plan can define an initial target and report measured deltas.

Likely user assumptions:
- `S3` means the existing GCS-as-`s3` XML/API path used as the benchmark baseline, not AWS S3. Evidence: user answer Q001. Confidence: high. Validation: phase-plan benchmark contract.
- Query elapsed time is the primary parity metric, with CPU, allocations, bytes, and request counts as secondary diagnostics. Evidence: user answer Q002. Confidence: high. Validation: phase-plan acceptance criteria.
- The next plan should compare both the custom 16-channel/bounded-read prototype and `google-cloud-cpp` `storage::MakeGrpcClient` alignment instead of assuming one architecture. Evidence: user answer Q003. Confidence: high. Validation: phase-plan option comparison.

## Investigation scope

In scope:
- Native GCS gRPC client and read-buffer code in `src/IO/GCS` and `src/Disks/DiskObjectStorage/ObjectStorages/GCS`.
- Existing S3-compatible object storage read path in `src/Disks/DiskObjectStorage/ObjectStorages/S3` and `src/IO/ReadBufferFromS3.cpp`.
- Remote filesystem wrapper behavior that mediates `MergeTree` reads.
- Vendored `google-cloud-cpp` GCS gRPC client behavior and official/external usage guidance.
- Recent remote benchmark observations from this investigation session.
- Prior related plan artifacts, especially staleness/conflict with `plans/gcs-grpc-perf`.

Out of scope:
- Implementing optimizations or tests.
- Editing benchmark harnesses.
- Producing a detailed task breakdown.
- Committing or staging.
- Making the `gcs` table function use native gRPC.
- Broad write/copy/backup parity, except where current source state affects read-performance framing.

## Executive findings

- F001: The original native GCS slowdown was dominated by insufficient gRPC channel parallelism.
  Evidence: Current `GCS::createClient` in dirty worktree creates 16 channels and a `RoundRobinStub` at `src/IO/GCS/GCSClient.cpp:527` and `:801-820`; remote benchmark observation improved native default full scan from roughly 9.6s to roughly 4.1s after this change.
  Confidence: high
  Plan impact: Phase-plan should formalize configurable multi-channel behavior or deliberately adopt the upstream `google-cloud-cpp` channel/stub factory behavior instead of single-channel use.

- F002: Upstream `google-cloud-cpp` does not use a single channel for ordinary cloud-path GCS gRPC; it defaults to `max(4, hardware_concurrency)` channels and round-robins stubs, while DirectPath endpoints default to one channel.
  Evidence: `contrib/google-cloud-cpp/google/cloud/storage/internal/grpc/default_options.cc:40-56` and `:121-126`; `storage_stub_factory.cc:67-84`; `storage_round_robin_decorator.cc:251-256`; `grpc_options.h:51-63`.
  Confidence: high
  Plan impact: A robust solution should avoid hardcoding one channel and should account for cloud-path versus `google-c2p` DirectPath semantics.

- F003: Native GCS previously claimed right-bounded reads but did not honor the upper bound; the dirty-worktree fix reduced over-read bytes to S3-like levels and improved elapsed time.
  Evidence: `GCSReadBuffer::supportsRightBoundedReads` at `GCSObjectStorage.cpp:208-209`; dirty-worktree `setReadUntilPosition` and `sequentialReadLimit` bound `read_limit` at `:226-234` and `:328-353`; remote benchmark observation changed `ReadBufferFromGCSBytes` from about 841 MB for about 752 MB compressed input to about 752 MB.
  Confidence: high
  Plan impact: Phase-plan should keep bounded-read correctness as a required acceptance criterion and add coverage for `remote_filesystem_read_method='threadpool'` exact-range behavior.

- F004: With 16 channels plus bounded reads, native GCS repeat scans reached about 1.6s versus S3 repeat scans around 1.1-1.4s on the same data, so the remaining gap is smaller and likely due to copy/CPU, external-buffer integration, cancellation/accounting, retry behavior, or environment variance.
  Evidence: Remote benchmark observations in this session: native repeats `1.637s`, `1.642s`, `1.610s`; S3 repeats `1.423s`, `1.344s`, `1.110s`.
  Confidence: medium
  Plan impact: The next plan should not chase vague “gRPC is slow”; it should validate the two big fixes and then profile residual overhead.

- F005: Native GCS still differs from S3 in nested remote-read integration: S3 receives and honors `remote_read_buffer_use_external_buffer`, while native GCS currently constructs `GCSReadBuffer` with its own buffer size and does not pass that setting.
  Evidence: `DiskObjectStorage::readFile` requires `use_external_buffer=true` for nested buffers at `DiskObjectStorage.cpp:763-784`; `ReadSettings::withNestedBuffer` sets it at `ReadSettings.cpp:50-56`; S3 passes it at `S3ObjectStorage.cpp:241-254`; GCS does not pass it at `GCSObjectStorage.cpp:1245-1255`.
  Confidence: high
  Plan impact: Phase-plan should investigate whether external-buffer support is needed for full parity or whether bounded streams already make its impact negligible.

- F006: Native GCS cleanup cancellation handling is directionally fixed to treat raw `grpc::StatusCode::CANCELLED` as expected, but `AccountingReader::Finish` still records a failed finish status before higher-level cleanup decides it is expected.
  Evidence: `GCSObjectStorage.cpp:399-438` checks raw `grpc::StatusCode::CANCELLED`; `AccountingReader::Finish` records any non-OK finish status at `GCSClient.cpp:412-420`; remote profile events still showed hundreds of `GCSReadRequestsErrors` on successful scans.
  Confidence: high
  Plan impact: Phase-plan should include a metrics/log-noise correction so profile events do not label expected local cancellations as remote GCS failures.

- F007: S3 has mature retry behavior for body reads and bounded range reads; native GCS retries stream creation but not mid-stream `Read` / `Finish` failures.
  Evidence: `ReadBufferFromS3::nextImpl` and `readBigAt` retry with backoff at `ReadBufferFromS3.cpp:96-180` and `:247-285`; `GCS::Client::readObject` only retries failure to create a stream at `GCSClient.cpp:729-763`.
  Confidence: high
  Plan impact: Retry parity may not explain the happy-path benchmark gap, but it affects production robustness and should be considered after read throughput parity.

- F008: The existing `plans/gcs-grpc-perf/investigation.md` overlaps this idea but is now partially stale because current source has `GCS*` profile events, parallel write support, native compatible rewrite copy, persistent sequential reads, and the uncommitted multi-channel/bounded-read prototype.
  Evidence: `ProfileEvents.cpp:753-805`; `GCSObjectStorage.h:77`; `GCSObjectStorage.cpp:1368-1405`; current dirty diff; previous investigation text says those were absent/deferred.
  Confidence: high
  Plan impact: This investigation should supersede stale performance-attribution sections while preserving the baseline/environment framing.

## Evidence and belief register

| ID | Claim | Type | Evidence | Confidence | Would change if |
|---|---|---|---|---|---|
| B001 | Target investigation and goal were supplied by the user. | fact | User prompt names `gcs-grpc-s3-perf-parity` and goal. | high | User revises slug or goal. |
| B002 | The worktree was dirty before this artifact, including two directly relevant GCS source files. | fact | `git status --short`: `M src/Disks/DiskObjectStorage/ObjectStorages/GCS/GCSObjectStorage.cpp`, `M src/IO/GCS/GCSClient.cpp`, plus modified contrib submodules. | high | Git status changes later. |
| B003 | Target plan directory did not exist before this investigation. | fact | `ls -la plans/gcs-grpc-s3-perf-parity` returned no entries. | high | Another process creates it. |
| B004 | Current dirty `GCSClient.cpp` uses 16 channels and round-robin stubs. | fact | `src/IO/GCS/GCSClient.cpp:527-590`, `:801-820`. | high | Prototype is reverted or parameterized. |
| B005 | Upstream `google-cloud-cpp` uses multiple channels by default for non-DirectPath endpoints. | fact | `default_options.cc:40-56`, `storage_stub_factory.cc:67-84`. | high | Vendored version changes. |
| B006 | Upstream DirectPath endpoint defaults to one channel because gRPC does its own load balancing. | fact | `default_options.cc:40-47`; docs mention `google-c2p:///storage.googleapis.com`. | high | Google library changes DirectPath behavior. |
| B007 | Fresh `grpc::ClientContext` per RPC/stream is normal and should not be “optimized” by reuse. | fact | Vendored grpc stub code and prior source review create contexts per request; `GCS::Client::readObject` does the same at `GCSClient.cpp:737`. | high | Upstream gRPC model changes, unlikely. |
| B008 | The previous single-channel native path was much slower than S3-compatible GCS in the remote test. | observation | Session benchmark: native roughly 9.6s/9.8s versus S3 roughly 1.4-3.0s depending settings. | medium | Raw logs/config reveal uncontrolled environment differences. |
| B009 | Multi-channel native GCS materially improved elapsed time. | observation | Session benchmark: native default about 4.1s after 16-channel prototype. | high | Repeated clean benchmark fails to reproduce. |
| B010 | Bounded-read fix removed about 90 MB of over-read in the tested scan. | observation | Session profile events: `ReadBufferFromGCSBytes` dropped from about 841 MB to about 752 MB, matching `ReadCompressedBytes`/S3. | high | Raw profile event capture was misattributed. |
| B011 | S3 read path honors external-buffer mode, native GCS currently does not. | fact | `S3ObjectStorage.cpp:241-254`; `GCSObjectStorage.cpp:1245-1255`; `ReadSettings.cpp:50-56`. | high | GCS implementation adds external-buffer support. |
| B012 | Native GCS uses protobuf `ReadObjectResponse` / `absl::Cord` and copies chunks into ClickHouse buffers. | fact | `GCSObjectStorage.cpp:282-297`, `:460-470`, `:552-560`. | high | A zero-copy or direct buffer fill path is added. |
| B013 | Successful cleanup cancellation can still inflate `GCSReadRequestsErrors`. | inference | `AccountingReader::Finish` records non-OK before `finishSequentialStream` filters expected cancellation; benchmark still observed hundreds of errors. | high | Accounting is changed to receive cancellation intent or classify raw status. |
| B014 | Existing `gcs-grpc-perf` remains useful for baseline/environment framing but stale on implementation details. | fact | Existing investigation plus current source events/write/copy behavior. | high | The branch changes again or older plan is intentionally authoritative. |
| B015 | Direct-connectivity conditions materially affect whether GCS gRPC should outperform XML/HTTP. | fact | Google docs: `https://docs.cloud.google.com/storage/docs/enable-grpc-api`, `https://docs.cloud.google.com/storage/docs/direct-connectivity`; vendored docs. | high | Benchmark target explicitly excludes DirectPath/direct connectivity. |
| B016 | The future plan should preserve existing GCS-as-`s3` behavior. | constraint | Prior `grpc-for-gcs` plans and docs. | high | User asks to replace the S3-compatible path. |

## Repository context inspected

| Path / command | Why inspected | Key findings | Confidence |
|---|---|---|---|
| `git status --short && git branch --show-current` | Worktree safety | Branch `gcs-grpc-coherent`; relevant dirty files are `GCSObjectStorage.cpp` and `GCSClient.cpp`; contrib submodules also modified. | high |
| `AGENTS.md` | Project instructions | Do not rebase/amend, do not commit to master, no broad code changes, use `tmp`, build/test logging rules. | high |
| `README.md` | Project overview | General ClickHouse project; no GCS parity-specific guidance. | high |
| `plans/gcs-grpc-s3-perf-parity/` | Existing target investigation check | Directory/investigation did not exist. | high |
| `plans/gcs-grpc-perf/investigation.md` | Prior overlapping perf investigation | Useful read-heavy GCS-vs-S3 framing, but now stale on events/write/copy/current benchmark evidence. | high |
| `plans/grpc-for-gcs/investigation.md` and `plans/grpc-for-gcs/plan.md` | Historical native GCS constraints | Preserve existing GCS-as-`s3`; native GCS is explicit `object_storage_type=gcs`; direct-connectivity perf target. | high |
| `src/IO/GCS/GCSClient.cpp` | Native gRPC client behavior | Operation events, accounting wrapper, round-robin prototype, stream creation retry, channel creation. | high |
| `src/IO/GCS/GCSStatus.cpp` | Retry/status mapping | Retryable statuses include resource exhausted, unavailable, deadline exceeded; cancellation status handling occurs higher in read buffer. | high |
| `src/Disks/DiskObjectStorage/ObjectStorages/GCS/GCSObjectStorage.cpp` | Native GCS read/write/copy behavior | Read buffer supports bounded reads in dirty tree, uses gRPC streaming/Cord copies, expected cancellation filtering, native compatible rewrite copy. | high |
| `src/Disks/DiskObjectStorage/ObjectStorages/GCS/GCSObjectStorage.h` | Capability surface | Current dirty tree advertises `supportParallelWrite=true`. | high |
| `src/Disks/DiskObjectStorage/ObjectStorages/S3/S3ObjectStorage.cpp` | S3 baseline read object construction | S3 passes external-buffer and restricted-seek settings into `ReadBufferFromS3`. | high |
| `src/IO/ReadBufferFromS3.cpp` | S3 range, retry, external-buffer behavior | Exact HTTP `Range` requests, `use_external_buffer`, read retries/backoff, bounded `readBigAt`. | high |
| `src/Disks/DiskObjectStorage/DiskObjectStorage.cpp` | Remote FS wrapper stack | `AsynchronousBoundedReadBuffer` / gather stack requires nested buffers to use external buffer. | high |
| `src/IO/ReadSettings.cpp` | Nested read settings | `withNestedBuffer` sets `remote_read_buffer_use_external_buffer=true`. | high |
| `src/Disks/IO/ReadBufferFromRemoteFSGather.cpp` | Propagation of read bounds | Propagates `setReadUntilPosition` into provider buffers. | high |
| `src/Common/ProfileEvents.cpp` | GCS/S3 attribution | Current tree has `GCS*`, `DiskGCS*`, and `ReadBufferFromGCS*` event families. | high |
| `contrib/google-cloud-cpp/google/cloud/storage/internal/grpc/default_options.cc` | Upstream defaults | Non-DirectPath default channel count is `max(4, hardware_concurrency)`; DirectPath default is 1. | high |
| `contrib/google-cloud-cpp/google/cloud/storage/internal/storage_stub_factory.cc` | Upstream channel/stub construction | Creates vector of channels/stubs, sets local subchannel pool and channel id, wraps with round robin. | high |
| `contrib/google-cloud-cpp/google/cloud/grpc_options.h` | Tuning semantics | `GrpcNumChannelsOption` exists; docs say gRPC limits simultaneous calls per channel to 100. | high |
| `contrib/google-cloud-cpp/google/cloud/storage/doc/storage-grpc.dox` | Upstream usage guidance | Official usage is `storage::MakeGrpcClient`; cloud path works anywhere, GCE/GKE defaults to direct connectivity for best performance. | high |
| Subagent scout/analyst/critic outputs | Independent lenses | Confirmed target artifact absence, source differences, upstream channel behavior, stale prior-plan findings, and key questions. | medium |

## External research consulted

| Source | Why consulted | Key findings | Plan impact |
|---|---|---|---|
| `https://docs.cloud.google.com/storage/docs/enable-grpc-api` | Validate official GCS gRPC performance and C++ usage conditions | GCS gRPC is intended for analytics-like workloads and direct connectivity on Google Cloud; C++ uses gRPC-capable storage client. | Benchmark claims must state endpoint/direct-connectivity conditions. |
| `https://docs.cloud.google.com/storage/docs/direct-connectivity` | Validate DirectPath/direct-connectivity requirements | Requires Google Cloud environment, colocated VM/bucket, service account/auth, routes/firewall/endpoints; includes diagnostic tooling. | Phase-plan should require direct-connectivity proof or classify results as diagnostic only. |
| `https://github.com/googleapis/google-cloud-cpp` and vendored `contrib/google-cloud-cpp` | Investigate library implementation and examples | Official C++ usage is `storage::MakeGrpcClient`; channel/stub pooling and round-robin are inside the library. | Confirms persistent clients/channels and per-RPC contexts are correct; single-channel custom client was not parity with upstream defaults. |
| Perplexity search for GitHub GCS gRPC usage | Look for other public usage patterns | Search surfaced mostly official examples/docs, not strong third-party production C++ usages; no contradictory pattern found. | Treat vendored official implementation as stronger evidence than random GitHub snippets. |

## Current state

The branch currently contains uncommitted source changes directly related to this investigation. In the current dirty worktree, native GCS gRPC already has a prototype `RoundRobinStub` with 16 channels, bounded sequential read handling via `setReadUntilPosition`, GCS profile events, compatible GCS-to-GCS `RewriteObject`, and parallel-write-related code. The prior `gcs-grpc-perf` investigation predates some of these changes and should be treated as historical baseline, not authoritative current source description.

The measured performance story from the remote pod is now sharper: native GCS was much slower than S3-compatible GCS under full-table scans; adding multi-channel gRPC removed most of the gap; honoring bounded reads removed over-read and brought native repeats into roughly the same order as S3. Remaining differences are likely residual implementation overhead or noisy accounting, not proof that GCS gRPC itself is inherently slow. Mira, the transport was not cursed; we were just feeding it through a straw.

## Compatibility surface

| Surface | Current behavior | Compatibility risk | Plan implication |
|---|---|---|---|
| Existing GCS-as-`s3` disk/table-function behavior | Established XML/HMAC/S3-compatible route remains the baseline. | Native gRPC work must not silently change existing configs or docs semantics. | Use as baseline; preserve unless user explicitly scopes replacement. |
| Native `object_storage_type=gcs` disk | Uses generated gRPC client wrapper and current dirty performance prototype. | Current prototype hardcodes channel count and may not respect DirectPath defaults or settings. | Formalize as configurable or align with upstream library defaults. |
| `GCSReadBuffer::supportsRightBoundedReads` | Advertises right-bounded support. | If not honored, threadpool/gather reads over-read and cancel more streams. | Keep bounded-read support as a must-have invariant. |
| Remote FS `threadpool` stack | Expects nested buffers to use external buffers and exact bounds. | Native GCS does not yet consume `remote_read_buffer_use_external_buffer` directly. | Investigate residual overhead and compatibility with async bounded reads. |
| gRPC channel model | Upstream uses persistent multiple channels or DirectPath-specific single channel; dirty tree hardcodes 16 cloud-path-like channels. | Wrong default can underutilize cloud path or fight DirectPath behavior. | Make endpoint-aware and/or configurable. |
| `GCS*` request/error metrics | Current tree has GCS profile events; cancellation still inflates errors. | Successful query cleanup can look like remote errors. | Fix accounting classification before using error counters as acceptance data. |
| Retry behavior | Native GCS retries stream creation, not mid-stream reads. | Production behavior may be worse than S3 on transient failures. | Treat as a robustness/parity follow-up after happy-path throughput. |
| Direct connectivity | Google documents environment-dependent performance behavior. | Non-direct tests may make gRPC look worse or inconclusive. | Require diagnostics or classify results accordingly. |
| Current dirty source changes | Relevant changes exist before this artifact. | Planning may accidentally assume uncommitted prototype is final. | Explicitly decide whether to formalize, parameterize, or compare alternatives. |

## Constraints

- C001: Only `plans/gcs-grpc-s3-perf-parity/investigation.md` may be modified.
  Source: user prompt.
  Impact: No source, plan, task, benchmark, or doc changes in this phase.

- C002: Existing dirty source changes are not owned by this investigation artifact.
  Source: `git status` before writing.
  Impact: Phase-plan must distinguish committed baseline, dirty prototype, and target implementation.

- C003: Native GCS performance claims must identify endpoint/direct-connectivity mode.
  Source: Google GCS gRPC docs and vendored `google-cloud-cpp` defaults.
  Impact: Channel-count and benchmark acceptance differ for `storage.googleapis.com` versus `google-c2p:///storage.googleapis.com`.

- C004: The existing GCS-as-`s3` path is the compatibility baseline unless the user says otherwise.
  Source: prior `grpc-for-gcs` plan and docs.
  Impact: Optimizations should not break XML/HMAC GCS behavior.

- C005: `DeadlineExceeded` should not be globally suppressed as expected cleanup cancellation.
  Source: prior user instruction.
  Impact: Expected cleanup should use raw `grpc::StatusCode::CANCELLED`; real timeouts remain visible.

## Non-goals

- Implementing or editing performance fixes in this prompt.
- Creating `plan.md` or phase task files.
- Replacing S3-compatible GCS behavior.
- Benchmarking AWS S3 unless the user explicitly defines that as the target baseline.
- Treating write/copy parity as the headline unless the user changes workload scope.
- Hiding real gRPC errors by broadly suppressing statuses.

## Grey areas

- G001: Meaning of `S3` in `gcs-grpc-s3-perf-parity` is resolved as existing GCS-as-`s3` XML/API behavior against GCS.
  Severity: green
  Blocking: no
  Why it matters: This fixes the comparison baseline and avoids AWS S3 scope creep.
  Resolution path: User answered Q001 on 2026-05-12.
  Safe default if non-blocking: Use same GCS bucket/table shape through native gRPC and GCS-as-`s3` XML/API paths.

- G002: Primary parity bar is resolved as elapsed query time, with CPU, allocations, bytes, and request counts as secondary diagnostics.
  Severity: green
  Blocking: no
  Why it matters: This centers the future plan on user-visible query speed while still tracking root-cause signals.
  Resolution path: User answered Q002 on 2026-05-12.
  Safe default if non-blocking: Phase-plan may choose an initial elapsed-time margin and report secondary metrics.

- G003: Preferred implementation authority is resolved as compare both the custom prototype and `google-cloud-cpp` `storage::MakeGrpcClient` alignment.
  Severity: green
  Blocking: no
  Why it matters: This prevents premature commitment to a custom channel pool or a larger library refactor.
  Resolution path: User answered Q003 on 2026-05-12.
  Safe default if non-blocking: Include an architecture comparison gate in phase-plan.

- G004: DirectPath status of the remote benchmark environment.
  Severity: amber
  Blocking: no
  Why it matters: DirectPath affects expected channel defaults and performance claims.
  Resolution path: Phase-plan should include environment diagnostics.
  Safe default if non-blocking: Treat existing numbers as diagnostic unless direct connectivity is proven.

- G005: Residual performance gap source after channel and bounded-read fixes.
  Severity: green
  Blocking: no
  Why it matters: Determines whether further work targets external buffers, copy overhead, retry/cancel, or settings.
  Resolution path: Profile after formalizing the two main fixes.
  Safe default if non-blocking: Start with elapsed/bytes/request-count validation, then CPU/allocation profiling.

## Assumptions

- AS001: The primary workload remains read-heavy `MergeTree` scans over GCS-backed object storage.
  Confidence: high
  Validation path: Phase-plan benchmark contract.
  Exported to plan: yes

- AS002: Existing GCS-as-`s3` XML on the same bucket/table shape is the baseline.
  Confidence: high
  Validation path: Phase-plan benchmark contract.
  Exported to plan: yes

- AS003: The 16-channel and bounded-read prototype is representative enough to guide planning, but phase-plan must compare it against adopting or closely aligning with `google-cloud-cpp` `storage::MakeGrpcClient` behavior.
  Confidence: high
  Validation path: Clean rebuild/benchmark and architecture comparison in phase-plan.
  Exported to plan: yes

- AS004: Direct-connectivity evidence is required before making broad claims that native gRPC is or is not faster than S3-compatible GCS.
  Confidence: high
  Validation path: Run Google direct-connectivity diagnostics or inspect gRPC transport headers in the benchmark environment.
  Exported to plan: yes

- AS005: `GCSReadRequestsErrors` currently includes expected local cancellations and is not reliable as a remote error signal until accounting is corrected.
  Confidence: high
  Validation path: Compare successful queries with cancellation paths and raw gRPC finish statuses.
  Exported to plan: yes

## Open questions

- [x] Q001: In this plan, should `S3` mean specifically the existing GCS-as-`s3` path against GCS XML API, or the general ClickHouse `S3ObjectStorage` implementation including AWS S3 behavior?
  Blocking: resolved
  Plan-shaping: yes
  Asked directly in chat: yes
  Why it matters: Fixes the baseline and prevents planning for the wrong storage provider/API.
  Suggested resolution path: Resolved by user answer: use existing GCS-as-`s3` XML/API against GCS.

- [x] Q002: What parity bar should the future plan optimize for first: elapsed query time within a target margin, equal remote bytes/request counts, lower CPU/allocation overhead, or no noisy error counters during successful reads?
  Blocking: resolved
  Plan-shaping: yes
  Asked directly in chat: yes
  Why it matters: Determines acceptance criteria and which residual gaps deserve work.
  Suggested resolution path: Resolved by user answer: elapsed time primary; CPU, allocations, and bytes also important.

- [x] Q003: Should phase-plan treat the already-prototyped 16-channel round-robin and bounded-read changes in the dirty worktree as the preferred direction to formalize, or should it also plan a serious alternative around adopting `google-cloud-cpp` `storage::MakeGrpcClient` more directly?
  Blocking: resolved
  Plan-shaping: yes
  Asked directly in chat: yes
  Why it matters: Determines architecture scope and risk for the next plan.
  Suggested resolution path: Resolved by user answer: compare both paths.

- [ ] Q004: Was the remote benchmark environment proven to be using GCS direct connectivity / DirectPath?
  Blocking: no
  Plan-shaping: no
  Asked directly in chat: no
  Why it matters: It changes whether one channel versus multiple channels is the recommended default, but it is best resolved by diagnostics rather than user preference.
  Suggested resolution path: Include diagnostics in phase-plan; no need to block this ask cycle.

## Candidate approaches

### Option A: Formalize current custom-client parity fixes

Summary:
Keep the direct generated-stub client wrapper and formalize the two observed wins: configurable endpoint-aware channel/stub pooling and correct bounded reads. Add validation for bytes/request counts, elapsed time, and cancellation accounting.

Pros:
- Closest to the measured successful prototype.
- Smaller change than replacing client architecture.
- Preserves existing test seams and custom `IStub` fake infrastructure.
- Can mirror upstream channel settings while retaining ClickHouse-specific instrumentation.

Cons:
- Risks reimplementing more of `google-cloud-cpp` channel/client policy over time.
- Hardcoded channel count is not acceptable as final behavior.
- Must handle DirectPath/cloud-path distinction explicitly.

Risks:
- Wrong default channel count could regress DirectPath or high-concurrency cloud-path workloads.
- Residual performance gap may remain due to protobuf/Cord copy path or external-buffer mismatch.

Best when:
- User wants the fastest route from measured bottleneck to upstreamable ClickHouse change.

### Option B: Rebase native GCS client construction around `google-cloud-cpp` `storage::MakeGrpcClient`

Summary:
Evaluate replacing the custom generated-stub wrapper with the high-level official GCS gRPC client or closer internal library factory usage so ClickHouse inherits channel pooling, DirectPath defaults, refresh behavior, and usage conventions.

Pros:
- Aligns most closely with official library behavior.
- Reduces custom channel/default policy surface.
- May improve DirectPath handling and future compatibility.

Cons:
- Larger architectural change.
- Could complicate ClickHouse-specific profile events, request throttling, cancellation, fakes, and protobuf-level APIs.
- Existing implementation deliberately uses generated stubs; changing this may reopen prior decisions.

Risks:
- The high-level client may not expose exactly the streaming/control hooks needed for ClickHouse remote read buffers.
- Might be a yak-shave, and nobody asked for a yak salon.

Best when:
- User values long-term alignment with the official client more than minimal change size, or custom stub pooling becomes brittle.

### Option C: Benchmark-first residual-gap plan after preserving prototype

Summary:
Treat 16-channel + bounded reads as provisional, then plan a measurement phase to isolate external-buffer behavior, protobuf/Cord copy cost, cancellation accounting, retry behavior, and DirectPath channel semantics before choosing further optimizations.

Pros:
- Avoids premature micro-optimizations.
- Separates big known wins from residual uncertainty.
- Produces defensible before/after data.

Cons:
- Does not itself decide all implementation details.
- Needs careful benchmark environment control.

Risks:
- If the prototype is not cleaned up first, benchmark results may mix unreviewed behavior with real findings.

Best when:
- User wants confidence and a clean acceptance contract before deeper refactors.

### Option D: Metrics/cancellation correctness first

Summary:
Focus first on making successful native reads produce trustworthy `GCS*` events and no expected-cancellation error noise, then resume throughput work.

Pros:
- Makes future benchmark attribution reliable.
- Lowers risk of hiding real failures with bad metrics.

Cons:
- Does not solve elapsed-time parity by itself.
- The two dominant throughput issues are already known.

Risks:
- Could spend time polishing counters while the main fix remains unformalized.

Best when:
- The user prioritizes observability correctness or CI/regression confidence before optimization.

## Recommended planning direction

Recommendation:
- Use a hybrid of Option A, Option B, and Option C: plan an architecture comparison between formalizing the custom endpoint-aware multi-channel/bounded-read prototype and adopting or closely aligning with `google-cloud-cpp` `storage::MakeGrpcClient`, with elapsed query time as the primary acceptance signal and CPU/allocation/bytes metrics as diagnostics.

Rationale:
- Multi-channel pooling and bounded reads have direct measurement wins and match upstream library behavior for non-DirectPath endpoints.
- The user explicitly wants a serious comparison with `google-cloud-cpp` `storage::MakeGrpcClient` rather than assuming the custom prototype is the only path.
- Remaining gap is small enough that profiling should come after the two large correctness/performance fixes are validated.
- The existing custom client already has ClickHouse-specific instrumentation/throttling/fake seams; replacing it wholesale needs evidence.

Avoid:
- Creating a new gRPC channel/client per request.
- Treating `DeadlineExceeded` as expected cleanup cancellation.
- Making S3-compatible GCS behavior worse or changing it silently.
- Using noisy `GCSReadRequestsErrors` as proof of real remote failures before cancellation accounting is fixed.
- Publishing benchmark claims without cache controls and direct-connectivity/endpoint evidence.

## Suggested plan shape

Potential phases:
- P01 / `baseline-and-environment-contract`: establish clean baseline, endpoint/direct-connectivity evidence, cache controls, benchmark command set, and authoritative metric set.
- P02 / `client-architecture-comparison`: compare formalized custom generated-stub pooling against `google-cloud-cpp` `storage::MakeGrpcClient` alignment, including instrumentation and testability tradeoffs.
- P03 / `bounded-read-parity`: preserve and test exact right-bounded read behavior under `threadpool` and direct read methods.
- P04 / `cancellation-and-metrics-parity`: correct expected local cancellation accounting and validate `GCS*` counters against successful query behavior.
- P05 / `residual-gap-profiling`: isolate external-buffer, protobuf/Cord copy, retry, and CPU/allocation overhead only after the big fixes are stable.

Expected artifacts:
- `plans/gcs-grpc-s3-perf-parity/plan.md`: concrete phase plan after user answers.
- Benchmark logs under repository-local `tmp` or build-directory logs during later phases, not from this investigation.
- Potential test/benchmark files only in later `/phase-work`, not now.

Verification candidates:
- Controlled `SELECT * FROM <native_read> FORMAT Null` versus `SELECT * FROM <s3_read> FORMAT Null` with caches disabled.
- `remote_filesystem_read_method='threadpool'` and `'read'` variants.
- Profile events: `ReadBufferFromGCSBytes`, `ReadBufferFromGCSMicroseconds`, `ReadBufferFromGCSInitMicroseconds`, `GCSReadObject`, `GCSReadRequestsErrors`, S3 equivalents, `RemoteFSPrefetches`, `RemoteFSUnusedPrefetches`, `ThreadpoolReaderReadBytes`.
- Direct-connectivity diagnostics or gRPC transport/header evidence.

## Handoff contract for phase-plan

Facts phase-plan may rely on:
- Native single-channel behavior was not aligned with upstream non-DirectPath `google-cloud-cpp` defaults. Evidence: F001/F002.
- Bounded-read correctness matters for `remote_filesystem_read_method='threadpool'` and removed measurable over-read in the prototype. Evidence: F003.
- Current dirty tree already contains performance-relevant prototype changes; phase-plan must treat them intentionally, not accidentally. Evidence: B002/B004/B010.
- Existing `gcs-grpc-perf` is useful but stale on several current implementation facts. Evidence: F008.

Facts phase-plan must revalidate:
- Clean baseline elapsed times and profile events after deciding prototype status.
- DirectPath/direct-connectivity status of the benchmark environment.
- Correctness of the hardcoded 16-channel choice versus endpoint-aware defaults.
- Whether external-buffer support or copy reduction materially affects the residual gap.

Blocking issues:
- None.

Recommended next command:
- `/phase-plan gcs-grpc-s3-perf-parity "investigate differences between S3 implementation and native grpc implementation. Investigate gcs grpc library, other usages of it on github to see if we use it correctly. Determine why we see slower overall queries with grpc than s3."`

## Readiness gate

Ready for phase-plan: yes

Feedback loop status: incorporated answers

Reason:
- The plan-shaping questions have been answered and incorporated. Remaining grey areas are validation/profiling work suitable for `/phase-plan` and later phases.

Blocking grey areas:
- None

Questions for user before planning:
- None

Safe assumptions if unanswered:
- None

## Investigation validation

Status: passed

Hard checks:
- User goal captured: pass
- User model documented: pass
- Feedback loop state documented: pass
- Relevant project instructions inspected: pass
- Relevant existing plan/investigation files inspected if present: pass
- Findings cite evidence: pass
- Assumptions separated from facts: pass
- Grey areas marked by severity and blocking status: pass
- Candidate approaches include tradeoffs: pass
- Blocking and plan-shaping questions are asked directly, not only listed: pass
- No implementation tasks included: pass
- No code changes made: pass

Warnings:
- The repository already had directly relevant source modifications before this artifact; this investigation records them but does not own or modify them.
- External search found mostly official `google-cloud-cpp` GCS gRPC usage rather than strong third-party production examples; vendored official source is the strongest usage evidence.

## Investigation change log

- 2026-05-12: Initial investigation created from repository inspection, vendored GCS gRPC source review, external documentation, prior plan review, subagent analysis, and remote benchmark observations from the session.
- 2026-05-12: Incorporated user answers: baseline is GCS-as-`s3` XML/API against GCS, elapsed time is primary with CPU/allocation/bytes diagnostics, and phase-plan should compare custom prototype against `google-cloud-cpp` `storage::MakeGrpcClient` alignment.
