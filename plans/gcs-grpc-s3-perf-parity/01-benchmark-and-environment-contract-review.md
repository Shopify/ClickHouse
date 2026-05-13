# Benchmark and environment contract Review

## Verification

- Commands run:
  - `git status --short --untracked-files=all`
  - `kubectl exec -n ch-builder clickhouse-builder-0 -- bash -lc '... clickhouse client --version ... SELECT version(), buildId() ...'`
  - `kubectl exec -n ch-builder clickhouse-builder-0 -- bash -lc '... EXISTS TABLE ... SHOW CREATE TABLE ... system.tables ... system.parts ...'`
  - `kubectl exec -n ch-builder clickhouse-builder-0 -- bash -lc '... system.disks ... system.settings ... metadata probe ... gcloud storage diagnose availability ...'`
  - `tmp/gcs-grpc-s3-perf-parity/p01-benchmark-and-environment-contract/benchmark-commands.sh`
  - `python3` parser for `tmp/gcs-grpc-s3-perf-parity/p01-benchmark-and-environment-contract/logs/*.log`
  - Reviewer agent: critic review of P01 tasks and artifacts.
- Results:
  - Remote server/client was available: ClickHouse `26.5.1.1`, build id `E271638D5031FEEC49001FDC4D99F8858032C363`.
  - Benchmark tables exist: `p06_gcs_20260512T174206Z.native_read` and `p06_gcs_20260512T174206Z.s3_read`, each `5,000,000` rows and `746,505,033` bytes in `system.tables`.
  - Native GCS and GCS-as-`s3` disks are both remote object storage in `system.disks`; endpoint/bucket values are hidden in `SHOW CREATE TABLE`.
  - DirectPath/direct-connectivity could not be proven: cloud metadata shows GCE zone `us-central1-c`, but `gcloud storage diagnose` is unavailable and hidden endpoint/bucket prevents a definitive diagnostic.
  - All 12 benchmark matrix runs completed with exit code `0`.
  - Median elapsed results from `benchmark-results.md`:
    - native/threadpool: `1.559s`; S3/threadpool: `0.937s`; ratio `1.664`.
    - native/read: `2.856s`; S3/read: `2.921s`; ratio `0.978`.
  - Median native read bytes match compressed bytes for both methods in this prototype-state benchmark.
  - GCS read-error counters remain non-zero on successful native reads, confirming the P05 diagnostic need.
- Evidence:
  - `tmp/gcs-grpc-s3-perf-parity/p01-benchmark-and-environment-contract/environment.md`
  - `tmp/gcs-grpc-s3-perf-parity/p01-benchmark-and-environment-contract/tables.md`
  - `tmp/gcs-grpc-s3-perf-parity/p01-benchmark-and-environment-contract/direct-connectivity.md`
  - `tmp/gcs-grpc-s3-perf-parity/p01-benchmark-and-environment-contract/benchmark-commands.sh`
  - `tmp/gcs-grpc-s3-perf-parity/p01-benchmark-and-environment-contract/logs/*.log`
  - `tmp/gcs-grpc-s3-perf-parity/p01-benchmark-and-environment-contract/benchmark-results.md`
  - `tmp/gcs-grpc-s3-perf-parity/p01-benchmark-and-environment-contract/benchmark-summary.json`
  - `tmp/gcs-grpc-s3-perf-parity/p01-benchmark-and-environment-contract/source-state.md`
- Verification tier used: Tier 2 behavioral.
- Deviations from planned verification:
  - During P01, direct-connectivity status was `diagnostic-only` rather than proven because endpoint/bucket data was hidden and `gcloud storage diagnose` was unavailable in the remote container.
  - P01 results were treated as acceptance-grade for this exact environment contract but diagnostic-only for broad GCS gRPC performance claims at review time.
  - Post-review update: exact staging evidence was later provided: `/work/gcs-grpc-testing/env.sh` uses the `google-c2p://` endpoint and native GCS reads/writes are confirmed working; P01 raw artifacts remain historical.
  - Post-review benchmark update: `/work/gcs-grpc-testing/run_benchmarks.sh` results in `/work/results/20260513T000127Z` show read median native `1.38s` vs S3 `1.18s` (`1.17x`), write median native `79.25s` vs S3 `10.80s` (`7.34x` slower), and copy median native `2.66s` vs S3 `4.12s` (`0.65x`, native faster).


## Critique

- Risks:
  - The source tree contains pre-existing dirty prototype changes in `src/IO/GCS/GCSClient.cpp` and `src/Disks/DiskObjectStorage/ObjectStorages/GCS/GCSObjectStorage.cpp`; benchmark results measure that prototype state, not a clean upstream baseline.
  - DirectPath was unresolved during P01 review, but exact staging evidence was later provided: `/work/gcs-grpc-testing/env.sh` uses the `google-c2p://` endpoint and native GCS reads/writes work.
  - The P01 in-phase `threadpool` case missed the default `1.25x` elapsed-time threshold, but the later `/work/results/20260513T000127Z` read benchmark meets it; P03-P06 remain needed because source state and final architecture still need production validation.
- Gaps:
  - No definitive DirectPath diagnostic was possible from the remote container.
  - No allocation profiler was run; P01 only records allocation-related profile events available from `--print-profile-events`.
  - Later staging results expose a large native write gap; after P01 review, the user expanded this plan to include write parity, so P02/P04/P06 must address it.
- Over-scope or under-scope concerns:
  - No source files were edited, which matches P01 scope.
  - Benchmark execution stayed within the phase's Tier 2 behavioral verification scope.

## Review findings

- [x] R001: Task status and checkboxes were initially stale after artifacts were generated.
  Severity: low
  Evidence: Reviewer agent observed `01-benchmark-and-environment-contract-tasks.md` still had `Phase status: not started` and unchecked tasks while artifacts satisfied T001-T007.
  Required follow-up: Resolved by updating `01-benchmark-and-environment-contract-tasks.md` with completed task evidence and `Phase status: completed`.

## Tasks added from findings

- None.

## Reviewer all-clear

Reviewer: critic agent `1778628806733-rf1w7t`
Status: approved
Notes: Reviewer confirmed T001-T007 acceptance criteria are met. P01 can approve completion. At review time, results were a reproducible diagnostic contract because DirectPath/direct-connectivity was not proven by P01 artifacts; after review, exact staging evidence was provided: `/work/gcs-grpc-testing/env.sh` uses the `google-c2p://` endpoint and native GCS reads/writes work. Later `/work/results/20260513T000127Z` benchmark results show read median parity within the provisional threshold, plus a large native write gap that is now in scope by user request.

