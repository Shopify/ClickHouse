# GCS gRPC S3 performance parity plan

Plan status: ready

Plan slug: gcs-grpc-s3-perf-parity

## User goal

Investigate differences between the existing GCS-as-`s3` implementation path and the native GCS gRPC implementation, inspect the GCS gRPC library and usage patterns, verify whether ClickHouse uses the library correctly, and determine why overall read and write workloads are slower or behave differently with native gRPC than with the S3-compatible GCS path.

## Investigation baseline

Investigation file: [investigation.md](./investigation.md)

Investigation status: ready

Imported findings:
- F001: Native GCS slowdown was dominated by insufficient gRPC channel parallelism.
- F002: Upstream `google-cloud-cpp` uses multiple channels by default for ordinary cloud-path GCS gRPC and one channel for DirectPath endpoints.
- F003: Native GCS claimed right-bounded reads but did not honor the upper bound before the prototype.
- F004: With multi-channel plus bounded reads, native GCS repeat scans were close to S3-compatible GCS repeats, with a smaller residual gap.
- F005: Native GCS still differs from S3 in nested remote-read external-buffer integration.
- F006: Expected local cleanup cancellation can still inflate `GCSReadRequestsErrors` before higher-level status classification.
- F007: S3 has more mature body-read retry behavior than the current native GCS stream path.
- F008: The older `gcs-grpc-perf` investigation is useful but stale for current source state.

Imported constraints:
- C001: Superseded by this prompt. The investigation-only constraint is replaced by the current plan-only constraint: modify only `plans/gcs-grpc-s3-perf-parity/plan.md`.
- C002: Existing dirty source changes are not owned by this plan.
- C003: Native GCS performance claims must identify endpoint and direct-connectivity mode.
- C004: Existing GCS-as-`s3` behavior is the compatibility baseline.
- C005: `DeadlineExceeded` must not be globally suppressed as expected cleanup cancellation.

Imported assumptions or grey areas:
- AS001: Primary workload is read-heavy `MergeTree` scans over GCS-backed object storage.
- AS002: Existing GCS-as-`s3` XML/API on the same bucket/table shape is the baseline.
- AS003: The 16-channel plus bounded-read prototype is evidence-bearing but must be compared with `google-cloud-cpp` `storage::MakeGrpcClient` alignment.
- AS004: Direct-connectivity evidence is required before making broad GCS gRPC performance claims.
- AS005: `GCSReadRequestsErrors` is not reliable as a remote error signal until expected local cancellation accounting is corrected.
- G001-G003: Resolved by user answers; baseline is GCS-as-`s3`, elapsed time is primary, and both architecture paths must be compared.
- G004: DirectPath status remains non-blocking and must be validated in the benchmark phase.
- G005: Residual performance source remains non-blocking and must be profiled after the large fixes are validated.

Imported blockers:
- None.

Planning response:
- This plan uses the ready investigation as the evidence baseline and does not reinvestigate from scratch. It orders phases so benchmark/environment contract comes first, architecture comparison happens before committing to client implementation, bounded-read correctness is preserved as an independent reviewable increment, channel/client behavior is made production-ready after the architecture decision, cancellation metrics are corrected before final attribution, and residual elapsed-time closure only targets measured remaining gaps. Vamos, no vamos a pelear con fantasmas.

## Problem statement

Native GCS gRPC read and write workloads differ from the GCS-as-`s3` baseline because ClickHouse's native path currently differs in gRPC client/channel policy, right-bounded read handling, remote filesystem buffer integration, write/upload request behavior, cancellation accounting, retry behavior, and protobuf/Cord copy overhead. The plan must turn the investigation into a controlled sequence that proves the baseline, chooses the correct client architecture, implements the known correctness/performance fixes, and verifies read/write parity without breaking the existing S3-compatible GCS path.

## Non-goals

- Do not replace or remove existing GCS-as-`s3` XML/API compatibility.
- Do not benchmark AWS S3 as the primary baseline.
- Do not make the `gcs` table function use native gRPC in this plan.
- Do not treat copy or backup parity as primary outcomes except where source interfaces constrain read/write performance work. The staging `/work/gcs-grpc-testing/run_benchmarks.sh` copy results are recorded as important context, but copy does not become primary scope unless the user explicitly changes scope again.
- Do not hide real gRPC errors by broad status suppression.
- Do not create a new gRPC channel/client per request.
- Do not ship a hardcoded final channel count such as `16` without setting, endpoint-aware default, or documented equivalence to upstream behavior.

## Constraints

- Only this plan file may be created or updated by this prompt.
- Existing dirty changes in `src/IO/GCS/GCSClient.cpp`, `src/Disks/DiskObjectStorage/ObjectStorages/GCS/GCSObjectStorage.cpp`, `contrib/liburing`, and `contrib/sysroot` must be left untouched unless later phase work explicitly owns them.
- GCS-as-`s3` XML/API behavior against GCS is the benchmark and compatibility baseline.
- Elapsed workload time for read and write is the primary parity metric; CPU, allocations, bytes, request counts, error counters, and per-run warm-up trends are secondary diagnostics.
- The client architecture phase must compare formalizing the custom generated-stub/channel-pool prototype against a real, separate-branch test of the higher-level `google-cloud-cpp` `storage::MakeGrpcClient` path.
- Native GCS performance results must record endpoint/direct-connectivity status before being used as acceptance evidence; for staging, `/work/gcs-grpc-testing/env.sh` was updated to use the `google-c2p://` endpoint and native GCS reads/writes are confirmed working.
- Expected cleanup cancellation may treat raw `grpc::StatusCode::CANCELLED` as expected; `DeadlineExceeded` remains a real timeout/error signal.
- Future build and test commands must redirect output to log files in the build directory and use a subagent to summarize logs, per repository instructions.
- Staging validation should use `kubectl exec -n ch-builder clickhouse-builder-0 -- <command>` against `/work/ch-dev`, which is a copy of this branch. Changes can reach staging by commit/push/pull or by `kubectl cp` of specific files into `/work/ch-dev`.
- Staging builds should run `ninja` from `/work/ch-dev/build`, with output redirected to a build log.
- If staging validation needs a server restart, use or create `/work/gcs-grpc-testing/start_server.sh`; it must source `/work/gcs-grpc-testing/env.sh`, kill an already-running `clickhouse-server`, start `/work/ch-dev/build/programs/clickhouse-server` in the background, and use a config with `query_log` enabled.
- The staging benchmark tables `p06_gcs_20260512T174206Z.native_read` and `p06_gcs_20260512T174206Z.s3_read` already exist with data and are configured for native GCS gRPC and GCS-as-`s3` respectively.

## Assumptions

- The primary workload includes full or wide `MergeTree` scans over GCS-backed object storage and bulk write workloads exercised by `/work/gcs-grpc-testing/run_benchmarks.sh`. Confidence: high. Validate in P01/P06 by recording benchmark tables, queries, settings, and per-run results.
- The remote benchmark tables from the investigation, `p06_gcs_20260512T174206Z.native_read` and `p06_gcs_20260512T174206Z.s3_read`, remain available or can be recreated equivalently. Confidence: medium. Validate in P01.
- A default elapsed-time parity target of native median elapsed time no worse than `1.25x` the GCS-as-`s3` median is a useful initial bar, with P01 allowed to record a stricter or looser threshold if the benchmark environment justifies it. Confidence: medium. Validate in P01 and P06.
- The current multi-channel and bounded-read prototype is evidence-bearing but not production-ready because its channel count and architecture choice are not finalized. Confidence: high. Validate in P02-P04.
- DirectPath is available and working in staging: `/work/gcs-grpc-testing/env.sh` uses the `google-c2p://` endpoint, and native GCS reads/writes work. Confidence: high for staging; future phases should preserve this evidence in review artifacts and apply DirectPath-aware channel policy in P04.
- Native read and copy timings in `/work/results/20260513T000127Z` speed up across iterations (`read` native `1.78s` to `0.89s`; `copy` native `3.31s` to `2.22s`), so later verification must report per-run sequences and medians rather than only one run. Confidence: high for that benchmark result.

## Open questions

None. Investigation Q001-Q003 were asked directly and resolved. Remaining uncertainties are non-blocking and assigned to phases with validation paths.

## Acceptance criteria

- [ ] A001: P01 records a reproducible benchmark/environment contract for native GCS gRPC versus GCS-as-`s3`, including endpoint/direct-connectivity status, benchmark queries, settings, primary elapsed-time target, secondary metrics, and post-P01 read/write/copy staging benchmark context.
- [ ] A002: P02 records an architecture decision comparing the custom generated-stub/channel-pool path against a separate-branch higher-level `google-cloud-cpp` `storage::MakeGrpcClient` test, including read and write API fit, instrumentation, cancellation, settings, performance evidence, and testability tradeoffs.
- [ ] A003: P03 validates native GCS right-bounded reads under `remote_filesystem_read_method='threadpool'` and direct read mode so `ReadBufferFromGCSBytes` no longer materially exceeds `ReadCompressedBytes` or the S3-compatible baseline for the target scan.
- [ ] A004: P04 delivers production-ready native GCS client/channel and write-path behavior with persistent clients/channels, fresh per-RPC contexts, endpoint-aware or configurable channel count, no hardcoded final `16`-channel policy, and no obvious write serialization bottleneck left unmeasured.
- [ ] A005: P05 ensures successful native GCS reads do not count expected local cleanup `CANCELLED` statuses as remote read errors, while real `DeadlineExceeded` and other unexpected statuses remain visible.
- [ ] A006: P06 final benchmark shows native GCS read and write median elapsed times meet the P01/P06 parity threshold against GCS-as-`s3`, or records measured residual bottlenecks with CPU/allocation/bytes/request evidence and bounded follow-up recommendations.
- [ ] A007: Across P03-P06, existing GCS-as-`s3` behavior remains unchanged except for benchmark/control observations.
- [ ] A008: P06 records per-run trends, including native read/copy warm-up behavior and write variance, so parity decisions are based on repeated runs rather than a single lucky or unlucky execution.

## Relevant context

- `plans/gcs-grpc-s3-perf-parity/investigation.md`: Ready evidence baseline; imports findings F001-F008, resolved user answers, constraints, assumptions, and recommended phase shape.
- `AGENTS.md`: Repository rules for commits, docs wording, C++ style, build/test logging, and use of `tmp`.
- `README.md`: General ClickHouse project context; no GCS-specific planning constraints found.
- Staging DirectPath/direct connectivity: `/work/gcs-grpc-testing/env.sh` was updated to use the `google-c2p://` endpoint, and native GCS reads/writes are confirmed working.
- Staging test environment: use `kubectl exec -n ch-builder clickhouse-builder-0 -- <command>`; remote source tree is `/work/ch-dev`; remote build directory is `/work/ch-dev/build`; staging file sync can use commit/push/pull or `kubectl cp` for specific files.
- Staging benchmark run: `/work/gcs-grpc-testing/run_benchmarks.sh` wrote results to `/work/results/20260513T000127Z`; medians from five runs were read native `1.38s` vs S3 `1.18s` (`1.17x`), write native `79.25s` vs S3 `10.80s` (`7.34x` slower), and copy native `2.66s` vs S3 `4.12s` (`0.65x`, native faster). Native read and copy speed up across iterations (`read`: `1.78`, `1.58`, `1.38`, `1.15`, `0.89`; `copy`: `3.31`, `3.18`, `2.66`, `2.51`, `2.22`), so later benchmark reviews must report per-run trends. Read results meet the provisional `1.25x` threshold in that run; write parity is now in scope; copy remains context unless scope changes.
- Staging server helper: use or create `/work/gcs-grpc-testing/start_server.sh`; it should source `/work/gcs-grpc-testing/env.sh`, kill an existing `clickhouse-server`, start `/work/ch-dev/build/programs/clickhouse-server` in the background, and use a config that enables `query_log`.
- Staging benchmark tables: `p06_gcs_20260512T174206Z.native_read` uses native GCS gRPC, and `p06_gcs_20260512T174206Z.s3_read` uses GCS-as-`s3`; both already exist with data.
- `src/IO/GCS/GCSClient.cpp`: Native GCS gRPC client, operation accounting, stream creation, and current dirty round-robin prototype surface.
- `src/IO/GCS/GCSClient.h`: Native GCS client interface and future settings surface if channel/client behavior needs exposure.
- `src/IO/GCS/GCSStatus.cpp`: gRPC-to-ClickHouse status mapping and retryability policy.
- `src/Disks/DiskObjectStorage/ObjectStorages/GCS/GCSObjectStorage.cpp`: Native GCS object-storage read buffer, bounded-read behavior, and cleanup cancellation handling.
- `src/Disks/DiskObjectStorage/ObjectStorages/S3/S3ObjectStorage.cpp`: GCS-as-`s3` baseline read-buffer construction and external-buffer behavior.
- `src/IO/ReadBufferFromS3.cpp`: S3-compatible range read, retry, and external-buffer behavior used as comparison.
- `src/Disks/DiskObjectStorage/DiskObjectStorage.cpp`: Remote filesystem wrapper stack and nested-buffer contract.
- `src/Disks/IO/ReadBufferFromRemoteFSGather.cpp`: Propagation of read bounds into object-storage buffers.
- `src/IO/ReadSettings.cpp`: Nested read setting propagation, including `remote_read_buffer_use_external_buffer`.
- `src/Common/ProfileEvents.cpp`: GCS and S3 profile-event families used for benchmark attribution.
- `contrib/google-cloud-cpp/google/cloud/storage/internal/grpc/default_options.cc`: Upstream GCS gRPC default channel-count and DirectPath behavior.
- `contrib/google-cloud-cpp/google/cloud/storage/internal/storage_stub_factory.cc`: Upstream channel/stub construction and round-robin wiring.
- `contrib/google-cloud-cpp/google/cloud/storage/internal/storage_round_robin_decorator.cc`: Upstream round-robin dispatch behavior.
- `contrib/google-cloud-cpp/google/cloud/grpc_options.h`: `GrpcNumChannelsOption` and channel concurrency semantics.

## Decisions

- D001: Treat GCS-as-`s3` XML/API against GCS as the baseline, not AWS S3.
  Rationale: The user selected this during investigation and the observed benchmark tables compare native GCS with a GCS-backed S3-compatible path.
  Alternatives considered: Generic `S3ObjectStorage`; AWS S3. Both rejected as primary scope.
  Reversible: yes
  Affects phases: P01, P06

- D002: Use elapsed query time as the primary metric and CPU, allocation, bytes, request counts, and error counters as diagnostics.
  Rationale: The user selected elapsed time as the primary parity bar while explicitly noting CPU, allocations, and bytes are also important.
  Alternatives considered: Bytes/request parity first; clean metrics first. Both remain supporting acceptance criteria.
  Reversible: yes
  Affects phases: P01, P03, P05, P06

- D003: Compare both client architecture paths before finalizing channel/client behavior.
  Rationale: The user selected a serious comparison between formalizing the current prototype and adopting or aligning with `google-cloud-cpp` `storage::MakeGrpcClient`.
  Alternatives considered: Immediately formalize prototype; immediately switch to `MakeGrpcClient`. Both are premature.
  Reversible: yes
  Affects phases: P02, P04

- D004: Require endpoint/direct-connectivity evidence for benchmark interpretation and channel-count policy.
  Rationale: Upstream `google-cloud-cpp` defaults differ between cloud-path and DirectPath endpoints, and GCS gRPC performance claims depend on environment.
  Alternatives considered: Use one channel-count default for all environments. Rejected as likely wrong.
  Reversible: no
  Affects phases: P01, P02, P04, P06

- D005: Preserve raw `grpc::StatusCode::CANCELLED` as expected cleanup cancellation but do not suppress `DeadlineExceeded`.
  Rationale: Prior user instruction and investigation evidence distinguish local `TryCancel` cleanup from real timeouts.
  Alternatives considered: Suppress both statuses. Rejected because it would hide real timeouts.
  Reversible: yes
  Affects phases: P05

- D006: Separate bounded-read correctness from client/channel architecture.
  Rationale: Bounded reads are independently correct and explain measurable over-read regardless of final client architecture.
  Alternatives considered: Bundle all performance fixes into one phase. Rejected because it would make review and attribution muddy, like paella with ketchup.
  Reversible: yes
  Affects phases: P03, P04

- D007: Include native GCS write parity in this plan.
  Rationale: Staging results show native write median `79.25s` versus S3 `10.80s`, and the user wants write parity included because it may suffer from similar client/channel or request-path issues as reads.
  Alternatives considered: Track write parity as a separate follow-up plan. Rejected by updated user scope.
  Reversible: yes
  Affects phases: P02, P04, P06

- D008: Report per-run trends as well as medians.
  Rationale: Staging native read and copy executions speed up across iterations, so a median alone can hide warm-up/cache/channel effects that matter for interpretation.
  Alternatives considered: Median-only benchmark reporting. Rejected as too lossy for this workload.
  Reversible: yes
  Affects phases: P01, P06

## Verification ladder

Use the lowest sufficient verification tier for each phase:

- Tier 0 smoke: format, lint, typecheck, targeted unit tests, static checks.
- Tier 1 core: relevant unit or integration test suite.
- Tier 2 behavioral: end-to-end test, migration test, benchmark, scenario, or regression reproduction.
- Tier 3 manual: manual inspection or human review when no automated check exists.

Each phase names its intended tier and why that tier is sufficient. Because the target issue is query performance and remote object-storage behavior, P01, P03, P04, P05, and P06 use Tier 2 behavioral verification. P02 uses Tier 3 because it is an architecture decision phase; no automated check can prove the decision by itself, but it must consume P01 measurements and source evidence.

## Phase overview

| Phase | Slug | Goal | Dependencies | Expected artifacts | Verification tier | Verification |
|---|---|---|---|---|---|---|
| P01 | 01-benchmark-and-environment-contract | Establish reproducible baseline, endpoint mode, metrics, parity threshold, and staging read/write/copy context. | none | `tmp/gcs-grpc-s3-perf-parity/p01-*`, phase review | Tier 2 | Controlled native vs GCS-as-`s3` benchmark with profile events and endpoint/direct-connectivity evidence. |
| P02 | 02-client-architecture-comparison | Choose between formalized custom pooling and separate-branch `google-cloud-cpp` `storage::MakeGrpcClient` testing for read and write paths. | P01 | Architecture decision in notes/review; separate branch evidence for `MakeGrpcClient`; optional throwaway prototype notes | Tier 3 | Manual source/design review grounded in P01 data, staging write evidence, vendored library behavior, and the separate-branch `MakeGrpcClient` test. |
| P03 | 03-bounded-read-parity | Make native GCS right-bounded reads correct and verifiable. | P01 | `src/Disks/DiskObjectStorage/ObjectStorages/GCS/GCSObjectStorage.cpp`, targeted tests or benchmark evidence, phase review | Tier 2 | Threadpool/direct-read scenario showing no material GCS over-read. |
| P04 | 04-client-channel-parity | Implement the chosen production-ready client/channel architecture and address measured write-path bottlenecks. | P01, P02, P03 | `src/IO/GCS/GCSClient.cpp`, `src/IO/GCS/GCSClient.h` if needed, GCS object-storage write-path changes if measured, tests/bench evidence, phase review | Tier 2 | Build plus native vs S3-compatible read/write benchmark showing channel/write bottlenecks addressed. |
| P05 | 05-cancellation-metrics-parity | Correct expected cancellation accounting and preserve real error visibility. | P03, P04 | `src/IO/GCS/GCSClient.cpp`, `src/Disks/DiskObjectStorage/ObjectStorages/GCS/GCSObjectStorage.cpp` if needed, metrics evidence, phase review | Tier 2 | Successful query cleanup has no expected-cancellation read-error noise; real timeout statuses remain classified. |
| P06 | 06-residual-performance-closure | Close or explain remaining read/write elapsed-time gaps with CPU/allocation/bytes/request evidence. | P05 | Final benchmark/profiling report, targeted source changes only if measured, phase review | Tier 2 | Final controlled read/write benchmark against P01/P06 thresholds and secondary diagnostics. |

## Phases

### P01: Benchmark and environment contract

Slug: `01-benchmark-and-environment-contract`

Goal:
Establish the reproducible GCS-as-`s3` versus native GCS gRPC benchmark contract, including endpoint/direct-connectivity status, cache/settings controls, query set, tables, primary elapsed-time threshold, secondary attribution metrics, and post-P01 read/write/copy staging context.

Scope:
- Record exact native and GCS-as-`s3` tables or recreate-equivalent criteria.
- Record server/client settings, especially `remote_filesystem_read_method`, cache state, prefetch behavior, and profile-event collection.
- Record endpoint and direct-connectivity evidence for the native GCS gRPC path.
- Establish the elapsed-time parity threshold, defaulting to native median `<= 1.25x` GCS-as-`s3` median unless this phase records a justified adjustment.
- Preserve post-P01 staging benchmark context from `/work/results/20260513T000127Z`, including read/write medians and native read/copy warm-up trends.

Out of scope:
- Production source changes.
- Choosing or implementing the final client architecture.
- Residual CPU/copy optimization beyond baseline measurement.

Dependencies:
- none

Phase interface:

Inputs:
- Investigation findings F001-F008.
- Existing dirty worktree status and remote benchmark context.
- Available benchmark tables such as `p06_gcs_20260512T174206Z.native_read` and `p06_gcs_20260512T174206Z.s3_read`, or equivalent recreated tables.

Outputs:
- Benchmark/environment contract with queries, settings, endpoint/direct-connectivity status, metrics, and threshold.
- Baseline benchmark logs under repository-local `tmp` or build log directories.
- Post-P01 staging benchmark summary for read, write, and copy context.

Downstream contract:
- Later phases may use the P01 query set, settings, and threshold as the authoritative benchmark contract.
- Later phases may treat direct-connectivity status as known only for the recorded environment.

Assumptions exported:
- GCS-as-`s3` XML/API against GCS is the control path.
- Elapsed time is primary for read and write; bytes/request/error/CPU/allocation signals and per-run trends are diagnostics.

Assumptions not exported:
- Any single run is representative; later phases must use repeated medians or P01's recorded method.

Expected artifacts:
- `tmp/gcs-grpc-s3-perf-parity/p01-benchmark-and-environment-contract/`: benchmark commands, logs, profile-event captures, and endpoint/direct-connectivity evidence.
- `plans/gcs-grpc-s3-perf-parity/01-benchmark-and-environment-contract-review.md`: phase review produced during phase completion.

Verification approach:
- Tier: Tier 2
- Method: Run the agreed native and GCS-as-`s3` `SELECT * ... FORMAT Null` scenarios with `--print-profile-events --time`, including `remote_filesystem_read_method='threadpool'` and `'read'` where relevant. Record endpoint/direct-connectivity evidence or mark results diagnostic-only. Preserve later `/work/gcs-grpc-testing/run_benchmarks.sh` read/write/copy context when available.
- Sufficiency: The performance problem is behavioral and environment-sensitive; a controlled benchmark contract is the lowest tier that proves later comparisons are meaningful.

Completion criteria:
- Benchmark commands, settings, tables, logs, profile-event fields, and endpoint/direct-connectivity status are recorded.
- Primary elapsed-time threshold and secondary metric list are recorded.
- Existing dirty source changes are explicitly classified as baseline, prototype, or out-of-scope for subsequent phases.

Risks and rollback:
- Risk: Existing remote tables are unavailable. Mitigation: Recreate equivalent table shape and record the delta. Rollback: Mark old table-specific numbers as historical only.
- Risk: Direct-connectivity status cannot be proven. Mitigation: Treat results as diagnostic and avoid broad GCS gRPC claims. Rollback: None; this is a classification outcome.

Task decomposition guidance:
- Create tasks around measurement contract and evidence capture, not source edits.

### P02: Client architecture comparison

Slug: `02-client-architecture-comparison`

Goal:
Choose the production architecture for native GCS gRPC client/channel behavior by comparing formalized custom generated-stub pooling against a separate-branch test of the higher-level `google-cloud-cpp` `storage::MakeGrpcClient`, for both read and write paths.

Scope:
- Compare persistent channel/client behavior, per-RPC context lifecycle, channel count defaults, DirectPath semantics, settings exposure, fake/test seams, profile-event accounting, throttling, cancellation handling, read streaming API fit, and write/upload API fit.
- Create or use a separate branch for the `storage::MakeGrpcClient` experiment; do not mix this exploratory implementation with the main custom-stub production path until P02 records a decision.
- Test whether the higher-level `MakeGrpcClient` path can support ClickHouse's native GCS read/write needs, including direct connectivity through `google-c2p://`, profile events, cancellation, throttling, and fakes.
- Decide whether P04 should formalize the custom `IStub`/round-robin path, wrap more upstream `google-cloud-cpp` internals, or move toward high-level `storage::MakeGrpcClient`.
- Record rejected alternatives and why.

Out of scope:
- Production implementation of the chosen architecture.
- Merging the `MakeGrpcClient` experiment branch before the P02 decision and review.
- Benchmarking residual CPU/copy overhead.
- Changing the GCS-as-`s3` baseline.

Dependencies:
- P01

Phase interface:

Inputs:
- P01 benchmark/environment contract.
- Investigation source evidence for `GCSClient`, upstream `google-cloud-cpp` defaults, and current prototype.

Outputs:
- Architecture decision specifying the P04 implementation direction and acceptance requirements for both read and write paths.
- Separate-branch `MakeGrpcClient` experiment evidence, including branch name, diff summary, build status if attempted, and read/write benchmark or blocker results.
- Comparison matrix covering correctness, read performance, write performance, compatibility, and testability.

Downstream contract:
- P04 may implement the selected architecture without reopening the high-level custom-versus-library decision unless new blocking evidence appears.

Assumptions exported:
- Final architecture must preserve persistent channels/clients and fresh per-RPC contexts.
- Final architecture must be endpoint-aware or configurable and must not hardcode the prototype `16` channel count.

Assumptions not exported:
- That upstream `MakeGrpcClient` is automatically better; P02 must prove or reject it for ClickHouse's streaming, write, and instrumentation needs using the separate branch.

Expected artifacts:
- `plans/gcs-grpc-s3-perf-parity/02-client-architecture-comparison-review.md`: architecture decision and comparison summary.
- Separate branch for `storage::MakeGrpcClient` testing, named in the P02 notes/review.
- Optional `tmp/gcs-grpc-s3-perf-parity/p02-client-architecture-comparison/`: throwaway prototype notes, benchmark summaries, branch diff summaries, or blocker logs needed for the decision.

Verification approach:
- Tier: Tier 3
- Method: Manual architecture review against P01 results, staging read/write evidence, vendored `google-cloud-cpp` source, and the separate-branch `MakeGrpcClient` experiment, because the output is a decision rather than executable production behavior.
- Sufficiency: No automated test can validate the architecture decision before implementation; the review must explicitly map each acceptance criterion and risk to the selected P04 path and explain why the separate-branch `MakeGrpcClient` test was accepted or rejected.

Completion criteria:
- Decision records the chosen architecture and rejected alternatives.
- Decision explains DirectPath/non-DirectPath channel policy.
- Decision records the `MakeGrpcClient` experiment branch, evidence, and outcome.
- Decision explains how ClickHouse profile events, throttling, fakes, cancellation handling, and write/upload behavior remain possible.

Risks and rollback:
- Risk: `MakeGrpcClient` lacks required stream/control hooks. Mitigation: test it on a separate branch and select custom pooling or closer internal alignment if hooks are missing. Rollback: abandon the experiment branch and keep current generated-stub interface for P04.
- Risk: Custom pooling duplicates too much upstream behavior. Mitigation: narrow custom code to the minimum required for ClickHouse instrumentation and compare against the separate-branch `MakeGrpcClient` evidence. Rollback: reopen P02 only with specific evidence.

Task decomposition guidance:
- Create tasks around decision evidence, separate-branch `MakeGrpcClient` testing, branch diff/benchmark capture, and tradeoff recording, not broad production implementation.

### P03: Bounded read parity

Slug: `03-bounded-read-parity`

Goal:
Make native GCS right-bounded reads correct and reviewable so threadpool/gather reads do not over-read compared with GCS-as-`s3`.

Scope:
- Preserve or implement `GCSReadBuffer` right-bound behavior equivalent to its advertised support.
- Ensure sequential stream limits honor `setReadUntilPosition` and `setReadUntilEnd` semantics without hiding short reads or EOF conditions.
- Validate behavior under `remote_filesystem_read_method='threadpool'` and direct read mode.

Out of scope:
- Final channel/client architecture selection or implementation.
- Expected cancellation metrics cleanup except where directly required by bounded-read correctness.
- External-buffer or zero-copy optimization unless required for correctness.

Dependencies:
- P01

Phase interface:

Inputs:
- P01 benchmark contract and byte/profile-event metric set.
- Investigation evidence F003 and B010.

Outputs:
- Correct native GCS bounded-read behavior.
- Test or benchmark evidence that GCS read bytes align with compressed bytes and S3-compatible baseline within P01 tolerance.

Downstream contract:
- P04-P06 may assume native GCS no longer over-reads because it ignored right bounds.

Assumptions exported:
- `supportsRightBoundedReads` is truthful for native GCS after this phase.

Assumptions not exported:
- That bounded reads alone close the elapsed-time gap; P04 and P06 must verify elapsed time separately.

Expected artifacts:
- `src/Disks/DiskObjectStorage/ObjectStorages/GCS/GCSObjectStorage.cpp`: bounded-read production changes if not already finalized.
- Targeted tests or benchmark evidence tied to bounded-read behavior.
- `plans/gcs-grpc-s3-perf-parity/03-bounded-read-parity-review.md`: phase review.

Verification approach:
- Tier: Tier 2
- Method: Build `clickhouse` with output redirected to a build log, run targeted read scenarios from P01 with profile events, and verify `ReadBufferFromGCSBytes` is not materially above `ReadCompressedBytes` or the S3-compatible control for the same scan.
- Sufficiency: The regression is behavioral and appeared through remote filesystem reads; profile-event byte parity under the benchmark scenario proves the intended outcome.

Completion criteria:
- Native GCS right-bounded read behavior is implemented or confirmed production-ready.
- Threadpool and direct read scenarios pass byte-parity checks.
- No GCS-as-`s3` path changes are required.

Risks and rollback:
- Risk: Bound handling causes premature EOF or short reads. Mitigation: Validate exact bytes and query correctness under both read methods. Rollback: Revert bounded-read changes and keep the phase blocked until EOF semantics are fixed.

Task decomposition guidance:
- Create tasks around bounded-read semantics, test coverage, and byte-profile verification.

### P04: Client channel parity

Slug: `04-client-channel-parity`

Goal:
Implement the P02-selected production-ready native GCS client/channel architecture so native reads are no longer bottlenecked by single-channel behavior and native writes no longer have an unmeasured multi-second parity gap against GCS-as-`s3`.

Scope:
- Implement endpoint-aware or configurable channel behavior according to the P02 decision.
- Preserve persistent clients/channels and fresh per-RPC `grpc::ClientContext` instances.
- Preserve ClickHouse-specific instrumentation and test/fake seams.
- Investigate and address measured native write-path bottlenecks that plausibly share client/channel, request concurrency, or streaming behavior with reads.
- Remove or replace any final hardcoded prototype channel count.

Out of scope:
- Bounded-read correctness already covered by P03.
- Expected cancellation metrics cleanup covered by P05.
- Residual external-buffer, copy, or allocation optimization unless needed to make the chosen read/write architecture work.

Dependencies:
- P01
- P02
- P03

Phase interface:

Inputs:
- P01 benchmark contract.
- P02 architecture decision.
- P03 bounded-read-correct native path.

Outputs:
- Production-ready native GCS client/channel behavior for read and write workloads.
- Evidence that the single-channel read bottleneck and the measured native write gap are addressed or explicitly attributed under the P01/P06 benchmark contract.

Downstream contract:
- P05 and P06 may assume final client/channel architecture is stable and reviewable.

Assumptions exported:
- Native GCS channel/client behavior is aligned with the selected architecture and endpoint policy.

Assumptions not exported:
- That residual elapsed gap is zero; P06 must profile remaining differences.

Expected artifacts:
- `src/IO/GCS/GCSClient.cpp`: client/channel implementation changes.
- `src/IO/GCS/GCSClient.h`: public/internal option surface if required by P02.
- GCS object-storage write-path files if P02/P04 evidence shows the bottleneck is above `GCSClient`.
- Targeted tests or benchmark evidence for read and write behavior.
- `plans/gcs-grpc-s3-perf-parity/04-client-channel-parity-review.md`: phase review.

Verification approach:
- Tier: Tier 2
- Method: Build `clickhouse` with output redirected to a build log, run P01/P06 native and GCS-as-`s3` read/write benchmarks, and compare elapsed time, per-run trend, request counts, and profile events before and after channel/write-path implementation.
- Sufficiency: The primary issue is workload-level throughput; benchmark behavior is required to prove the read channel bottleneck and write parity gap are addressed.

Completion criteria:
- No final hardcoded `16`-channel policy remains unless P02 explicitly justifies it through a setting/default contract.
- DirectPath and non-DirectPath endpoint behavior are addressed according to P02.
- Benchmark shows native GCS no longer has the prior single-channel-scale read slowdown and no longer has the measured `7.34x` native write slowdown without a documented residual bottleneck.

Risks and rollback:
- Risk: Final architecture regresses DirectPath behavior. Mitigation: Use P01 endpoint evidence and P02 policy; test both endpoint modes if available. Rollback: Restore previous single-architecture behavior and mark endpoint policy unresolved.
- Risk: Adopting upstream client APIs breaks instrumentation. Mitigation: Require P02 instrumentation mapping before implementation. Rollback: Revert to custom generated-stub path.

Task decomposition guidance:
- Create tasks from the P02 decision, with separate validation for endpoint policy, instrumentation, read benchmark behavior, and write benchmark behavior.

### P05: Cancellation metrics parity

Slug: `05-cancellation-metrics-parity`

Goal:
Ensure successful native GCS reads do not report expected local cleanup cancellation as remote GCS read errors, while real timeout and unexpected statuses remain visible.

Scope:
- Correct accounting around stream `Finish` so local `TryCancel` cleanup returning raw `grpc::StatusCode::CANCELLED` is classified as expected.
- Preserve error visibility for `DeadlineExceeded`, `Unavailable`, `ResourceExhausted`, and other unexpected statuses.
- Validate profile-event and log behavior on successful benchmark queries.

Out of scope:
- Broad retry-policy redesign.
- Suppressing errors outside the local cleanup path.
- Changing S3-compatible error accounting.

Dependencies:
- P03
- P04

Phase interface:

Inputs:
- Stable bounded-read and client/channel behavior from P03-P04.
- P01 profile-event metric contract.
- Investigation evidence F006 and C005.

Outputs:
- Correct native GCS cancellation/error accounting.
- Evidence that successful read cleanup does not inflate `GCSReadRequestsErrors` / `DiskGCSReadRequestsErrors` for expected local cancellation.

Downstream contract:
- P06 may use GCS error counters as meaningful residual diagnostics.

Assumptions exported:
- Expected local cancellation no longer pollutes successful-query error counters.

Assumptions not exported:
- That all retry or transient-failure parity with S3 is solved.

Expected artifacts:
- `src/IO/GCS/GCSClient.cpp`: accounting changes if classification belongs near `AccountingReader`.
- `src/Disks/DiskObjectStorage/ObjectStorages/GCS/GCSObjectStorage.cpp`: cancellation classification changes if cleanup intent must be propagated there.
- Targeted scenario evidence for successful cleanup and at least one unexpected-status classification path where practical.
- `plans/gcs-grpc-s3-perf-parity/05-cancellation-metrics-parity-review.md`: phase review.

Verification approach:
- Tier: Tier 2
- Method: Run successful native GCS read benchmark with profile events and server logs; verify expected local cleanup does not produce read-error counters/log noise. Use targeted status-path tests or controlled failure scenario where practical to confirm non-`CANCELLED` statuses remain visible.
- Sufficiency: The issue appears in query cleanup and profile events; behavioral query/log verification is the lowest sufficient proof.

Completion criteria:
- Successful native GCS scan does not report expected local cleanup cancellation as remote read errors.
- `DeadlineExceeded` is not treated as expected cleanup cancellation.
- Metrics are reliable enough for P06 residual diagnosis.

Risks and rollback:
- Risk: Accounting change hides real stream failures. Mitigation: Gate expected classification on explicit cleanup cancellation context and raw `grpc::StatusCode::CANCELLED`. Rollback: Revert classification change and keep noisy counters documented.

Task decomposition guidance:
- Create tasks around status classification boundaries and metric verification, not broad logging cleanup.

### P06: Residual read/write performance closure

Slug: `06-residual-performance-closure`

Goal:
Close the remaining native GCS read and write elapsed-time gaps against GCS-as-`s3`, or produce measured evidence identifying bounded residual bottlenecks and follow-up recommendations.

Scope:
- Re-run the P01 benchmark after P03-P05, including read and write workloads from `/work/gcs-grpc-testing/run_benchmarks.sh`.
- Attribute residual elapsed-time differences using CPU, allocation, bytes, request counts, profile events, query log entries, and logs.
- Report per-run trends as well as medians, including native read/copy warm-up behavior where copy remains context.
- Address only measured residual causes that are scoped to native GCS read/write parity, such as external-buffer integration, protobuf/Cord copy overhead, write request concurrency, upload stream behavior, or retry behavior if it affects benchmark stability.

Out of scope:
- Large unrelated refactors.
- Generic object-storage redesign.
- AWS S3 comparisons.
- Copy or backup parity as primary goals unless the user explicitly expands scope again.

Dependencies:
- P05

Phase interface:

Inputs:
- P01 benchmark contract, post-P01 staging benchmark evidence, and threshold.
- Stable bounded-read, client/channel, write-path, and cancellation-accounting behavior.

Outputs:
- Final benchmark report and source changes only for measured residual causes.
- Pass/fail statement against the read and write parity thresholds.

Downstream contract:
- Future plans may use the final benchmark report as the native GCS read/write performance baseline.

Assumptions exported:
- Native GCS read and write performance are either within the agreed parity threshold or the remaining bottlenecks are measured and bounded.

Assumptions not exported:
- That improvements generalize beyond the recorded endpoint/workload without further benchmark evidence.

Expected artifacts:
- `tmp/gcs-grpc-s3-perf-parity/p06-residual-performance-closure/`: final benchmark and profiling logs.
- Targeted source changes only if residual profiling identifies a scoped fix.
- `plans/gcs-grpc-s3-perf-parity/06-residual-performance-closure-review.md`: phase review.

Verification approach:
- Tier: Tier 2
- Method: Run final repeated native and GCS-as-`s3` read/write benchmarks from P01 and `/work/gcs-grpc-testing/run_benchmarks.sh`, compare median elapsed time against the P01/P06 thresholds, and record secondary metrics plus per-run trends. If scoped residual fixes are made, rebuild with logged output and repeat the benchmark.
- Sufficiency: The plan's primary success criterion is read/write workload behavior; final behavioral benchmark is required and sufficient for plan-level closure.

Completion criteria:
- Native GCS read and write median elapsed times meet the P01/P06 thresholds, or the phase review records specific residual bottlenecks with evidence and recommended follow-ups.
- Secondary metrics do not show hidden regressions in bytes read/written, request count, CPU/allocation indicators, or error counters.
- Existing GCS-as-`s3` baseline behavior remains stable.
- Per-run trend analysis is recorded, including whether native warm-up behavior affects interpretation.

Risks and rollback:
- Risk: Residual gap is environment noise. Mitigation: Use repeated medians, per-run trend reporting, and the P01 environment contract. Rollback: Mark result inconclusive and require environment stabilization before further code changes.
- Risk: Residual optimization destabilizes correctness. Mitigation: Keep changes scoped and benchmark with correctness-preserving `FORMAT Null` scans plus read/write profile-event checks. Rollback: Revert residual optimization and retain measured bottleneck report.

Task decomposition guidance:
- Create tasks only for residual causes that P06 profiling can measure; do not pre-authorize speculative micro-optimizations.

## Plan validation

Status: passed

Hard checks:
- Unique phase ids and slugs: pass
- Dependencies reference existing earlier phases or `none`: pass
- Phase dependency graph has no cycles: pass
- No phase depends on a later phase: pass
- Every phase has expected artifacts or `None` with a reason: pass
- Every phase has a verification approach and tier: pass
- Every plan-level acceptance criterion maps to at least one phase: pass
- If `investigation.md` exists, imported findings, constraints, assumptions, grey areas, and blockers are reflected or explicitly rejected with rationale: pass
- Blocking or plan-shaping open questions are asked directly in the chat response, or plan status is `blocked`: pass
- Plan contains no implementation task checklist: pass
- Plan contains no generic filler: pass

Warnings:
- The repository has pre-existing dirty source changes in GCS-related files; this plan records them as evidence-bearing but does not own or modify them.
- `plans/` is ignored by the global gitignore in this environment, so the new plan file may not appear in normal `git status` output.
- The `1.25x` elapsed-time threshold is an initial planning threshold for read and write parity and may be adjusted only by P01/P06 with recorded benchmark/environment rationale.

## Review and handoff expectations

- Each phase must produce `<phase-slug>-review.md` before completion.
- Review findings that require work must become tasks before the next phase starts.
- Notes must capture assumptions, decisions, uncertainties, and handoff summary.

## Plan change log

- 2026-05-12: Initial plan created from ready investigation baseline and resolved user answers.
- 2026-05-12: Added staging validation instructions for `clickhouse-builder-0`, `/work/ch-dev`, remote builds, `start_server.sh`, `query_log`, and the existing native/S3 benchmark tables.
- 2026-05-12: Recorded exact staging DirectPath evidence: `/work/gcs-grpc-testing/env.sh` uses the `google-c2p://` endpoint and native GCS reads/writes are confirmed working.
- 2026-05-13: Recorded staging `/work/gcs-grpc-testing/run_benchmarks.sh` results from `/work/results/20260513T000127Z`; read median meets the provisional threshold, native write is much slower, native copy is faster than S3, and native read/copy speed up across iterations.
- 2026-05-13: Expanded plan scope to include native GCS write parity based on user request and the `7.34x` native write median gap.
- 2026-05-13: Required P02 to test the higher-level `google-cloud-cpp` `storage::MakeGrpcClient` path on a separate branch before choosing the final client architecture.

## Plan maintenance

- Update this plan only when scope, phase order, acceptance criteria, or constraints change.
- Record every material plan change in the plan change log.
- Do not use this plan as a task list.
