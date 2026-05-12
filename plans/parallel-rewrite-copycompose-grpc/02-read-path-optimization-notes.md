# Read path optimization Notes

Plan: [plan.md](./plan.md)
Phase: P02 / `02-read-path-optimization`

## Implementation context

- Primary implementation target: `src/Disks/DiskObjectStorage/ObjectStorages/GCS/GCSObjectStorage.cpp`, especially `GCSReadBuffer`.
- Selected strategy: `GCSReadBuffer::nextImpl` uses a persistent sequential `ReadObject` stream/window instead of calling the range helper for every buffer refill.
- Known-size sequential reads use bounded windows capped by the remaining logical file size and by a read-ahead window. The window is at least `16 * remote_fs_buffer_size`, at least `prefetch_buffer_size`, and at least `read_hint` when provided.
- Unknown-size sequential reads use an unbounded stream unless `read_hint` or enabled remote prefetch gives a window larger than `remote_fs_buffer_size`.
- The normal sequential path copies `checksummed_data` chunks directly into the `ReadBufferFromFileBase` internal buffer and keeps only bounded overflow in `pending_data`; it does not accumulate the whole requested range in a `String` before filling the buffer.
- `readBigAt` remains independent from the sequential stream, clamps to known logical `file_size`, copies directly into the caller buffer, supports progress cancellation through `ClientContext::TryCancel`, and returns `0` at known logical EOF.
- `seek` resets sequential state only after finishing the active stream; non-OK stream `Finish` statuses during seek are surfaced as exceptions.
- `GCSObjectStorage::readObject` now passes `read_hint`, `remote_fs_prefetch`, `prefetch_buffer_size`, `remote_fs_buffer_size`, `remote_throttler`, and optional blob-storage logging into `GCSReadBuffer`.
- Existing native GCS read tests live in `src/Disks/tests/gtest_gcs_object_storage_config.cpp` under `GCSObjectStorageReadBuffer`; this phase expanded them for request counts, multi-window reads, seek reset, bounded `readBigAt`, cancellation, EOF, failures, and accounting.
- `GCS::FakeStub` records `read_object_requests` and serves object-map or scripted reads, which is sufficient to assert offsets, limits, request counts, finish failures, and short reads for Tier 1.

## Investigation context

- Investigation file: None
- Relevant findings: None from `plans/parallel-rewrite-copycompose-grpc/investigation.md`; related plan imports `gcs-grpc-perf` F001, F002, F003, F007, and F008 as evidence that native GCS read behavior is the important performance gap.
- Relevant constraints: preserve explicit native `object_storage_type=gcs`; do not change existing GCS-as-`s3` behavior; fail closed instead of silently falling back; do not implement `GCS*` profile events in this phase.
- Relevant assumptions validated: sequential scans can keep a stream/window alive until seek, EOF, range boundary, cancellation, or failure requires reinitialization; `seek`, `readBigAt`, bounded reads, EOF, and remote size contracts remain valid under fake tests.
- Relevant open questions/blockers: None blocking. Plan Q001 about exact level-D performance thresholds belongs to P06.

## Decisions from planning

- Optimize reads before writes/copy because read-heavy `MergeTree` scans are the performance-critical baseline for this plan.
- Keep the implementation under native GCS object storage only; do not change `ReadBufferFromS3` or GCS-as-`s3` behavior.
- Use fake/unit coverage for P02 because Tier 1 deterministic tests can prove read contracts, request counts, offsets, limits, cancellation, and error propagation without real GCS.
- Treat `GCS*` profile-event additions as external; P02 preserves existing read events and blob-storage logging but does not add new profile events.

## Assumptions

- Bounded sequential windows are safer than one full-object known-size request because they reduce per-refill request churn while bounding `pending_data` if gRPC delivers large response chunks. Confidence: high after `SequentialReadsUseSingleStreamAcrossBufferRefills` covers one-window and multi-window reads.
- `read_hint` and `remote_fs_prefetch` should bound unknown-size reads only when they produce a window larger than `remote_fs_buffer_size`; smaller values would not improve request churn. Confidence: medium; covered by `ReadHintAndPrefetchBoundSequentialWindows` for values larger than the buffer.
- `readBigAt` should keep using a separate range `ReadObject` request so random reads do not disturb sequential stream state. Confidence: high; covered by `RangeReadsAndEOF`.
- `remote_read_min_bytes_for_seek` is intentionally not used in P02 because native GCS has no cheap in-stream skip primitive with the direct synchronous reader; seek closes the current stream and opens a new offset stream. Confidence: medium; revisit only if P06 shows seek-heavy regressions.
- `io_scheduling`, `remote_fs_method`, filesystem cache, and page cache settings are intentionally deferred to P05 compatibility surfaces because this phase changes only the native GCS object-storage core read buffer. Confidence: medium.

## Risks

- Persistent stream/window reads can hide server `Finish` failures until EOF, seek, or destruction. Mitigation: `nextImpl` checks `Finish` at EOF/window boundaries, `seek` surfaces active-stream `Finish` failures, and destructor best-effort finishes with logged exceptions.
- Read-ahead windows can over-read relative to a caller that stops early. Mitigation: windows are bounded for known-size reads, and `readBigAt` supports cancellation via `TryCancel`.
- The fake stream returns simple response shapes; real GCS response chunking may differ. Mitigation: tests cover large fake chunks, bounded pending data behavior, cancellation, and multi-window offsets; P06 owns real cloud performance validation.
- Some `ReadSettings` remain intentionally unsupported in P02. Mitigation: deferred settings are documented above and P05 owns broader disk/cache compatibility.

## Deferred or future work

- P03 owns parallel compose-backed writes after P02 is complete.
- P04 owns native rewrite/copy behavior after P01.
- P05 owns compatibility validation across disk settings, caches, metadata modes, and profile-event readiness; trigger: P02-P04 implementations complete.
- P06 owns real GCS performance validation and direct-connectivity proof; trigger: P05 compatibility validation complete.

## Handoff summary

Current status:
- P02 implementation, Tier 1 verification, critique, reviewer all-clear, and task bookkeeping are complete; changes are ready for commit.

Completed artifacts:
- `src/Disks/DiskObjectStorage/ObjectStorages/GCS/GCSObjectStorage.cpp`: optimized native GCS sequential read path, bounded read windows, direct-copy `readBigAt`, cancellation, and failure propagation.
- `src/Disks/tests/gtest_gcs_object_storage_config.cpp`: expanded native GCS read-buffer tests for request counts, multi-window reads, settings windows, seek, bounded reads, cancellation, failures, and accounting.
- `plans/parallel-rewrite-copycompose-grpc/02-read-path-optimization-tasks.md`: completed task list.
- `plans/parallel-rewrite-copycompose-grpc/02-read-path-optimization-notes.md`: updated implementation notes.
- `plans/parallel-rewrite-copycompose-grpc/02-read-path-optimization-review.md`: verification, critique, findings, and reviewer all-clear.

Key decisions:
- Use persistent sequential `ReadObject` streams/windows for normal reads and keep random `readBigAt` range-based.
- Bound known-size sequential windows instead of requesting the full remaining object to cap pending memory while still reducing request churn.
- Reset seek by finishing the active stream and surfacing non-OK `Finish` statuses.

Assumptions:
- Sequential request churn can be reduced without breaking seek, `readBigAt`, bounded read, EOF, or remote-size contracts. Confidence: high after targeted tests.

Uncertainties:
- Real GCS chunk sizing and throughput impact remain for P06 same-region benchmark validation.

Next likely work:
- Commit P02 changes, then stop. Do not start P03 in this session.
