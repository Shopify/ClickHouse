# Client architecture comparison Tasks

Phase status: completed

Phase id: P02
Phase slug: 02-client-architecture-comparison
Plan: [plan.md](./plan.md)

Phase goal:
Choose the production architecture for native GCS gRPC client/channel behavior by comparing the formalized custom generated-stub/channel-pool path against a separate-branch test of the higher-level `google-cloud-cpp` `storage::MakeGrpcClient` path for read and write workloads.

Verification tier:
Tier 3

Dependencies:
- P01 / `01-benchmark-and-environment-contract` completed

Tasks:
- [x] T001: Create `tmp/gcs-grpc-s3-perf-parity/p02-client-architecture-comparison/source-and-branch-context.md` with the current local branch, `git status --short --untracked-files=all`, P01 artifact links, staging environment facts, and the selected `MakeGrpcClient` experiment branch name. Acceptance: the file names current branch `gcs-grpc-coherent`, records existing dirty GCS source/submodule changes as pre-existing prototype or out-of-scope state, links the P01 benchmark artifacts, and names the separate experiment branch before any experiment edits.
  Done: local change pending commit, verified by `source-and-branch-context.md` and `git branch --format='%(refname:short)'` showing `gcs-grpc-make-grpc-client-experiment`.
- [x] T002: Document the custom generated-stub/channel-pool path in `tmp/gcs-grpc-s3-perf-parity/p02-client-architecture-comparison/custom-stub-analysis.md`. Acceptance: the analysis covers `src/IO/GCS/GCSClient.cpp`, `src/IO/GCS/GCSClient.h`, and `src/Disks/DiskObjectStorage/ObjectStorages/GCS/GCSObjectStorage.cpp`; records persistent channel/client behavior, per-RPC `grpc::ClientContext` lifecycle, endpoint/channel-count policy gaps, read streaming fit, write/upload fit, profile events, throttling, fakes, and cancellation hooks.
  Done: local change pending commit, verified by `custom-stub-analysis.md`.
- [x] T003: Document the upstream high-level `storage::MakeGrpcClient` path in `tmp/gcs-grpc-s3-perf-parity/p02-client-architecture-comparison/make-grpc-client-source-analysis.md`. Acceptance: the analysis covers `contrib/google-cloud-cpp/google/cloud/storage/client.h`, `contrib/google-cloud-cpp/google/cloud/storage/grpc_plugin.cc`, `contrib/google-cloud-cpp/google/cloud/storage/grpc_plugin.h`, `contrib/google-cloud-cpp/google/cloud/storage/internal/grpc/default_options.cc`, `contrib/google-cloud-cpp/google/cloud/storage/internal/storage_stub_factory.cc`, and `contrib/google-cloud-cpp/google/cloud/storage/internal/storage_round_robin_decorator.cc`; records channel defaults, DirectPath behavior, read/write API surface, stream/control hooks, options/settings surface, fake/test seams, and ClickHouse instrumentation barriers.
  Done: local change pending commit, verified by `make-grpc-client-source-analysis.md`.
- [x] T004: Create or use the separate `MakeGrpcClient` experiment branch and record it in `tmp/gcs-grpc-s3-perf-parity/p02-client-architecture-comparison/make-grpc-client-branch.md`. Acceptance: the file records the branch name, base commit, creation or checkout command, worktree safety handling for pre-existing dirty changes, and confirms the experiment branch is not merged into the main production path during P02.
  Done: local change pending commit, verified by `make-grpc-client-branch.md` and branch `gcs-grpc-make-grpc-client-experiment`.
- [x] T005: Prototype or prove a blocker for using `storage::MakeGrpcClient` on the experiment branch, then summarize the result in `tmp/gcs-grpc-s3-perf-parity/p02-client-architecture-comparison/make-grpc-client-experiment.md`. Acceptance: the summary records changed files or attempted integration points, maps native read and write needs to `MakeGrpcClient` APIs, identifies any missing hooks for profile events/throttling/cancellation/fakes, and includes either a diff summary for the prototype or a concrete blocker with source references.
  Done: local change pending commit, verified by `make-grpc-client-experiment.md` source/API blocker references.
- [x] T006: Build the experiment branch if T005 produces a buildable prototype, using `ninja programs/clickhouse` from the selected build directory with output redirected to a unique build log. Acceptance: `tmp/gcs-grpc-s3-perf-parity/p02-client-architecture-comparison/make-grpc-client-build.md` records the exact build command, build log path, subagent log summary, exit status, and either successful `clickhouse` build evidence or the first blocking compile/link error.
  Done: local change pending commit, verified by `make-grpc-client-build.md`; build skipped because T005 produced a source/API blocker rather than a buildable prototype.
- [x] T007: Run or explicitly block the P02 read/write benchmark comparison for the experiment branch and record results in `tmp/gcs-grpc-s3-perf-parity/p02-client-architecture-comparison/make-grpc-client-benchmark.md`. Acceptance: if T006 builds, the file records the exact staging sync/restart commands and `/work/gcs-grpc-testing/run_benchmarks.sh` or equivalent read/write commands, result directory, native/S3 read and write elapsed results, and endpoint evidence; if T006 is blocked, the file records why benchmark execution is impossible and links the build/blocker artifact.
  Done: local change pending commit, verified by `make-grpc-client-benchmark.md` linking the build and source/API blocker artifacts.
- [x] T008: Create `tmp/gcs-grpc-s3-perf-parity/p02-client-architecture-comparison/architecture-comparison.md` comparing custom pooling, closer upstream-internal alignment, and high-level `MakeGrpcClient`. Acceptance: the matrix covers correctness, read performance, write performance, DirectPath/non-DirectPath policy, settings surface, profile events, throttling, fakes, cancellation, retry behavior, compatibility with GCS-as-`s3`, production risk, and testability; it names the recommended P04 architecture and rejected alternatives.
  Done: local change pending commit, verified by `architecture-comparison.md`.
- [x] T009: Update `plans/gcs-grpc-s3-perf-parity/02-client-architecture-comparison-notes.md` with the final P02 handoff. Acceptance: notes record the chosen architecture recommendation, experiment branch outcome, assumptions exported to P04, non-exported assumptions, residual uncertainties, and the next likely `/phase-work` or review step.
  Done: local change pending commit, verified by `02-client-architecture-comparison-notes.md`.

Task dependencies:

| Task | Depends on | Can run before review? | Reason |
|---|---|---|---|
| T001 | none | yes | Source and branch context is the foundation for safe experiment work and later evidence. |
| T002 | T001 | yes | Custom-path analysis should use the recorded source-state context. |
| T003 | T001 | yes | Upstream-path analysis should use the recorded source-state context. |
| T004 | T001 | yes | The experiment branch must be based on the recorded branch/status and must preserve unrelated dirty changes. |
| T005 | T003, T004 | yes | The prototype or blocker needs the upstream API analysis and the separate branch. |
| T006 | T005 | yes | Build evidence depends on the prototype or blocker result. |
| T007 | T006 | yes | Benchmark execution depends on a buildable experiment or must record the build blocker. |
| T008 | T002, T003, T005, T006, T007 | yes | The comparison matrix needs custom analysis, upstream analysis, and experiment/build/benchmark evidence. |
| T009 | T008 | yes | Final handoff notes should summarize the completed comparison and recommendation. |

Review gates:
- Verification must be recorded in `02-client-architecture-comparison-review.md`.
- Critique must be recorded in `02-client-architecture-comparison-review.md`.
- Reviewer all-clear must be recorded before phase completion.
