# P02 source and branch context

Plan: `plans/gcs-grpc-s3-perf-parity/plan.md`
Phase: P02 / `02-client-architecture-comparison`

## Current branch and base

- Main working branch: `gcs-grpc-coherent`
- Main branch HEAD during P02 start: `046405713d7`
- Experiment branch selected: `gcs-grpc-make-grpc-client-experiment`
- Experiment branch base: `046405713d7`

## Worktree status snapshot

Command:

```bash
git status --short --untracked-files=all
```

Snapshot after creating the experiment worktree:

```text
 m contrib/liburing
 m contrib/sysroot
 M plans/gcs-grpc-s3-perf-parity/01-benchmark-and-environment-contract-notes.md
 M plans/gcs-grpc-s3-perf-parity/01-benchmark-and-environment-contract-review.md
 M plans/gcs-grpc-s3-perf-parity/plan.md
?? tmp/gcs-grpc-s3-perf-parity/p02-client-architecture-comparison/make-grpc-client-worktree/
```

Classification:

- `contrib/liburing`: pre-existing submodule state; out of scope for P02.
- `contrib/sysroot`: pre-existing submodule state; out of scope for P02.
- `plans/gcs-grpc-s3-perf-parity/plan.md`: plan-maintenance update that made write parity and separate-branch `MakeGrpcClient` testing explicit; related context for P02.
- `plans/gcs-grpc-s3-perf-parity/01-benchmark-and-environment-contract-notes.md`: post-P01 context update; related context for P02.
- `plans/gcs-grpc-s3-perf-parity/01-benchmark-and-environment-contract-review.md`: post-P01 context update; related context for P02.
- `tmp/gcs-grpc-s3-perf-parity/p02-client-architecture-comparison/make-grpc-client-worktree/`: temporary separate worktree for the `MakeGrpcClient` experiment; do not commit this directory.

## P01 artifact links

- `tmp/gcs-grpc-s3-perf-parity/p01-benchmark-and-environment-contract/environment.md`
- `tmp/gcs-grpc-s3-perf-parity/p01-benchmark-and-environment-contract/tables.md`
- `tmp/gcs-grpc-s3-perf-parity/p01-benchmark-and-environment-contract/direct-connectivity.md`
- `tmp/gcs-grpc-s3-perf-parity/p01-benchmark-and-environment-contract/benchmark-commands.sh`
- `tmp/gcs-grpc-s3-perf-parity/p01-benchmark-and-environment-contract/benchmark-results.md`
- `tmp/gcs-grpc-s3-perf-parity/p01-benchmark-and-environment-contract/benchmark-summary.json`
- `tmp/gcs-grpc-s3-perf-parity/p01-benchmark-and-environment-contract/source-state.md`

## Staging environment facts

- Remote command wrapper: `kubectl exec -n ch-builder clickhouse-builder-0 -- <command>`
- Remote source tree: `/work/ch-dev`
- Remote build directory: `/work/ch-dev/build`
- Server helper: `/work/gcs-grpc-testing/start_server.sh`
- Benchmark helper: `/work/gcs-grpc-testing/run_benchmarks.sh`
- Latest result directory recorded before P02: `/work/results/20260513T000127Z`
- Native GCS endpoint evidence: `/work/gcs-grpc-testing/env.sh` uses `google-c2p://` and native GCS reads/writes work in staging.
- Existing benchmark tables: `p06_gcs_20260512T174206Z.native_read` and `p06_gcs_20260512T174206Z.s3_read`.

## Experiment branch selected before experiment edits

The selected branch for the high-level `storage::MakeGrpcClient` experiment is `gcs-grpc-make-grpc-client-experiment`. It was created from `046405713d7` before any P02 experiment edits.

## Post-experiment cleanup

The temporary experiment worktree was removed after documenting the `MakeGrpcClient` blocker. The branch `gcs-grpc-make-grpc-client-experiment` remains available for future experiments.

## Source-state clarification

Earlier GCS prototype source changes discussed by P01 are now part of base commit `046405713d7` in this worktree, so `git status` no longer shows dirty `src/IO/GCS/GCSClient.cpp` or `src/Disks/DiskObjectStorage/ObjectStorages/GCS/GCSObjectStorage.cpp`. Dirty submodule states in `contrib/liburing` and `contrib/sysroot` remain out of scope.
