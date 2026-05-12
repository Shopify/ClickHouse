# Benchmark results

Generated from raw logs in `logs/`. Direct-connectivity status is diagnostic-only; see `direct-connectivity.md`.

## Summary

| Case | Method | Runs | Median elapsed s | Min elapsed s | Max elapsed s | Median native/S3 ratio | Read bytes median | Compressed bytes median | Requests median | Errors median | Peak memory median |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| native | read | 3 | 2.856 | 2.811 | 3.339 | 0.978 | 750484937 | 750484937 | 450 | 251 | 241927521 |
| native | threadpool | 3 | 1.559 | 1.554 | 1.775 | 1.664 | 751881261 | 751881261 | 450 | 336 | 327448972 |
| s3 | read | 3 | 2.921 | 2.876 | 2.947 | n/a | 750119102 | 750119102 | 450 | 0 | 260834238 |
| s3 | threadpool | 3 | 0.937 | 0.915 | 1.089 | n/a | 751989010 | 751989010 | 450 | 0 | 234862427 |

## Per-run selected metrics

| Case | Method | Run | Elapsed s | Exit | ReadCompressedBytes | ReadBufferFromGCSBytes | ReadBufferFromS3Bytes | GCSReadObject | S3GetObject | GCS errors | S3 errors | User µs | System µs | Alloc bytes | Log |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---|
| native | read | 1 | 3.339 | 0 | 750484904 | 750484904 | 0 | 450 | 0 | 251 | 0 | 432187 | 197693 | 1509474956 | `tmp/gcs-grpc-s3-perf-parity/p01-benchmark-and-environment-contract/logs/native_read_run1.log` |
| native | read | 2 | 2.811 | 0 | 750527023 | 750527023 | 0 | 450 | 0 | 251 | 0 | 435156 | 147255 | 1709736840 | `tmp/gcs-grpc-s3-perf-parity/p01-benchmark-and-environment-contract/logs/native_read_run2.log` |
| native | read | 3 | 2.856 | 0 | 750484937 | 750484937 | 0 | 450 | 0 | 236 | 0 | 432189 | 149870 | 1679310608 | `tmp/gcs-grpc-s3-perf-parity/p01-benchmark-and-environment-contract/logs/native_read_run3.log` |
| native | threadpool | 1 | 1.775 | 0 | 751797078 | 751797078 | 0 | 450 | 0 | 328 | 0 | 357408 | 243627 | 519989916 | `tmp/gcs-grpc-s3-perf-parity/p01-benchmark-and-environment-contract/logs/native_threadpool_run1.log` |
| native | threadpool | 2 | 1.554 | 0 | 751881261 | 751881261 | 0 | 450 | 0 | 336 | 0 | 374341 | 147104 | 472948248 | `tmp/gcs-grpc-s3-perf-parity/p01-benchmark-and-environment-contract/logs/native_threadpool_run2.log` |
| native | threadpool | 3 | 1.559 | 0 | 751946971 | 751946971 | 0 | 450 | 0 | 337 | 0 | 371895 | 132022 | 423616764 | `tmp/gcs-grpc-s3-perf-parity/p01-benchmark-and-environment-contract/logs/native_threadpool_run3.log` |
| s3 | read | 1 | 2.876 | 0 | 750119102 | 0 | 750119102 | 0 | 450 | 0 | 0 | 1540485 | 1357533 | 2720648 | `tmp/gcs-grpc-s3-perf-parity/p01-benchmark-and-environment-contract/logs/s3_read_run1.log` |
| s3 | read | 2 | 2.921 | 0 | 749988696 | 0 | 749988696 | 0 | 450 | 0 | 0 | 1446437 | 1381226 | 8551932 | `tmp/gcs-grpc-s3-perf-parity/p01-benchmark-and-environment-contract/logs/s3_read_run2.log` |
| s3 | read | 3 | 2.947 | 0 | 750269272 | 0 | 750269272 | 0 | 450 | 0 | 0 | 1642465 | 1451551 | 2523592 | `tmp/gcs-grpc-s3-perf-parity/p01-benchmark-and-environment-contract/logs/s3_read_run3.log` |
| s3 | threadpool | 1 | 1.089 | 0 | 751989010 | 0 | 751989010 | 0 | 450 | 0 | 0 | 1581836 | 1935477 | 569317072 | `tmp/gcs-grpc-s3-perf-parity/p01-benchmark-and-environment-contract/logs/s3_threadpool_run1.log` |
| s3 | threadpool | 2 | 0.937 | 0 | 751989010 | 0 | 751989010 | 0 | 450 | 0 | 0 | 1146702 | 1933568 | 472570328 | `tmp/gcs-grpc-s3-perf-parity/p01-benchmark-and-environment-contract/logs/s3_threadpool_run2.log` |
| s3 | threadpool | 3 | 0.915 | 0 | 751989010 | 0 | 751989010 | 0 | 450 | 0 | 0 | 1157444 | 1968888 | 467649660 | `tmp/gcs-grpc-s3-perf-parity/p01-benchmark-and-environment-contract/logs/s3_threadpool_run3.log` |

## Interpretation

- Threadpool median native/S3 elapsed ratio: `1.664` (`1.559s` native vs `0.937s` S3).
- Direct read median native/S3 elapsed ratio: `0.978` (`2.856s` native vs `2.921s` S3).
- Selected P01 parity threshold remains native median elapsed time `<= 1.25x` GCS-as-`s3` median for later acceptance, but this P01 run is diagnostic-only for broad GCS gRPC claims because DirectPath status is not proven.
- Raw JSON data is in `benchmark-summary.json`.
