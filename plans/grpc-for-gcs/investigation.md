# Native GCS gRPC disk investigation

Investigation status: ready

Plan slug: grpc-for-gcs

## User goal

Investigate the most practical implementation path for adding native Google Cloud Storage gRPC support to ClickHouse as a storage disk, motivated by performance.

## Feedback loop state

Iteration: 2

Questions asked directly so far:
- Q000, 2026-05-08/session: The goal field was blank; user selected implementation-path investigation and clarified: "I want to implement a native GCS grpc storage disk implementation for performance reasons." Impact: scope is biased toward a C++ native disk implementation rather than a generic feasibility brief.
- Q001, 2026-05-08/session: Asked whether native GCS should be an explicit new type or transparently upgrade existing GCS-as-`s3` disks. Answer: explicit new type; existing `type=s3` GCS behavior remains unchanged.
- Q002, 2026-05-08/session: Asked MVP surface. Answer: read/write `MergeTree` disk, `plain` metadata, `plain_rewritable` metadata, and `gcs` table function are in MVP. Backups/native copy and read-only-only milestone were not selected.
- Q003, 2026-05-08/session: Asked deployment/auth/performance envelope. Answer: optimize first for same-region Google Compute Engine to GCS buckets with service-account credentials/direct connectivity.

Current questions for the user:
- None.

User answers incorporated:
- Native GCS gRPC should be explicit opt-in, not a transparent upgrade of existing GCS-as-`s3` paths.
- MVP includes a production-oriented read/write disk surface plus `plain`, `plain_rewritable`, and table-function support.
- Performance validation should target the Google-documented direct-connectivity envelope.

Ready to stop asking: yes

Reason:
- The remaining uncertainties are technical validation items for `/phase-plan` and implementation phases, not user-answerable blockers. No further user question is likely to change the plan shape without becoming design-by-committee, and nobody needs that circus.

## Normalized problem statement

ClickHouse currently supports Google Cloud Storage mainly through the S3/XML compatibility path. The plan opportunity is to add an explicit native GCS gRPC implementation for better performance, preserving existing GCS-as-`s3` behavior while introducing a new native type that supports read/write `MergeTree` disks with `local`, `plain`, and `plain_rewritable` metadata plus a `gcs` table-function gRPC path. The first performance target is same-region Google Compute Engine to GCS buckets using service-account credentials and direct connectivity.

## User model

Stated request:
- Create or update only `plans/grpc-for-gcs/investigation.md` for `grpc-for-gcs`, based on ClickHouse issue 91012, and ask plan-shaping questions before readiness.
- User wants to implement a native GCS gRPC storage disk for performance reasons.
- User chose explicit new type, read/write `MergeTree` disk with `plain` and `plain_rewritable` metadata plus `gcs` table function in MVP, and GCE direct-connectivity target.

Inferred goal:
- Produce a plan-ready engineering brief that identifies the safest native implementation path, likely code areas, risks, compatibility boundaries, and validation strategy. Confidence: high.

Likely success criteria:
- A future plan can add an explicit native GCS gRPC disk path without regressing existing `s3`/`gcs` XML behavior. Confidence: high.
- The implementation can support read/write `MergeTree` disk workloads with `local`, `plain`, and `plain_rewritable` metadata. Confidence: high as user intent, medium as technical feasibility until design validation.
- The `gcs` table function can opt into or route to native gRPC without breaking the current S3/XML-compatible default. Confidence: high as user intent, medium as implementation feasibility.
- Performance validation is meaningful under same-region GCE-to-GCS direct-connectivity conditions. Confidence: high.

Hard constraints:
- Only `plans/grpc-for-gcs/investigation.md` may be created or updated. Source: user prompt.
- Do not create or modify `plan.md`, phase files, notes, reviews, implementation files, code changes, staging, or commits. Source: user prompt.
- Existing behavior must not be silently overwritten in the investigation; conflicts become blockers. Source: user prompt.
- Existing GCS-as-`s3` behavior must remain unchanged by default. Source: user answer Q001.
- Repository instructions require no rebase/amend, no master commits, no unrelated changes, Allman braces for future C++ work, and fail-close behavior over silent fallbacks. Source: `AGENTS.md`.

Soft preferences:
- User prefers an implementation-biased investigation over product-only ideation because they selected implementation path and added a native-disk goal. Confidence: high.
- User values performance evidence and benchmarking because performance was explicitly mentioned and direct-connectivity envelope was selected. Confidence: high.
- User is willing to accept a larger MVP because they selected read/write disk, `plain`, `plain_rewritable`, and table-function support. Confidence: high.

Authority boundaries:
- Allowed: inspect repository/docs/external sources, create/update `plans/grpc-for-gcs/investigation.md`, ask clarifying questions.
- Forbidden: source changes, implementation, task decomposition, phase plan creation, staging, committing.
- Unclear: exact config spelling (`gcs` vs `gcs_grpc`, table-function argument name, metric names). These are technical design choices for `/phase-plan`, not blockers.

Likely user assumptions:
- Native GCS gRPC can fit into ClickHouse as a disk implementation. Evidence: issue 91012 and user's clarification. Confidence: medium; validation requires detailed design and build proof.
- `google-cloud-cpp` gRPC storage support can be used from ClickHouse. Evidence: issue body and Google docs. Confidence: medium; repository wrapper currently appears REST-storage oriented and needs validation.
- Performance gains are expected from gRPC/direct connectivity. Evidence: user statement, user selected GCE direct path, and Google docs. Confidence: high for target envelope; exact magnitude requires benchmarking.

## Investigation scope

In scope:
- Current ClickHouse GCS support and how it maps to S3/XML compatibility.
- Disk/object storage extension points and compatibility surfaces.
- Existing `google-cloud-cpp`, gRPC, protobuf, and Google APIs build plumbing.
- External Google documentation for Cloud Storage gRPC and direct connectivity.
- Candidate implementation directions and plan-shaping decisions.

Out of scope:
- Implementing code, modifying tests, changing docs outside this artifact, compiling ClickHouse, or benchmarking.
- Full implementation task breakdown; that belongs in `/phase-plan` and later phase/task prompts.

## Executive findings

- F001: Current ClickHouse GCS user-facing support is primarily an S3/XML compatibility path, not native GCS gRPC.
  Evidence: `docs/en/sql-reference/table-functions/gcs.md` says `gcs` is an alias of `s3`; `docs/en/operations/storing-data.md` says GCS disks use type `s3`; `registerStorageGCS` calls `registerStorageS3Impl` in `src/Storages/ObjectStorage/registerStorageObjectStorage.cpp`.
  Confidence: high
  Plan impact: Preserve existing `s3`/`gcs` compatibility by default; add native gRPC as explicit opt-in.

- F002: The cleanest ClickHouse extension point is a new native `IObjectStorage` implementation behind an explicit `object_storage_type`.
  Evidence: `IObjectStorage` defines provider operations; `ObjectStorageFactory` dispatches by `object_storage_type`; `RegisterDiskObjectStorage` composes object storage with metadata storage.
  Confidence: high
  Plan impact: Plan should center on `GCSObjectStorage` under `src/Disks/DiskObjectStorage/ObjectStorages/GCS/`, not on mutating `S3ObjectStorage` into a dual-protocol class.

- F003: Adding a distinct `ObjectStorageType::GCS` has non-trivial compatibility reach.
  Evidence: `DiskType.h` lacks `GCS`; `DiskType.cpp`, `DiskObjectStorage::isSharedCompatible`, `MetadataStorageFactory::getCompatibilityMetadataTypeHint`, and `PlainRewritableMetrics.cpp` switch on `ObjectStorageType`.
  Confidence: high
  Plan impact: Phase planning must include identity/system-table/metadata compatibility before implementation.

- F004: ClickHouse already vendors and wraps `google-cloud-cpp`, gRPC, protobuf, and Google APIs, but the current storage wrapper is REST-oriented.
  Evidence: `contrib/CMakeLists.txt` enables `ENABLE_GOOGLE_CLOUD_CPP`; `contrib/google-cloud-cpp-cmake/CMakeLists.txt` requires gRPC/protobuf and builds storage protos; `google_cloud_cpp_storage.cmake` links `rest_internal` and `CURL::libcurl`; `src/CMakeLists.txt` links `ch_contrib::google_cloud_cpp` when present.
  Confidence: medium
  Plan impact: Plan should include early validation around whether to use `google::cloud::storage::MakeGrpcClient` through an extended wrapper or generated `google.storage.v2` stubs directly.

- F005: Google documents GCS gRPC performance benefits as environment-dependent.
  Evidence: Google Cloud docs state gRPC improves Cloud Storage read performance for analytics when the Compute Engine instance and bucket are in the same region; direct connectivity is only available from Compute Engine VMs, requires attached service account, routes/firewall, and supported endpoints.
  Confidence: high
  Plan impact: Acceptance criteria and benchmarks should target same-region GCE/bucket direct connectivity, as confirmed by the user.

- F006: Existing S3 code contains GCS-specific workarounds that a native path must intentionally replace or preserve.
  Evidence: `S3Capabilities.h` notes missing GCS `DeleteObjects` and `UploadPartCopy`; `S3ObjectStorage.h` documents single-delete fallback; `S3::Client` detects `storage.googleapis.com` and switches GCS header behavior; OAuth bearer token support exists in `PocoHTTPClientGCPOAuth` and `GCPOAuth`.
  Confidence: high
  Plan impact: Native GCS must define delete, copy/rewrite, auth refresh, request settings, and error mapping rather than assuming S3 semantics carry over.

- F007: The MVP is broader than disk-only.
  Evidence: User selected read/write `MergeTree` disk, `plain`, `plain_rewritable`, and `gcs` table function in Q002.
  Confidence: high
  Plan impact: `/phase-plan` should include a table-function compatibility/design phase and metadata modes; backup/native copy can be deferred unless needed by disk semantics.

## Evidence and belief register

| ID | Claim | Type | Evidence | Confidence | Would change if |
|---|---|---|---|---|---|
| B001 | Existing `gcs` table function is an alias over `s3` and uses the XML API/HMAC model. | fact | `docs/en/sql-reference/table-functions/gcs.md` lines 13-29 | high | Docs or source show a separate native GCS implementation elsewhere. |
| B002 | GCS-backed disks currently use `type=s3` / `object_storage_type=s3`. | fact | `docs/en/operations/storing-data.md` lines 330-371 | high | A hidden `gcs` disk factory registration is found. |
| B003 | There is no `ObjectStorageType::GCS` today. | fact | `src/Disks/DiskType.h` lines 15-25 | high | Another enum or newer branch adds it. |
| B004 | `ObjectStorageFactory` is the disk object-storage provider registry. | fact | `src/Disks/DiskObjectStorage/ObjectStorages/ObjectStorageFactory.cpp` lines 69-95 and 273-291 | high | Disk config bypasses this registry for GCS, not observed. |
| B005 | A native GCS disk can fit the existing `IObjectStorage` abstraction. | inference | `IObjectStorage.h` required operations and current S3/Azure/Local implementations | medium | GCS gRPC cannot provide required range/read/write/list/delete semantics without unacceptable gaps. |
| B006 | A new explicit native object storage type is preferred over silently altering S3 behavior. | user-stated preference | User answer Q001 | high | User reverses the decision. |
| B007 | `google-cloud-cpp` storage wrapper currently looks REST-focused, not storage gRPC. | fact | `contrib/google-cloud-cpp-cmake/storage/google_cloud_cpp_storage.cmake` includes `internal/rest` and links `CURL::libcurl` | medium | The pinned submodule contains enabled `storage_grpc` targets not visible in this checkout or wrapper. |
| B008 | GCS gRPC performance benefits require specific GCP environment conditions. | fact | Google Cloud `enable-grpc-api` and `direct-connectivity` docs | high | Google changes direct connectivity requirements. |
| B009 | Initial implementation should fail closed if native gRPC is unavailable instead of silently falling back to S3/XML. | constraint | `AGENTS.md` fail-close instruction and compatibility risk | high | User explicitly requests fallback behavior. |
| B010 | A Rust plugin/sidecar is not the preferred first implementation path. | inference | No repo object-storage plugin point observed; static C++ disk abstractions exist | high | User or maintainers request Rust. |
| B011 | MVP includes read/write disk, `plain`, `plain_rewritable`, and `gcs` table function; backup/native copy is not MVP unless required by core semantics. | user-stated preference | User answer Q002 | high | User expands or narrows MVP later. |
| B012 | First performance envelope is same-region GCE-to-GCS direct connectivity with service-account credentials. | user-stated preference | User answer Q003 and Google docs | high | User needs broader first-platform support. |

## Repository context inspected

| Path / command | Why inspected | Key findings | Confidence |
|---|---|---|---|
| `git status --porcelain=v1`; `git branch --show-current` | Startup safety | Branch is `gcs-grpc`; no unrelated tracked changes were reported before writing this artifact. | high |
| `AGENTS.md` | Project instructions | No commits to master, no rebase/amend, C++ style constraints, fail-close preference, only `tmp` for temp files. | high |
| `README.md` | Project overview | General ClickHouse repository; no GCS-specific direction. | high |
| `plans/` and `plans/grpc-for-gcs/` | Existing plan/investigation check | No existing plan directory contents before this artifact. | high |
| `https://github.com/ClickHouse/ClickHouse/issues/91012` | User-supplied source issue | Issue proposes GCS storage via gRPC, adding `google-cloud-cpp` storage gRPC package, and asks whether Rust/plugin is preferred. | high |
| `docs/en/sql-reference/table-functions/gcs.md` | Current GCS docs | `gcs` is documented as an `s3` alias using GCS XML API and HMAC keys. | high |
| `docs/en/operations/storing-data.md` | Disk docs | External storage supports `s3`, `azure_blob_storage`, `hdfs`, `local_blob_storage`, `web`; GCS is supported using `s3`; `support_batch_delete=false` is recommended for GCS. | high |
| `src/Storages/ObjectStorage/StorageObjectStorageDefinitions.h` | GCS table engine/function definitions | `GCSDefinition` exists for table engine/function naming but not as disk object storage type. | high |
| `src/Storages/ObjectStorage/registerStorageObjectStorage.cpp` | Table engine registration | `registerStorageGCS` delegates to S3 registration. | high |
| `src/TableFunctions/TableFunctionObjectStorage.cpp` | Table function registration | `gcs` uses `TableFunctionObjectStorage<GCSDefinition, StorageS3Configuration>`. | high |
| `src/Disks/DiskType.h`; `src/Disks/DiskType.cpp` | Disk identity | `ObjectStorageType` lacks `GCS`; names and `sameKind` must be updated if a distinct type is added. | high |
| `src/Disks/DiskObjectStorage/ObjectStorages/IObjectStorage.h` | Provider contract | Native GCS must implement read, write, list, metadata, delete, copy, lifecycle, settings, namespace, and key generation. | high |
| `src/Disks/DiskObjectStorage/ObjectStorages/ObjectStorageFactory.cpp` | Provider registration | Current providers are S3, HDFS, Azure, Web, and Local. | high |
| `src/Disks/DiskObjectStorage/RegisterDiskObjectStorage.cpp` | Disk assembly | Generic `object_storage` composes object storage with metadata storage and supports multiple locations. | high |
| `src/Disks/DiskObjectStorage/ObjectStorages/S3/S3ObjectStorage.*` | Existing comparable implementation | S3 implementation is AWS SDK/S3 request-type heavy; GCS gRPC inside it would be a conditional dual protocol. | high |
| `src/IO/S3/*`; `src/IO/GCPOAuth.*` | Current GCS compatibility/auth | GCS provider detection, header mode, OAuth bearer token acquisition, delete/copy workarounds exist in S3 path. | high |
| `contrib/CMakeLists.txt`; `contrib/google-cloud-cpp-cmake/CMakeLists.txt`; `contrib/google-cloud-cpp-cmake/storage/google_cloud_cpp_storage.cmake`; `src/CMakeLists.txt` | Build/dependency state | Google Cloud C++ is optional and linked when available; wrapper currently builds REST storage and storage protos, with gRPC/protobuf prerequisites. | medium |
| `.gitmodules`; `git submodule status -- contrib/google-cloud-cpp` | Submodule state | `google-cloud-cpp` is a submodule at commit `83f30c...` but not initialized in this checkout. | high |
| Subagent scout/critic/analyst outputs | Independent repo lenses | Confirmed same major findings: current GCS is S3/XML, native `IObjectStorage` is likely best, and explicit config/compatibility/auth decisions were plan-shaping. | medium |

## External research consulted

| Source | Why consulted | Key findings | Plan impact |
|---|---|---|---|
| `https://github.com/ClickHouse/ClickHouse/issues/91012` | User-supplied idea source | Issue from Shopify asks about contributing GCS storage via gRPC and `google-cloud-cpp` storage gRPC package, versus Rust/plugin. | Anchors user goal and alternative set. |
| `https://docs.cloud.google.com/storage/docs/enable-grpc-api` | GCS gRPC support and limitations | C++ requires gRPC 1.65.1+, google-cloud-cpp v2.30.0+, C++14; use `gcs::MakeGrpcClient`; direct connectivity is automatic on Google Cloud; gRPC improves analytics reads only when VM and bucket are same-region; gRPC lacks notifications, hmacKeys, serviceAccount methods and regional endpoints. | Defines environment constraints and unsupported-operation risks. |
| `https://docs.cloud.google.com/storage/docs/direct-connectivity` | Performance prerequisites | Direct connectivity requires Compute Engine VMs, attached service account, co-located bucket/VM, routes/firewall for `34.126.0.0/18` and `2001:4860:8040::/42`, and endpoints `storage.googleapis.com:443` and `directpath-pa.googleapis.com:443`; not supported with Private Service Connect. | Benchmark and acceptance criteria should target the confirmed environment. |
| `https://github.com/googleapis/google-cloud-cpp/blob/main/google/cloud/storage/quickstart/README.md` | C++ storage gRPC packaging | `storage_grpc` plugin is disabled by default in CMake; enable with `GOOGLE_CLOUD_CPP_STORAGE_ENABLE_GRPC=ON`; link additional library and use a different client initialization; benefits mostly very large workloads. | Plan must validate or extend ClickHouse's wrapper, not assume current target exposes `storage_grpc`. |

## Current state

ClickHouse currently exposes GCS primarily through S3-compatible HTTP/XML paths. The `gcs` table function and `GCS` storage engine are wrappers around the S3 implementation and `StorageS3Configuration`; GCS disk documentation points users to `type=s3`. Internally, the S3 client contains GCS-specific compatibility handling for endpoint detection, headers, OAuth bearer tokens, unsupported batch delete, compose/copy behavior, and error handling.

The disk architecture is more provider-neutral than the current GCS implementation. `DiskObjectStorage` composes an `IObjectStorage` implementation with a metadata storage implementation. `ObjectStorageFactory` already registers provider-specific object storages. Adding native GCS gRPC as a new `IObjectStorage` implementation fits this design, but a distinct `ObjectStorageType::GCS` ripples through disk identity, system tables, shared compatibility, metadata defaults, metrics, and backup/copy behavior.

The repository already has optional Google Cloud C++ build plumbing and Google storage protos, but this checkout's `google-cloud-cpp` submodule is not initialized and the visible wrapper builds REST storage rather than a storage gRPC target. The implementation path must validate whether to enable the upstream `storage_grpc` client through the wrapper or to use generated `google.storage.v2` gRPC stubs directly.

## Compatibility surface

| Surface | Current behavior | Compatibility risk | Plan implication |
|---|---|---|---|
| `gcs` table function | Alias over `s3` table function using `StorageS3Configuration` and XML API/HMAC docs. | Hidden gRPC switch could break URL/auth semantics and user expectations. | MVP includes table-function support, but it should be explicit and preserve current default behavior. |
| `GCS` storage engine | Registered through S3 implementation. | Same as table function; also schema inference/access settings rely on S3 path. | Decide whether table-function work also changes storage engine routing or only the table function. |
| Disk config `type=s3` for GCS | Documented GCS disk path via S3-compatible endpoint. | Transparent upgrade may alter auth, metrics, copy/delete behavior, and compatibility with existing data. | Keep unchanged; native GCS uses explicit new type. |
| `ObjectStorageType` | Enum has `S3`, `Azure`, `HDFS`, `Web`, `Local`, no `GCS`. | Adding `GCS` requires switch-site updates and system-table semantics. | Include identity compatibility in phase plan. |
| `IObjectStorage` | Provider contract for disk operations. | Missing operation support causes runtime exceptions in `MergeTree`, backups, or metadata. | MVP must satisfy read/write disk operations and metadata modes. |
| Metadata storage (`local`, `plain`, `plain_rewritable`, `keeper`, `web`) | Provider-agnostic but uses object storage type, prefixes, key generator, and provider metrics. | Native GCS key layout or type identity could make existing GCS-as-S3 metadata unreadable if implicitly migrated. | No implicit migration; support `local`, `plain`, and `plain_rewritable` for native type. |
| S3 GCS compatibility code | Handles GCS headers, OAuth, no batch delete, compose/copy behavior. | Native path may lose hard-won compatibility if not mapped deliberately. | Inventory and reimplement/replace relevant semantics. |
| Build flags | `ENABLE_GOOGLE_CLOUD_CPP` optional and depends on `ENABLE_GRPC`/`ENABLE_PROTOBUF`; `USE_GOOGLE_CLOUD` is compile-time gated. | Native disk may be unavailable in some builds/platforms. | Guard with `USE_GOOGLE_CLOUD`; fail closed with clear error. |
| Metrics/logs | Existing remote disk metrics are S3/Azure/Local-oriented. | Reusing S3 metrics hides GCS; new metrics affect dashboards. | Treat as technical design choice in `/phase-plan`; default to distinct GCS identity where adding a new type already requires switch updates. |
| Backups/native copy | S3 and Azure have dedicated backup IO/copy paths. | New GCS type may fall back to buffered copy or break same-kind optimizations. | Native backup/copy is not selected as MVP; preserve buffered correctness and defer native optimization unless required. |

## Constraints

- C001: Preserve existing documented GCS-as-`s3` behavior.
  Source: `docs/en/sql-reference/table-functions/gcs.md`, `docs/en/operations/storing-data.md`, user answer Q001.
  Impact: Native gRPC should be explicit opt-in in the recommended plan.

- C002: Native implementation must satisfy the `IObjectStorage` contract for read/write `MergeTree` disk, `local`, `plain`, and `plain_rewritable` metadata in MVP.
  Source: `src/Disks/DiskObjectStorage/ObjectStorages/IObjectStorage.h`, user answer Q002.
  Impact: MVP scope cannot hand-wave unsupported operations.

- C003: Native gRPC availability is build-gated.
  Source: `contrib/google-cloud-cpp-cmake/CMakeLists.txt`, `src/configure_config.cmake`, `src/Common/config.h.in`.
  Impact: Plan must include compile-time guards and clear unsupported-build behavior.

- C004: Performance claims must be tied to same-region GCE-to-GCS direct connectivity.
  Source: Google Cloud gRPC/direct connectivity docs and user answer Q003.
  Impact: Benchmarks should target same-region Compute Engine and bucket direct-connectivity conditions.

- C005: Avoid silent fallback paths.
  Source: `AGENTS.md`.
  Impact: If native gRPC cannot initialize or direct connectivity assumptions are not met, the initial design should surface the condition rather than silently use S3/XML for consequential disk operations.

- C006: This prompt may only update `plans/grpc-for-gcs/investigation.md`.
  Source: user prompt.
  Impact: Implementation planning and code changes are deferred.

## Non-goals

- Implementing `GCSObjectStorage`, CMake changes, tests, docs, or benchmarks in this phase.
- Changing existing `s3` or `gcs` user behavior during investigation.
- Producing a phase task checklist or executable implementation plan.
- Designing a Rust sidecar/plugin as the primary path unless the user or maintainers explicitly request it.
- Making GCS-native backup/copy optimization part of MVP unless `/phase-plan` finds it necessary for correctness.

## Grey areas

- G001: Configuration spelling and exact user-facing knobs for explicit native GCS.
  Severity: amber
  Blocking: no
  Why it matters: `object_storage_type=gcs`, `object_storage_type=gcs_grpc`, or another explicit spelling affects docs and compatibility but not the high-level direction.
  Resolution path: Decide in `/phase-plan` after checking naming conventions and maintainer expectations.
  Safe default if non-blocking: Use `object_storage_type=gcs` for native disk and an explicit table-function setting for gRPC.

- G002: Table-function gRPC integration boundary.
  Severity: amber
  Blocking: no
  Why it matters: User selected `gcs` table function in MVP, but current `gcs` is an S3 alias; implementation must avoid breaking default behavior.
  Resolution path: `/phase-plan` should compare explicit argument/setting/named-collection key versus separate internal configuration path.
  Safe default if non-blocking: Add explicit opt-in transport selection; keep current `gcs` behavior default.

- G003: Whether to use upstream `storage_grpc` high-level client or generated gRPC stubs directly.
  Severity: amber
  Blocking: no
  Why it matters: Affects build integration, retry/auth behavior, and implementation complexity.
  Resolution path: During planning, validate the pinned `google-cloud-cpp` submodule and wrapper after submodule initialization or via source inspection.
  Safe default if non-blocking: Plan a validation spike before committing to either client layer.

- G004: Exact GCS gRPC semantics for `compose`, `rewrite`, generation preconditions, object attributes/tags, and resumable writes.
  Severity: amber
  Blocking: no
  Why it matters: These map to copy/write/delete guarantees and future backup performance.
  Resolution path: API design/prototype in plan; consult generated proto and Google docs.
  Safe default if non-blocking: Start with conservative operation support and fail closed on unsupported advanced operations.

- G005: Hermetic testing strategy for gRPC.
  Severity: green
  Blocking: no
  Why it matters: Real GCS tests need credentials/environment; mocks may miss direct-connectivity and auth behavior.
  Resolution path: Plan for unit tests plus fake gRPC service; mark real-GCS benchmark as optional/manual or environment-gated.
  Safe default if non-blocking: Use fake/mock gRPC service for correctness and a real same-region GCE benchmark for performance validation when available.

- G006: Observability naming for native GCS metrics/log categories.
  Severity: green
  Blocking: no
  Why it matters: Affects dashboards and system-table clarity.
  Resolution path: Decide in `/phase-plan` using existing metric conventions.
  Safe default if non-blocking: Prefer distinct GCS identity for new type; reuse generic object-storage metrics where provider-specific additions are not required.

## Assumptions

- AS001: The first implementation should be native C++ under existing `DiskObjectStorage` rather than a Rust plugin or sidecar.
  Confidence: high
  Validation path: Confirm with maintainer feedback on issue/PR.
  Exported to plan: yes

- AS002: Existing `s3`/`gcs` XML behavior should remain unchanged by default.
  Confidence: high
  Validation path: User Q001 confirmed; future plan should cite docs/tests requiring compatibility.
  Exported to plan: yes

- AS003: A distinct native GCS identity is preferable for clarity even if it requires enum/switch updates.
  Confidence: high
  Validation path: User Q001 confirmed explicit new type; exact naming remains for plan.
  Exported to plan: yes

- AS004: The performance target is same-region GCE-to-GCS direct connectivity, not generic internet GCS access.
  Confidence: high
  Validation path: User Q003 confirmed; performance phase should benchmark this envelope.
  Exported to plan: yes

- AS005: Table-function support is MVP, but current table-function default behavior must remain compatible.
  Confidence: high
  Validation path: User Q002 confirmed table function; `/phase-plan` must choose explicit opt-in UX.
  Exported to plan: yes

- AS006: Native backup/copy optimization is not MVP, but buffered correctness should continue through generic disk/object-storage paths.
  Confidence: medium
  Validation path: User did not select backups/native copy; `/phase-plan` should verify no correctness dependency.
  Exported to plan: yes

## Open questions

- [x] Q001: Should the first planned implementation be an explicit new native disk/object storage type, leaving existing GCS-as-`s3` behavior unchanged, or should it attempt to transparently upgrade existing GCS `s3` disks to gRPC when possible?
  Blocking: no
  Plan-shaping: yes
  Asked directly in chat: yes
  Answer: Explicit new type; leave existing GCS-as-`s3` behavior unchanged.
  Why it matters: Determines config UX, migration risk, compatibility requirements, and whether fallback behavior is allowed.
  Suggested resolution path: Resolved by user answer; `/phase-plan` should pick exact config spelling.

- [x] Q002: What is the MVP surface for phase planning: full read/write `MergeTree` disk only, read-only first, or also `plain`, `plain_rewritable`, backups/native copy, and the `gcs` table function?
  Blocking: no
  Plan-shaping: yes
  Asked directly in chat: yes
  Answer: MVP includes read/write `MergeTree` disk, `plain`, `plain_rewritable`, and `gcs` table function. Backups/native copy was not selected.
  Why it matters: Determines required `IObjectStorage` operations, metadata support, tests, docs, and rollout size.
  Suggested resolution path: Resolved by user answer; `/phase-plan` should scope backups/native copy as deferred unless needed for correctness.

- [x] Q003: Which deployment/auth/performance envelope should the plan optimize for first: Google Compute Engine with same-region buckets and service-account credentials for direct connectivity, or broader GCS access including ADC refresh-token files, public/no-sign buckets, non-GCE hosts, and HMAC/XML compatibility?
  Blocking: no
  Plan-shaping: yes
  Asked directly in chat: yes
  Answer: Optimize first for same-region GCE direct path with service-account credentials/direct connectivity.
  Why it matters: Determines auth scope, benchmark design, and whether direct connectivity can be an acceptance criterion.
  Suggested resolution path: Resolved by user answer; `/phase-plan` should keep broader auth as future or optional unless cheap via selected client layer.

- [ ] Q004: What exact native type/table-function setting names should be used?
  Blocking: no
  Plan-shaping: no
  Asked directly in chat: no
  Why it matters: Affects docs and UX, but can be decided from repository naming conventions and maintainer review.
  Suggested resolution path: Resolve in `/phase-plan`; default to `object_storage_type=gcs` plus explicit table-function transport setting.

## Candidate approaches

### Option A: Explicit native `GCSObjectStorage` under `IObjectStorage`

Summary:
Add a new C++ `IObjectStorage` implementation for native GCS gRPC, registered through an explicit `object_storage_type` such as `gcs` or `gcs_grpc`, while preserving existing `s3`/`gcs` XML behavior.

Pros:
- Fits existing `DiskObjectStorage` architecture.
- Keeps protocol-specific logic isolated and testable.
- Preserves existing compatibility by default.
- Gives accurate disk identity and future observability surface.
- Matches the user's chosen explicit-new-type direction.

Cons:
- Requires enum/switch/system-table/metadata/metrics updates.
- Requires new read/write/list/delete/copy/auth/error mapping implementation.
- Requires build wrapper validation for `storage_grpc` or direct stubs.
- MVP is broad because it includes `plain`, `plain_rewritable`, and table-function support.

Risks:
- Under-scoped implementation could fail later in `MergeTree` metadata or table-function paths.
- Native GCS gRPC limitations may not match existing S3 compatibility behavior.

Best when:
- The goal is a production-quality native disk backend with explicit opt-in and maintainable protocol boundaries.

### Option B: Add gRPC transport branch inside existing `S3ObjectStorage`

Summary:
Keep GCS as an S3-style disk/config and internally switch to gRPC for GCS endpoints or settings.

Pros:
- Smaller apparent config change.
- Reuses some existing GCS auth/settings compatibility.
- May avoid adding a new `ObjectStorageType` initially.

Cons:
- Conflicts with the user's explicit-new-type choice.
- `S3ObjectStorage` is deeply tied to AWS SDK request/response types.
- Risks hidden behavior changes for existing users.
- Metrics/system tables still report S3 unless more changes are made.
- Fallback semantics become tempting and risky.

Risks:
- Dual-protocol class becomes difficult to reason about, test, and review.
- Existing GCS S3/XML compatibility regressions could be subtle.

Best when:
- Maintainers explicitly reject a new object storage type and require compatibility under `s3` config.

### Option C: Use `google::cloud::storage::Client` high-level gRPC plugin directly

Summary:
Expose the upstream `storage_grpc` client through ClickHouse's `google-cloud-cpp` wrapper and implement GCS operations using high-level `google::cloud::storage` APIs.

Pros:
- Potentially less low-level gRPC/proto code.
- Upstream client owns auth/retry/channel behavior.
- Aligns with Google documentation using `MakeGrpcClient`.

Cons:
- Current ClickHouse wrapper visibly builds REST storage and links curl; `storage_grpc` target is not evident.
- High-level client may not expose enough control for ClickHouse read buffers, deadlines, throttling, generation preconditions, or observability.

Risks:
- Could accidentally implement REST rather than gRPC if wrapper integration is misunderstood.
- Build/link size and dependency impact may grow.

Best when:
- Validation proves the pinned submodule and wrapper can provide `storage_grpc` cleanly with the needed control surface.

### Option D: Use generated `google.storage.v2` gRPC stubs directly

Summary:
Build on existing Google APIs/protobuf/gRPC infrastructure and implement the storage API calls directly against generated `google.storage.v2.Storage` stubs.

Pros:
- Guarantees native gRPC transport.
- Maximum control over streaming reads/writes, deadlines, retries, and error mapping.
- Avoids needing the high-level `storage_grpc` wrapper if unavailable.

Cons:
- More implementation work.
- Must design auth/channel setup, retries, and observability carefully.
- More direct exposure to GCS API details.

Risks:
- Reimplementing client-library behavior incorrectly could harm reliability.

Best when:
- High-level `storage_grpc` integration is unavailable or insufficient for ClickHouse's disk semantics.

### Option E: Rust plugin or sidecar

Summary:
Prototype or implement GCS gRPC access outside the core C++ disk abstractions.

Pros:
- Rust ecosystem has strong gRPC tooling.
- Could be useful for an isolated experiment.

Cons:
- No observed object-storage plugin boundary in repository.
- Sidecar adds operational complexity and bypasses ClickHouse disk lifecycle, cache, metrics, and retry conventions.
- Harder to upstream into ClickHouse core.
- Does not match the user's native disk direction.

Risks:
- Maintainers may reject it as architecture drift.

Best when:
- This is only a non-upstream performance prototype, not a native ClickHouse disk implementation.

## Recommended planning direction

Recommendation:
- Plan Option A as the main direction: an explicit native C++ `GCSObjectStorage` implementing `IObjectStorage`, registered as a new object storage type, guarded by `USE_GOOGLE_CLOUD`, preserving existing GCS-as-`s3` behavior by default.
- Include read/write `MergeTree` disk support, `local`, `plain`, and `plain_rewritable` metadata, and explicit `gcs` table-function gRPC support in MVP.
- Include an early validation decision between Option C and Option D for the actual gRPC client layer.
- Target same-region GCE-to-GCS direct connectivity with service-account credentials for performance validation.

Rationale:
- Repository architecture already separates disk metadata from provider-specific `IObjectStorage` operations.
- Existing GCS behavior is clearly documented and implemented as S3/XML compatibility, so silent mutation is risky.
- User confirmed explicit new type and a GCE direct-connectivity envelope.
- Google gRPC performance benefits are real but environment-dependent, so explicit opt-in and targeted benchmarking are cleaner.

Avoid:
- Do not transparently switch existing `type=s3` GCS disks to gRPC.
- Do not add silent S3/XML fallback inside native GCS for disk operations.
- Do not bury gRPC implementation behind S3 AWS SDK request types unless maintainers require it.
- Do not include GCS-native backup/copy optimization in MVP unless `/phase-plan` finds it necessary for correctness.

## Suggested plan shape

Potential phases:
- P01 / `client-layer-validation`: Validate pinned `google-cloud-cpp`/protobuf/gRPC build path and choose high-level `storage_grpc` client versus direct generated stubs.
- P02 / `native-type-and-config`: Define explicit GCS disk config, table-function opt-in UX, `ObjectStorageType` identity, build guards, and factory registration boundaries.
- P03 / `core-rw-object-storage`: Implement native read/write/list/metadata/delete behavior needed for `MergeTree` disk with local metadata.
- P04 / `metadata-modes`: Make native GCS work with `plain` and `plain_rewritable` metadata, including metrics and key-prefix compatibility.
- P05 / `gcs-table-function`: Add explicit native gRPC routing for `gcs` table-function usage while preserving current default behavior.
- P06 / `compatibility-and-regression`: Ensure existing S3/GCS XML paths, system tables, logs, and generic backup/copy behavior remain correct.
- P07 / `verification-and-performance`: Add correctness tests and performance validation for the same-region GCE direct-connectivity envelope.

Expected artifacts:
- `src/Disks/DiskObjectStorage/ObjectStorages/GCS/`: likely home for native object storage implementation if Option A is chosen.
- `src/IO/GCS/` or equivalent: likely home for auth/client/request helpers if direct stubs are used.
- `contrib/google-cloud-cpp-cmake/`: likely wrapper changes if upstream `storage_grpc` target is used.
- `docs/en/operations/storing-data.md` and `docs/en/sql-reference/table-functions/gcs.md`: likely future docs if user-facing config/table-function transport is added; not touched by this investigation.
- Tests under `src/Disks/tests/`, `src/IO/tests/`, and possibly `tests/integration/`: likely future verification surfaces.

Verification candidates:
- Unit tests for URI/bucket/key parsing, metadata mapping, error mapping, auth-token setup, and object-storage contract behavior.
- Fake gRPC service tests for read/list/write/delete without real GCS credentials.
- Existing S3/GCS XML regression tests to ensure no behavior changes.
- `plain` and `plain_rewritable` metadata tests for native GCS.
- Table-function tests proving default `gcs` behavior remains compatible and explicit gRPC path works.
- Optional or environment-gated real GCS performance benchmark under same-region Compute Engine/bucket direct-connectivity conditions.

## Handoff contract for phase-plan

Facts phase-plan may rely on:
- Existing GCS support is S3/XML compatibility, not native gRPC. Evidence: B001, B002, F001.
- `IObjectStorage`/`ObjectStorageFactory` is the correct disk-provider extension seam. Evidence: B004, F002.
- Adding a distinct native GCS type touches disk identity, metadata defaults, metrics, and shared compatibility. Evidence: B003, F003.
- Native GCS should be explicit new type; existing `type=s3` GCS behavior remains unchanged. Evidence: Q001/B006.
- MVP includes read/write disk, `plain`, `plain_rewritable`, and `gcs` table function. Evidence: Q002/B011.
- First performance envelope is same-region GCE-to-GCS direct connectivity with service-account credentials. Evidence: Q003/B012.

Facts phase-plan must revalidate:
- Whether the pinned `google-cloud-cpp` submodule and ClickHouse wrapper can expose `storage_grpc` cleanly.
- Exact generated proto/stub target names and API availability after submodules are initialized.
- Exact config and table-function option names.
- Whether generic backup/copy fallback remains correct for a distinct `ObjectStorageType::GCS`.

Blocking issues:
- None.

Recommended next command:
- `/phase-plan grpc-for-gcs "Implement an explicit native GCS gRPC object storage type for ClickHouse, preserving existing GCS-as-s3 behavior, supporting read/write MergeTree disks with local/plain/plain_rewritable metadata and an explicit gcs table-function gRPC path, optimized first for same-region GCE-to-GCS direct connectivity with service-account credentials."`

## Readiness gate

Ready for phase-plan: yes

Feedback loop status: satisfied

Reason:
- The investigation completed one ask-improve cycle after the initial missing-goal clarification, incorporated the user's answers, and has no remaining blocking or plan-shaping user questions.

Blocking grey areas:
- None.

Questions for user before planning:
- None.

Safe assumptions if unanswered:
- None.

## Investigation validation

Status: passed

Hard checks:
- User goal captured: pass
- User model documented: pass
- Feedback loop state documented: pass
- Relevant project instructions inspected: pass
- Relevant existing plan/investigation files inspected if present: pass
- Findings cite evidence: pass
- Assumptions separated from facts: pass
- Grey areas marked by severity and blocking status: pass
- Candidate approaches include tradeoffs: pass
- Blocking and plan-shaping questions are asked directly, not only listed: pass
- No implementation tasks included: pass
- No code changes made: pass

Warnings:
- The `google-cloud-cpp` submodule is not initialized in this checkout, so wrapper/client-layer findings are based on visible wrapper files and external docs. Phase planning must revalidate against initialized submodule sources.
- No build or tests were run because this investigation is intentionally non-implementation.

## Investigation change log

- 2026-05-08: Initial investigation created after repository inspection, external research, and user clarification of the implementation/performance goal.
- 2026-05-08: Incorporated direct user answers on explicit new type, MVP surface, and GCE direct-connectivity envelope; marked ready for `/phase-plan`.
