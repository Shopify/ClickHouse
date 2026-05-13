# `MakeGrpcClient` experiment benchmark record

## Benchmark status

- Benchmark run: no
- Result directory: not created
- Endpoint evidence: staging is known to use `google-c2p://` from `/work/gcs-grpc-testing/env.sh`, but no experiment binary was produced for benchmark execution.

## Reason blocked

The P02 `MakeGrpcClient` experiment did not produce a buildable prototype. T005 found that the high-level public API hides the raw gRPC stream lifecycle hooks ClickHouse needs to preserve current native GCS semantics and instrumentation.

Therefore the benchmark comparison is blocked by the architecture/API mismatch, not by staging availability.

## Benchmark command that would be used if P04 chooses a buildable experiment

```bash
kubectl exec -n ch-builder clickhouse-builder-0 -- bash -lc '
  cd /work/ch-dev/build && ninja programs/clickhouse > /work/ch-dev/build/gcs_grpc_make_grpc_client_p02.log 2>&1
  /work/gcs-grpc-testing/start_server.sh
  /work/gcs-grpc-testing/run_benchmarks.sh
'
```

## Baseline context for comparison

Latest known staging run before this phase: `/work/results/20260513T000127Z`.

- Read median: native `1.38s` vs S3 `1.18s`, ratio about `1.17x`.
- Write median: native `79.25s` vs S3 `10.80s`, native about `7.34x` slower.
- Copy median: native `2.66s` vs S3 `4.12s`, native faster at about `0.65x`.

## P04/P06 implication

Any future benchmark should compare a buildable production candidate, not a high-level-client replacement that loses required hooks. P04 should first implement the selected custom/upstream-aligned client policy, then P06 should repeat read/write medians and per-run trends.

## Blocker links

- Build blocker record: `tmp/gcs-grpc-s3-perf-parity/p02-client-architecture-comparison/make-grpc-client-build.md`
- Source/API blocker record: `tmp/gcs-grpc-s3-perf-parity/p02-client-architecture-comparison/make-grpc-client-experiment.md`
