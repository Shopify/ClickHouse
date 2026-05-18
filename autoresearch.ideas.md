# Autoresearch ideas

- Try XML policy variants: `32 MiB/20`, `64 MiB/32`, `128 MiB/16`, and adaptive per-object thresholds, but keep the benchmark broad so policy choices do not overfit only the 23.59 GiB part.
- Investigate why native `GCS` read path shows lower object count similarity but different per-read latency; compare gRPC read stream buffering and prefetch size against XML path.
- Investigate whether XML read path can reuse S3-style connection pool/client settings for lower single-object index-read overhead.
- Profile `EXPLAIN indexes = 1` with a single query under `perf` if `trace_log` remains too syscall-heavy to identify the client-side overhead.
