# Client architecture comparison Notes

Plan: [plan.md](./plan.md)
Phase: P02 / `02-client-architecture-comparison`

## Implementation context

- This phase is an architecture decision phase, not the production implementation phase. No production source files were edited on the main branch.
- The plan required a real, separate-branch test of the higher-level `google-cloud-cpp` `storage::MakeGrpcClient` path. The branch `gcs-grpc-make-grpc-client-experiment` was created from `046405713d7`.
- A temporary worktree was created under `tmp/gcs-grpc-s3-perf-parity/p02-client-architecture-comparison/make-grpc-client-worktree` and then removed after the source/API blocker was documented. The branch remains.
- Current local branch observed during task creation and work: `gcs-grpc-coherent`.
- Existing dirty changes are present in `contrib/liburing` and `contrib/sysroot`; they were treated as pre-existing out-of-scope submodule state and were not touched.
- Existing plan/P01 note updates were preserved as related P02 context.
- P01 artifacts live under `tmp/gcs-grpc-s3-perf-parity/p01-benchmark-and-environment-contract/` and include benchmark logs, table contract, source-state classification, and benchmark summaries.
- Staging context remains: use `kubectl exec -n ch-builder clickhouse-builder-0 -- <command>`; remote source tree is `/work/ch-dev`; build directory is `/work/ch-dev/build`; use `/work/gcs-grpc-testing/start_server.sh` for restart if needed.

## Investigation context

- Investigation file: [investigation.md](./investigation.md)
- Relevant findings: F001, F002, F004, F005, F006, F007, F008
- Relevant constraints: C002, C003, C004, C005
- Relevant assumptions validated: AS003 was validated by comparing the custom stub path with high-level `MakeGrpcClient`; AS004 remains valid for staging based on `google-c2p://` evidence; AS005 remains assigned to P05 because expected-cancellation metrics depend on final client architecture.
- Relevant open questions/blockers: None. The high-level `MakeGrpcClient` path has a P02 blocker for direct adoption, but it is not user-answerable and has a safe P04 recommendation.

## Decisions from planning

- D001: Use existing GCS-as-`s3` XML/API against GCS as the baseline, not AWS S3.
- D002: Use elapsed workload time as the primary metric, with CPU, allocations, bytes, request counts, error counters, and per-run trends as diagnostics.
- D003: Compare both client architecture paths before finalizing channel/client behavior.
- D004: Require endpoint/direct-connectivity evidence for benchmark interpretation and channel-count policy.
- D007: Include native GCS write parity in this plan because staging write median was about `7.34x` slower than S3.
- P02 tested the high-level `storage::MakeGrpcClient` path on a separate branch and rejected direct adoption for P04 because public `ObjectReadStream` / `ObjectWriteStream` hide raw gRPC stream lifecycle hooks.

## Assumptions

- The custom generated-stub path is the safer P04 production direction. Confidence: high. Validation: `architecture-comparison.md` maps it against high-level and internal-upstream alternatives.
- `MakeGrpcClient` remains valuable as a reference implementation for endpoint-aware channel policy, channel arguments, DirectPath defaults, upload-buffer defaults, round-robin behavior, and channel refresh. Confidence: high. Validation: `make-grpc-client-source-analysis.md` and `architecture-comparison.md`.
- Direct high-level `MakeGrpcClient` adoption would require broader instrumentation and test-surface changes than P04 should carry. Confidence: high. Validation: `make-grpc-client-experiment.md`.
- The native write gap may require write-path tuning even after channel policy is formalized. Confidence: medium. Validation path: P04/P06 read/write benchmarks.

## Non-exported assumptions

- Do not assume high-level `storage::MakeGrpcClient` is unusable for every future design; P02 only rejects direct adoption for this P04 path because preserving current raw stream hooks is required.
- Do not assume the custom generated-stub path has solved write parity; it is only the selected architecture direction, and write tuning remains assigned to P04/P06.
- Do not assume P02 benchmark results exist for `MakeGrpcClient`; benchmark execution was blocked by the documented API mismatch before a buildable prototype existed.

## Risks

- Risk: The separate-branch experiment did not produce a buildable prototype. Mitigation: P02 found a source/API blocker before implementation; `make-grpc-client-build.md` and `make-grpc-client-benchmark.md` record why build/benchmark were skipped.
- Risk: Rejecting direct high-level `MakeGrpcClient` may miss upstream write buffering improvements. Mitigation: P04 should borrow upstream defaults and recommendations, especially the 32 MiB gRPC upload buffer and DirectPath-aware channel policy.
- Risk: Custom pooling duplicates upstream code. Mitigation: P04 should keep custom code narrow and explicitly align with upstream defaults, rather than inventing new behavior. No flamenco improvisation in the hot path.
- Risk: Final channel policy may still regress DirectPath. Mitigation: P04 must test or document `google-c2p://` behavior and avoid hardcoded 16-channel policy.

## Deferred or future work

- P03 bounded-read parity remains separate and should not be folded into P02.
- P04 should formalize custom generated-stub client/channel behavior with upstream-aligned channel count and channel arguments.
- P04 should investigate write-path tuning using upstream evidence: 32 MiB default gRPC upload buffer, 256 KiB upload quantum, and direct `WriteObject` stream behavior.
- P05 cancellation metrics cleanup is deferred until the architecture choice is stable.
- P06 residual read/write performance closure is deferred until P03-P05 complete.

## Handoff summary

Current status:
- P02 is complete, reviewed, and ready for phase completion commit.

Completed artifacts:
- `tmp/gcs-grpc-s3-perf-parity/p02-client-architecture-comparison/source-and-branch-context.md`: branch, status, staging, and P01 context.
- `tmp/gcs-grpc-s3-perf-parity/p02-client-architecture-comparison/custom-stub-analysis.md`: current custom generated-stub path analysis.
- `tmp/gcs-grpc-s3-perf-parity/p02-client-architecture-comparison/make-grpc-client-source-analysis.md`: upstream high-level `MakeGrpcClient` source analysis.
- `tmp/gcs-grpc-s3-perf-parity/p02-client-architecture-comparison/make-grpc-client-branch.md`: separate branch/worktree evidence.
- `tmp/gcs-grpc-s3-perf-parity/p02-client-architecture-comparison/make-grpc-client-experiment.md`: source/API blocker and experiment outcome.
- `tmp/gcs-grpc-s3-perf-parity/p02-client-architecture-comparison/make-grpc-client-build.md`: skipped build record with blocker rationale.
- `tmp/gcs-grpc-s3-perf-parity/p02-client-architecture-comparison/make-grpc-client-benchmark.md`: blocked benchmark record with rationale.
- `tmp/gcs-grpc-s3-perf-parity/p02-client-architecture-comparison/architecture-comparison.md`: final architecture matrix and P04 recommendation.
- `plans/gcs-grpc-s3-perf-parity/02-client-architecture-comparison-review.md`: P02 review with critic-agent all-clear.

Key decisions:
- Recommend custom generated stubs aligned with upstream `google-cloud-cpp` defaults for P04.
- Reject direct high-level `storage::MakeGrpcClient` adoption for P04 because it hides raw `grpc::ClientContext`, raw streaming `Finish` status, and generated message lifecycle hooks needed by ClickHouse.
- Use `MakeGrpcClient` as a reference for channel count, DirectPath behavior, channel arguments, upload-buffer defaults, and write recommendations.

Assumptions:
- DirectPath staging evidence remains valid for future benchmark interpretation; confidence high for this staging environment.
- P04 can preserve current fakes/profile events/cancellation behavior while improving channel policy; confidence high.

Uncertainties:
- Exact write-path bottleneck remains unresolved; assigned to P04/P06 with benchmark evidence.
- Whether upstream internal APIs are worth selectively borrowing remains a P04 implementation detail, not a P02 blocker.

Next likely work:
- Run `/phase-tasks gcs-grpc-s3-perf-parity 03-bounded-read-parity` after this phase is committed.
