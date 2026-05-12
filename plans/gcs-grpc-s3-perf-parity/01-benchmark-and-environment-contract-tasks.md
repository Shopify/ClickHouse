# Benchmark and environment contract Tasks

Phase status: completed

Phase id: P01
Phase slug: 01-benchmark-and-environment-contract
Plan: [plan.md](./plan.md)

Phase goal:
Establish the reproducible GCS-as-`s3` versus native GCS gRPC benchmark contract, including endpoint/direct-connectivity status, cache/settings controls, query set, tables, primary elapsed-time threshold, and secondary attribution metrics.

Verification tier:
Tier 2

Dependencies:
- none

Tasks:
- [x] T001: Create `tmp/gcs-grpc-s3-perf-parity/p01-benchmark-and-environment-contract/environment.md` and record local worktree state plus remote benchmark host context. Acceptance: the file includes `git status --short --untracked-files=all`, branch name, relevant dirty files classification inputs, remote command target used, ClickHouse binary path, and ClickHouse version or an explicit note that the remote server/client was unavailable.
  Done: local change pending commit, verified by `tmp/gcs-grpc-s3-perf-parity/p01-benchmark-and-environment-contract/environment.md`.
- [x] T002: Validate the benchmark table contract for `p06_gcs_20260512T174206Z.native_read` and `p06_gcs_20260512T174206Z.s3_read`. Acceptance: `tmp/gcs-grpc-s3-perf-parity/p01-benchmark-and-environment-contract/tables.md` records `EXISTS TABLE`, row/byte-count query results where available, and `SHOW CREATE TABLE` or equivalent schema evidence for both tables; if either table is missing, it records an equivalent recreation requirement and marks old table-specific numbers historical only.
  Done: local change pending commit, verified by `tmp/gcs-grpc-s3-perf-parity/p01-benchmark-and-environment-contract/tables.md`.
- [x] T003: Capture native GCS endpoint and direct-connectivity evidence in `tmp/gcs-grpc-s3-perf-parity/p01-benchmark-and-environment-contract/direct-connectivity.md`. Acceptance: the file records the native GCS endpoint/configuration evidence and either output from a direct-connectivity diagnostic such as `gcloud alpha storage diagnose --test-type=DIRECT_CONNECTIVITY` or a clear `diagnostic-only` classification explaining why DirectPath status could not be proven.
  Done: local change pending commit, verified by `tmp/gcs-grpc-s3-perf-parity/p01-benchmark-and-environment-contract/direct-connectivity.md`.
- [x] T004: Write the benchmark command matrix to `tmp/gcs-grpc-s3-perf-parity/p01-benchmark-and-environment-contract/benchmark-commands.sh`. Acceptance: the script or command log covers native GCS and GCS-as-`s3` `SELECT * ... FORMAT Null` scans, `remote_filesystem_read_method='threadpool'` and `'read'`, `--print-profile-events --time`, repeated runs for median elapsed time, and raw-log output paths for every run.
  Done: local change pending commit, verified by `tmp/gcs-grpc-s3-perf-parity/p01-benchmark-and-environment-contract/benchmark-commands.sh`.
- [x] T005: Execute the P01 benchmark command matrix and save raw output under `tmp/gcs-grpc-s3-perf-parity/p01-benchmark-and-environment-contract/logs/`. Acceptance: each matrix cell has raw logs with elapsed time and profile events, or a named failure log plus a note in `benchmark-results.md` explaining why that cell could not run.
  Done: local change pending commit, verified by 12 `logs/*.log` files with `exit_code=0`.
- [x] T006: Summarize the benchmark contract in `tmp/gcs-grpc-s3-perf-parity/p01-benchmark-and-environment-contract/benchmark-results.md`. Acceptance: the summary includes median elapsed time, native/S3 ratio, selected parity threshold, `ReadBufferFromGCSBytes`, `ReadCompressedBytes`, request counts, error counters, CPU/allocation indicators if available, and whether results are acceptance-grade or diagnostic-only.
  Done: local change pending commit, verified by `tmp/gcs-grpc-s3-perf-parity/p01-benchmark-and-environment-contract/benchmark-results.md` and `benchmark-summary.json`.
- [x] T007: Record source-state classification in `tmp/gcs-grpc-s3-perf-parity/p01-benchmark-and-environment-contract/source-state.md`. Acceptance: the file classifies current dirty changes in `src/IO/GCS/GCSClient.cpp`, `src/Disks/DiskObjectStorage/ObjectStorages/GCS/GCSObjectStorage.cpp`, `contrib/liburing`, and `contrib/sysroot` as baseline, prototype, or out-of-scope for later phases, with rationale tied to the investigation findings.
  Done: local change pending commit, verified by `tmp/gcs-grpc-s3-perf-parity/p01-benchmark-and-environment-contract/source-state.md`.
- [x] T008: Create `plans/gcs-grpc-s3-perf-parity/01-benchmark-and-environment-contract-review.md` after T001-T007. Acceptance: the review records verification evidence, critique, reviewer all-clear or required findings, and maps phase completion to A001 plus the P01 completion criteria in `plan.md`.
  Done: local change pending commit, verified by `plans/gcs-grpc-s3-perf-parity/01-benchmark-and-environment-contract-review.md` with critic-agent all-clear.

Task dependencies:

| Task | Depends on | Can run before review? | Reason |
|---|---|---|---|
| T001 | none | yes | Environment and worktree context is foundational and does not depend on benchmark execution. |
| T002 | T001 | yes | Table validation uses the remote context recorded by T001. |
| T003 | T001 | yes | Endpoint/direct-connectivity evidence uses the same remote context and can run independently of table validation. |
| T004 | T002, T003 | yes | The command matrix needs the validated table names and endpoint classification assumptions. |
| T005 | T004 | yes | Raw benchmark execution depends on the recorded command matrix. |
| T006 | T005 | yes | The benchmark summary depends on raw benchmark logs. |
| T007 | T001 | yes | Source-state classification depends on the recorded worktree state, not benchmark execution. |
| T008 | T006, T007 | no | The phase review must consume benchmark summary and source-state classification before phase completion. |

Review gates:
- Verification recorded in `01-benchmark-and-environment-contract-review.md`.
- Critique recorded in `01-benchmark-and-environment-contract-review.md`.
- Reviewer all-clear recorded before phase completion.
