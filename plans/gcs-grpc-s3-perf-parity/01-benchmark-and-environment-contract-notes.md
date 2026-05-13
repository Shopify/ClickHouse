# Benchmark and environment contract Notes

Plan: [plan.md](./plan.md)
Phase: P01 / `01-benchmark-and-environment-contract`

## Implementation context

- This phase must not edit source files. It creates benchmark/environment artifacts under `tmp/gcs-grpc-s3-perf-parity/p01-benchmark-and-environment-contract/` and later creates the phase review file.
- Current worktree has pre-existing dirty changes in `src/IO/GCS/GCSClient.cpp`, `src/Disks/DiskObjectStorage/ObjectStorages/GCS/GCSObjectStorage.cpp`, `contrib/liburing`, and `contrib/sysroot`; classify these, do not silently own them.
- Use repository-local `tmp`, not `/tmp`, for new local artifacts.
- Remote testing context from the investigation used `kubectl exec -n ch-builder clickhouse-builder-0 -- ...`, source tree `/work/ch-dev`, build directory `/work/ch-dev/build`, and populated tables `p06_gcs_20260512T174206Z.native_read` and `p06_gcs_20260512T174206Z.s3_read`.
- Benchmark commands use `clickhouse-client --print-profile-events --time` and `SELECT * FROM <table> FORMAT Null` for native and GCS-as-`s3` paths.
- Both `remote_filesystem_read_method='threadpool'` and `'read'` are part of the P01 contract because the investigation found right-bounded read behavior matters most under the threadpool/gather path.
- Post-P01 staging benchmark evidence from `/work/results/20260513T000127Z`: `/work/gcs-grpc-testing/run_benchmarks.sh` measured read median native `1.38s` vs S3 `1.18s`, write median native `79.25s` vs S3 `10.80s`, and copy median native `2.66s` vs S3 `4.12s`.
- No source files were edited in P01.

## Investigation context

- Investigation file: [investigation.md](./investigation.md)
- Relevant findings: F001, F002, F003, F004, F005, F006, F008
- Relevant constraints: C002, C003, C004, C005
- Relevant assumptions validated: AS001 and AS002 validated for the current remote tables; AS004 was diagnostic-only during P01, then exact staging evidence was provided: `/work/gcs-grpc-testing/env.sh` uses the `google-c2p://` endpoint and native GCS reads/writes work.
- Relevant open questions/blockers: Q004/G004 is non-blocking and now recorded in `direct-connectivity.md`; no user-answerable blocker remains.

## Decisions from planning

- D001: Use existing GCS-as-`s3` XML/API against GCS as the baseline, not AWS S3.
- D002: Use elapsed query time as the primary metric, with CPU, allocations, bytes, request counts, and error counters as diagnostics.
- D004: Require endpoint/direct-connectivity evidence before treating benchmark results as broad acceptance-grade evidence.
- P01 keeps the default threshold for later acceptance: native median elapsed time no worse than `1.25x` the GCS-as-`s3` median. P01's own diagnostic artifact could not prove DirectPath, but later exact staging evidence shows `/work/gcs-grpc-testing/env.sh` uses the `google-c2p://` endpoint and native GCS reads/writes work.

## Assumptions

- The benchmark workload is read-heavy `MergeTree` scans over GCS-backed object storage. Confidence: high. Validated by `tables.md` and the benchmark command matrix.
- The known benchmark tables exist. Confidence: high for this phase. Validated by `tables.md`: both tables exist with `5,000,000` rows and `746,505,033` bytes in `system.tables`.
- Direct-connectivity status can be determined from configuration or diagnostics. Confidence: high for staging after exact evidence: `/work/gcs-grpc-testing/env.sh` uses the `google-c2p://` endpoint and native GCS reads/writes work. P01's `direct-connectivity.md` remains a historical record of the failed in-container diagnostic attempt.
- Repeated medians are more reliable than single runs. Confidence: high. Validated by three runs per matrix cell in `logs/` and summarized in `benchmark-results.md`.

## Risks

- Risk: Existing dirty prototype changes may contaminate baseline interpretation. Mitigation: `source-state.md` classifies GCS source changes as prototype and contrib submodules as out-of-scope.
- Risk: Future agents may rely on P01's historical diagnostic-only artifact and miss the later exact evidence. Mitigation: `plan.md` and these notes now record that `/work/gcs-grpc-testing/env.sh` uses the `google-c2p://` endpoint and native GCS reads/writes work.
- Risk: Remote benchmark tables may have changed or disappeared. Mitigation: `tables.md` records current table existence, schema, rows, and bytes.
- Risk: Cache state or server settings make runs noisy. Mitigation: `benchmark-commands.sh` records repeated runs and `benchmark-results.md` reports medians plus secondary profile events.

## Deferred or future work

- P02 client architecture comparison can now use P01 benchmark artifacts and `source-state.md`.
- P03 bounded-read parity can use P01 byte metrics showing native GCS bytes match compressed bytes in the current prototype state.
- The write benchmark gap from `/work/results/20260513T000127Z` is now in scope for this plan after user clarification; P02/P04/P06 should investigate whether it shares client/channel, request concurrency, or streaming issues with the read path.

## Handoff summary

Current status:
- P01 is complete, reviewed, and committed; later notes updated with user-confirmed staging DirectPath/direct connectivity.

Completed artifacts:
- `plans/gcs-grpc-s3-perf-parity/01-benchmark-and-environment-contract-tasks.md`: completed task list.
- `plans/gcs-grpc-s3-perf-parity/01-benchmark-and-environment-contract-notes.md`: updated implementation notes and handoff.
- `plans/gcs-grpc-s3-perf-parity/01-benchmark-and-environment-contract-review.md`: Tier 2 review with critic-agent all-clear.
- `tmp/gcs-grpc-s3-perf-parity/p01-benchmark-and-environment-contract/environment.md`: worktree and remote context.
- `tmp/gcs-grpc-s3-perf-parity/p01-benchmark-and-environment-contract/tables.md`: table contract.
- `tmp/gcs-grpc-s3-perf-parity/p01-benchmark-and-environment-contract/direct-connectivity.md`: endpoint/direct-connectivity evidence and diagnostic-only classification.
- `tmp/gcs-grpc-s3-perf-parity/p01-benchmark-and-environment-contract/benchmark-commands.sh`: benchmark matrix.
- `tmp/gcs-grpc-s3-perf-parity/p01-benchmark-and-environment-contract/logs/*.log`: raw benchmark logs.
- `tmp/gcs-grpc-s3-perf-parity/p01-benchmark-and-environment-contract/benchmark-results.md`: benchmark summary.
- `tmp/gcs-grpc-s3-perf-parity/p01-benchmark-and-environment-contract/benchmark-summary.json`: parsed benchmark data.
- `tmp/gcs-grpc-s3-perf-parity/p01-benchmark-and-environment-contract/source-state.md`: source-state classification.

Key decisions:
- P01 source-state classification treats dirty GCS source changes as prototype and contrib submodule changes as out-of-scope.
- The `1.25x` elapsed-time threshold remains the planning threshold for later phases, though P01 shows native/threadpool currently misses it and native/read meets it in the prototype state.
- Post-P01 exact evidence supersedes the P01 diagnostic-only uncertainty for staging: `/work/gcs-grpc-testing/env.sh` uses the `google-c2p://` endpoint and native GCS reads/writes work. P01 artifacts still show that the in-container diagnostic could not prove it at the time.
- Post-P01 `/work/results/20260513T000127Z` benchmark evidence: read median native/S3 ratio is about `1.17x`, within the provisional `1.25x` threshold; write native is about `7.34x` slower; copy native is about `0.65x` of S3 time and therefore faster.

Assumptions:
- The current benchmark tables are valid for downstream phase comparisons in this environment; confidence high for this remote pod.
- DirectPath is available and working in staging: `/work/gcs-grpc-testing/env.sh` uses the `google-c2p://` endpoint and native GCS reads/writes work; confidence high.

Uncertainties:
- Whether the native write gap is mainly client/channel reuse, write request concurrency, upload stream behavior, or a separate write-path issue; assigned to P02/P04/P06.
- How much of the remaining threadpool/read gap is external-buffer/copy overhead versus runtime noise; assigned to later phases.

Next likely work:
- Run `/phase-tasks gcs-grpc-s3-perf-parity 02-client-architecture-comparison`.

