# Environment

Generated: 2026-05-12 23:29:07 UTC

## Local repository

Working directory: /Users/tanner/src/trees/github.com/ClickHouse/ClickHouse-gcs-grpc
Branch: gcs-grpc-coherent
HEAD: 75d3edcd106

```text
 m contrib/liburing
 m contrib/sysroot
 M src/Disks/DiskObjectStorage/ObjectStorages/GCS/GCSObjectStorage.cpp
 M src/IO/GCS/GCSClient.cpp
?? tmp/gcs-grpc-s3-perf-parity/p01-benchmark-and-environment-contract/environment.md
```

## Relevant dirty file classification inputs

```text
 .../ObjectStorages/GCS/GCSObjectStorage.cpp        | 53 +++++++++++---
 src/IO/GCS/GCSClient.cpp                           | 84 +++++++++++++++++++++-
 2 files changed, 124 insertions(+), 13 deletions(-)
 m contrib/liburing
 m contrib/sysroot
```

## Remote benchmark target

Command target: kubectl exec -n ch-builder clickhouse-builder-0 -- ...
Remote source tree: /work/ch-dev
Remote build directory: /work/ch-dev/build
Expected binary path: /work/ch-dev/build/programs/clickhouse

## Remote ClickHouse status

```text
host=clickhouse-builder-0
-rwxr-xr-x 1 root root 6627822496 May 12 23:07 /work/ch-dev/build/programs/clickhouse
ClickHouse client version 26.5.1.1.
26.5.1.1	E271638D5031FEEC49001FDC4D99F8858032C363
```
