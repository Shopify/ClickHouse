# Autoresearch: GCS XML/native read-write wall-clock time

## Objective

Optimize ClickHouse object-storage read and write wall-clock time for native `GCS` XML multipart, tuned XML multipart, and native gRPC `GCS` paths. The benchmark runs on the `clickhouse-builder-0` pod in namespace `ch-builder` against the existing staging bucket and the representative `p04_real_part.core_n4_v0_base` source part created earlier.

The loop may change C++ implementation details, XML multipart settings, native gRPC client parameters, buffering/prefetch behavior, and benchmark XML policy variants. It must not cheat by skipping data, weakening correctness checks, using cached query results, or overfitting to one tiny benchmark shape.

## Metrics

- **Primary**: `total_seconds` (`s`, lower is better) — total wall-clock time for the benchmark suite.
- **Secondary**:
  - `real_write_seconds` — copy/upload the existing 23.59 GiB real part to `xml`, `xml_64m20`, and `native` disks.
  - `real_read_seconds` — read heavy string/map columns from the existing real-part target tables for `xml`, `xml_64m20`, and `native`.
  - `small_write_seconds` — copy/upload a local wide part with many small files to `xml`, `xml_64m20`, and `native`.
  - `small_read_seconds` — read from those small-file target tables.
  - Per-path timings emitted as extra `METRIC` lines.

## How to Run

```bash
./autoresearch.sh
```

The script copies in-scope source files to `clickhouse-builder-0`, builds `clickhouse` in `/work/ch-dev/build`, restarts the builder server, runs the benchmark suite, and outputs `METRIC name=value` lines.

## Files in Scope

Primary implementation files:

- `src/IO/GCS/GCSXMLClient.cpp` — native `GCS` XML multipart client and request behavior.
- `src/IO/GCS/GCSXMLClient.h` — XML client interfaces and policy plumbing.
- `src/Disks/DiskObjectStorage/ObjectStorages/GCS/GCSObjectStorage.cpp` — native `GCS` object storage read/write setup.
- `src/Disks/DiskObjectStorage/ObjectStorages/GCS/GCSObjectStorage.h` — native `GCS` object storage declarations.
- `src/IO/S3/PocoHTTPClient.cpp` — shared XML/S3 HTTP client behavior used by XML path.
- `src/IO/S3/PocoHTTPClient.h` — shared HTTP client declarations.
- `src/IO/WriteBufferFromS3.cpp` — multipart write buffering behavior shared by XML and S3-like paths.
- `src/Common/HTTPConnectionPool.cpp` — connection pool behavior when directly relevant.
- `src/Common/BufferAllocationPolicy.cpp` — buffer allocation policy when directly relevant.

Benchmark/control files:

- `autoresearch.md`
- `autoresearch.sh`
- `autoresearch.ideas.md`

## Off Limits

- Do not modify unrelated dirty submodules `contrib/liburing` or `contrib/sysroot`.
- Do not stage or commit `plans/` or `tmp/` local coordination artifacts.
- Do not delete the existing benchmark target database `p04_real_copy_readsrc_20260518T221002Z` unless explicitly asked.
- Do not change benchmark correctness checks to hide failures.
- Do not use sleep in C++ code to mask races.
- Do not use `ninja -j` or `nproc`; let `ninja` choose parallelism.

## Constraints

- Use the current branch/worktree only; do not create another worktree.
- Run tests/benchmarks on `clickhouse-builder-0` in namespace `ch-builder`.
- For C++ changes, run `clang-format`/`gofmt` equivalent as appropriate; for this repo use existing C++ style with Allman braces.
- Build output must go to a log file in the build directory.
- Be careful not to overfit: optimize mechanisms that plausibly help varied object-storage workloads, not just constants that exploit one query.
- Preserve correctness: copied parts must have expected rows and active part count; reads must execute real aggregations with caches disabled.

## Benchmark Workloads

1. **Real big-file write**: `ALTER TABLE ... ATTACH PARTITION ID '1778709600' FROM p04_real_part.core_n4_v0_base` into `xml`, `xml_64m20`, and `native` disks. This exercises multi-GB column files and many medium files.
2. **Real big-file read**: heavy read over `message`, `attrs.url.query`, `json_attributes`, `string_attributes`, and `string_array_attributes` on the kept target tables.
3. **Small-file write**: local wide source table with many columns and small files, copied to the three object-storage disks.
4. **Small-file read**: aggregation over all small-file columns from those target tables.

## What's Been Tried

- Baseline setup in progress. Previous manual profiling showed the large read path is dominated by `ReadFromMergeTree` and object read time, not expression execution.
- Manual copy benchmark showed `xml_64m20` is fastest for full real-part upload, while default `xml` or native paths can win different read shapes.
