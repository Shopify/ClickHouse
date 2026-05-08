# Native GCS gRPC object storage plan

Plan status: ready

Plan slug: grpc-for-gcs

## User goal

Implement an explicit native GCS gRPC object storage type for ClickHouse, preserving existing GCS-as-`s3` behavior, supporting read/write `MergeTree` disks with `local`, `plain`, and `plain_rewritable` metadata and an explicit `gcs` table-function gRPC path, optimized first for same-region GCE-to-GCS direct connectivity with service-account credentials.

## Investigation baseline

Investigation file: [investigation.md](./investigation.md)

Investigation status: ready

Imported findings:
- F001: Current ClickHouse GCS user-facing support is primarily an S3/XML compatibility path, not native GCS gRPC.
- F002: The cleanest ClickHouse extension point is a new native `IObjectStorage` implementation behind an explicit `object_storage_type`.
- F003: Adding a distinct `ObjectStorageType::GCS` has non-trivial compatibility reach.
- F004: ClickHouse already vendors and wraps `google-cloud-cpp`, gRPC, protobuf, and Google APIs, but the current storage wrapper is REST-oriented.
- F005: Google documents GCS gRPC performance benefits as environment-dependent.
- F006: Existing S3 code contains GCS-specific workarounds that a native path must intentionally replace or preserve.
- F007: The MVP is broader than disk-only.

Imported constraints:
- C001: Preserve existing documented GCS-as-`s3` behavior.
- C002: Native implementation must satisfy the `IObjectStorage` contract for read/write `MergeTree` disk, `local`, `plain`, and `plain_rewritable` metadata in MVP.
- C003: Native gRPC availability is build-gated.
- C004: Performance claims must be tied to same-region GCE-to-GCS direct connectivity.
- C005: Avoid silent fallback paths.
- C006: Planning prompt may only update `plans/grpc-for-gcs/plan.md`.

Imported assumptions or grey areas:
- AS001: The implementation should be native C++ under existing `DiskObjectStorage` rather than a Rust plugin or sidecar.
- AS002: Existing `s3`/`gcs` XML behavior should remain unchanged by default.
- AS003: A distinct native GCS identity is preferable for clarity even if it requires enum/switch updates.
- AS004: The performance target is same-region GCE-to-GCS direct connectivity, not generic internet GCS access.
- AS005: Table-function support is MVP, but current table-function default behavior must remain compatible.
- AS006: Native backup/copy optimization is not MVP, but buffered correctness should continue through generic disk/object-storage paths.
- G001: Exact native config spelling is non-blocking and should be resolved by planning/maintainer review.
- G002: Table-function gRPC integration boundary is non-blocking but must preserve current defaults.
- G003: The client layer must choose between upstream `storage_grpc` and generated gRPC stubs.
- G004: Exact GCS gRPC semantics for `compose`, `rewrite`, generation preconditions, object attributes/tags, and resumable writes need implementation-phase validation.
- G005: Hermetic testing should use a fake/mock gRPC service plus environment-gated real GCS performance validation.
- G006: Observability naming should prefer distinct GCS identity where provider-specific additions are needed.

Imported blockers:
- None.

Planning response:
- This plan adopts the investigation recommendation: explicit native C++ `GCSObjectStorage` under `IObjectStorage`, preserving existing GCS-as-`s3` behavior by default.
- The plan collapses the investigation's seven suggested phase outcomes into six reviewable phases to keep handoff manageable: client foundation, native type/config, core disk semantics, metadata modes, table function, and compatibility/performance/docs.
- Non-blocking naming grey areas are resolved as planning defaults: use `object_storage_type=gcs` for the native disk unless maintainer review forces `gcs_grpc`, and use an explicit table-function transport option rather than changing current `gcs` defaults.

## Problem statement

ClickHouse already supports GCS through S3/XML compatibility, but that path cannot directly use Cloud Storage gRPC/direct connectivity. This plan adds a native, explicit GCS gRPC object storage backend that fits the existing `DiskObjectStorage`/`IObjectStorage` architecture, preserves existing S3/XML behavior, supports the requested disk metadata modes and table-function surface, and validates performance in the same-region GCE direct-connectivity envelope where Google documents gRPC benefits.

## Non-goals

- Transparent migration or automatic upgrade of existing `type=s3` GCS disks.
- Native GCS backup/copy optimization as an MVP requirement, unless required for correctness after validation.
- Broad first-release auth coverage for ADC refresh-token files, HMAC/XML compatibility, public/no-sign buckets, or non-GCE hosts beyond what falls out safely from the chosen client layer.
- Rust plugin, sidecar, or out-of-process storage implementation.
- Silent fallback from native GCS gRPC to S3/XML for consequential disk operations.
- Creating executable task lists in this file.

## Constraints

- Preserve current `gcs` table-function and GCS-as-`s3` disk behavior unless an explicit new gRPC option is selected.
- Guard native GCS code behind `USE_GOOGLE_CLOUD` or equivalent build availability checks; unsupported builds must fail closed with a clear exception.
- Future C++ work must follow ClickHouse Allman brace style.
- Future build/test commands must redirect output to build-directory log files and use a subagent to summarize logs.
- Do not use `-j` or `nproc` with `ninja` in later implementation phases.
- Use `tmp` under the repository for temporary files in later phases; do not use `/tmp`.
- Documentation changes under `docs/` must use explicit heading anchors and required frontmatter for new docs.
- Keep this plan strategic; `/phase-tasks` owns executable task decomposition.

## Assumptions

- `object_storage_type=gcs` is the best planning name for the explicit native disk type. Confidence: medium. Validate through maintainer review and local naming consistency in P02.
- The table-function gRPC path should be explicit, likely through a setting or named-collection option such as transport selection, while current `gcs` behavior remains default. Confidence: high. Validate in P02/P05 against parser/config conventions.
- Same-region GCE service-account credentials are enough for the first performance envelope. Confidence: high. Validate through P01 auth design and P06 environment-gated performance run.
- Generic disk/backup copy fallback remains correct for native GCS even without native GCS `rewrite` optimization. Confidence: medium. Validate in P03 and P06.
- A fake/mock gRPC service can cover correctness without real GCS credentials. Confidence: medium. Validate in P01/P03 test scaffold.

## Open questions

- [ ] Q001: Should the public native disk type name be `gcs` or `gcs_grpc`?
  Blocking: no
  Plan-shaping: no
  Asked directly in chat: no
  Safe assumption or validation path: Default to `object_storage_type=gcs` because the user requested an explicit native GCS type and existing `gcs` table-function naming already exists; validate during P02 and maintainer review.

- [ ] Q002: What exact table-function opt-in spelling should expose gRPC?
  Blocking: no
  Plan-shaping: no
  Asked directly in chat: no
  Safe assumption or validation path: Use an explicit setting/named-collection transport option in P05; preserve current `gcs` default behavior.

## Acceptance criteria

- [ ] A001: Existing GCS-as-`s3` disks and default `gcs` table-function behavior continue to use the existing S3/XML path unless an explicit native gRPC option is selected. Mapped phases: P02, P05, P06.
- [ ] A002: A native GCS object storage type is registered, visible through disk identity surfaces, and unavailable builds fail closed with a clear exception. Mapped phases: P01, P02.
- [ ] A003: Native GCS supports read/write `MergeTree` disk operations with `local` metadata. Mapped phase: P03.
- [ ] A004: Native GCS supports `plain` and `plain_rewritable` metadata modes with compatible key-prefix and metrics behavior. Mapped phase: P04.
- [ ] A005: The `gcs` table function has an explicit native gRPC path without changing the current default S3/XML path. Mapped phase: P05.
- [ ] A006: Correctness is covered by targeted unit/integration/fake-service tests, and performance has an environment-gated validation path for same-region GCE direct connectivity. Mapped phases: P01, P03, P04, P05, P06.
- [ ] A007: User-facing docs or release-facing notes explain native GCS gRPC configuration, limitations, build availability, and direct-connectivity expectations. Mapped phase: P06.

## Relevant context

- `plans/grpc-for-gcs/investigation.md`: Evidence baseline; ready status; imports findings F001-F007 and constraints C001-C006.
- `docs/en/sql-reference/table-functions/gcs.md`: Documents `gcs` as an `s3` alias using the GCS XML API and HMAC keys; must not be silently invalidated.
- `docs/en/operations/storing-data.md`: Documents GCS disks through type `s3`; native GCS docs must be additive.
- `src/Disks/DiskObjectStorage/ObjectStorages/IObjectStorage.h`: Defines the provider contract native GCS must satisfy.
- `src/Disks/DiskObjectStorage/ObjectStorages/ObjectStorageFactory.cpp`: Registers object storage providers by config type.
- `src/Disks/DiskObjectStorage/RegisterDiskObjectStorage.cpp`: Composes object storage with metadata storage into `DiskObjectStorage`.
- `src/Disks/DiskType.h` and `src/Disks/DiskType.cpp`: Define object storage identity and names used by system surfaces.
- `src/Disks/DiskObjectStorage/MetadataStorages/MetadataStorageFactory.cpp`: Determines metadata defaults and key-prefix behavior.
- `src/Disks/DiskObjectStorage/MetadataStorages/PlainRewritable/PlainRewritableMetrics.cpp`: Provider-specific metrics surface for `plain_rewritable` metadata.
- `src/Disks/DiskObjectStorage/ObjectStorages/S3/S3ObjectStorage.*`: Existing comparable provider; also illustrates why a dual S3/gRPC class should be avoided.
- `src/IO/S3/*` and `src/IO/GCPOAuth.*`: Existing GCS S3/XML compatibility, OAuth, and error-workaround behavior to preserve or intentionally replace.
- `src/Storages/ObjectStorage/registerStorageObjectStorage.cpp` and `src/TableFunctions/TableFunctionObjectStorage.cpp`: Current `GCS` storage/table-function registration path through S3.
- `contrib/google-cloud-cpp-cmake/` and `contrib/CMakeLists.txt`: Optional Google Cloud C++/gRPC/protobuf build integration requiring validation.

## Decisions

- D001: Implement native GCS as a distinct explicit object storage type, not as a transparent `s3` upgrade.
  Rationale: User selected explicit new type; existing docs and implementation promise S3/XML compatibility.
  Alternatives considered: Transparent upgrade; hybrid branch inside `S3ObjectStorage`.
  Reversible: no
  Affects phases: P02, P03, P04, P05, P06

- D002: Use C++ `IObjectStorage`/`DiskObjectStorage` integration as the architecture.
  Rationale: This matches current provider abstraction and avoids Rust/sidecar operational drift.
  Alternatives considered: Rust plugin, sidecar, mutating S3 classes.
  Reversible: no
  Affects phases: P01, P02, P03, P04

- D003: Validate the gRPC client layer before committing to high-level `storage_grpc` or direct generated stubs.
  Rationale: Investigation found visible wrapper files are REST-oriented even though Google docs describe `MakeGrpcClient` and `storage_grpc`.
  Alternatives considered: Assume `google::cloud::storage::Client` works; write direct stubs immediately.
  Reversible: yes
  Affects phases: P01, P03

- D004: Preserve default `gcs` table-function behavior and add explicit gRPC opt-in.
  Rationale: MVP includes table-function support, but current behavior is documented as S3/XML.
  Alternatives considered: Replace `gcs` implementation wholesale; create a separate table function.
  Reversible: yes
  Affects phases: P02, P05, P06

- D005: Target same-region GCE service-account/direct-connectivity first.
  Rationale: User selected this envelope and Google documents it as the performance-relevant path.
  Alternatives considered: Broad auth/platform support in MVP; benchmark-only prototype.
  Reversible: yes
  Affects phases: P01, P06

- D006: Defer native GCS backup/copy optimization unless correctness requires it.
  Rationale: User did not select backups/native copy; generic buffered paths should be validated first.
  Alternatives considered: Include `rewrite`/copy optimization in MVP.
  Reversible: yes
  Affects phases: P03, P06

## Verification ladder

Use the lowest sufficient verification tier for each phase:

- Tier 0 smoke: format, lint, typecheck, targeted unit tests, static checks.
- Tier 1 core: relevant unit or integration test suite.
- Tier 2 behavioral: end-to-end test, migration test, benchmark, scenario, or regression reproduction.
- Tier 3 manual: manual inspection or human review when no automated check exists.

Each phase names its intended tier and why that tier is sufficient. Future phase work must redirect build/test output to build-directory log files and use a subagent to summarize logs, per repository instructions.

## Phase overview

| Phase | Slug | Goal | Dependencies | Expected artifacts | Verification tier | Verification |
|---|---|---|---|---|---|---|
| P01 | 01-grpc-client-foundation | Establish build-gated native GCS gRPC client/auth foundation and decide high-level client vs direct stubs. | none | `contrib/google-cloud-cpp-cmake/`, `src/IO/GCS/` or equivalent, fake gRPC test scaffold | Tier 1 | Targeted build/config checks and client/auth unit or fake-service tests |
| P02 | 02-native-type-and-config | Add explicit native GCS identity/config registration while preserving existing S3/XML defaults. | P01 | `src/Disks/DiskType.*`, `ObjectStorageFactory`, registration/config docs hooks | Tier 1 | Targeted unit/config tests plus existing disk identity checks |
| P03 | 03-core-rw-disk | Implement native GCS read/write object storage semantics sufficient for `MergeTree` disk with `local` metadata. | P01, P02 | `src/Disks/DiskObjectStorage/ObjectStorages/GCS/`, read/write buffers/helpers | Tier 2 | Fake-service disk scenario plus targeted unit/core integration tests |
| P04 | 04-metadata-modes | Support `plain` and `plain_rewritable` metadata for native GCS. | P03 | Metadata compatibility updates, metrics handling, metadata tests | Tier 2 | Metadata-mode integration/scenario tests over fake GCS |
| P05 | 05-gcs-table-function | Add explicit native gRPC path for `gcs` table-function use without changing default behavior. | P01, P03 | Table-function/storage config routing and tests | Tier 2 | Table-function behavioral tests for default XML path and explicit gRPC path |
| P06 | 06-compatibility-performance-docs | Prove compatibility, document usage/limits, and add environment-gated performance validation. | P02, P03, P04, P05 | Docs, regression coverage, benchmark/perf harness notes | Tier 2 | Regression tests plus environment-gated benchmark/manual report |

## Phases

### P01: gRPC client foundation

Slug: `01-grpc-client-foundation`

Goal:
Establish the native GCS gRPC dependency, client, channel/auth, and test foundation behind build guards, and choose whether the implementation uses upstream `storage_grpc` or generated `google.storage.v2` stubs.

Scope:
- Validate `google-cloud-cpp` submodule and ClickHouse wrapper support for storage gRPC.
- Add or expose the minimal native GCS gRPC client layer required by later disk phases.
- Support service-account/GCE direct-connectivity credentials as the first auth envelope.
- Establish clear unsupported-build behavior.
- Establish a fake/mock gRPC testing seam for later phases.

Out of scope:
- Disk registration or user-facing disk configuration.
- Full object storage semantics.
- Broad auth modes beyond same-region GCE/service-account target.

Dependencies:
- none

Phase interface:

Inputs:
- Investigation findings F004 and F005.
- Existing `contrib/google-cloud-cpp-cmake/`, gRPC, protobuf, and Google APIs build plumbing.

Outputs:
- A chosen and documented client-layer approach: high-level `storage_grpc` or direct generated stubs.
- A build-gated native GCS client/auth abstraction usable by disk and table-function phases.
- A fake/mock test seam for GCS gRPC requests.

Downstream contract:
- Later phases may create native GCS operations through a stable internal client abstraction without knowing whether the implementation uses high-level `storage_grpc` or direct stubs.
- Unsupported builds fail closed before runtime disk operations proceed.

Assumptions exported:
- Service-account/GCE credentials are the first supported performance/auth envelope.
- The chosen client layer can express range reads, streaming writes, list, metadata, and delete semantics needed by `IObjectStorage`.

Assumptions not exported:
- That the unchosen client-layer alternative is impossible or bad; P01 only needs to choose the best path for this plan.

Expected artifacts:
- `contrib/google-cloud-cpp-cmake/`: wrapper adjustments if upstream `storage_grpc` is exposed.
- `src/IO/GCS/` or equivalent: internal client/auth/error abstraction if direct helpers are needed.
- Test scaffold for fake/mock gRPC behavior.

Verification approach:
- Tier: Tier 1
- Method: Targeted build/config verification and unit or fake-service tests for client creation, auth setup, unsupported-build behavior, and representative request/response mapping. When run later, logs must be redirected to a build-directory log and summarized by a subagent.
- Sufficiency: P01 is foundational; targeted client/auth tests are enough before disk behavior exists.

Completion criteria:
- The native GCS client layer can be compiled in supported builds and is absent or rejects clearly in unsupported builds.
- The plan has a recorded client-layer decision with rationale.
- A fake/mock gRPC seam exists for subsequent correctness tests.

Risks and rollback:
- Risk: Upstream `storage_grpc` is unavailable or too inflexible. Mitigation: use generated stubs directly. Rollback: remove wrapper exposure and keep only direct-stub abstraction.
- Risk: Auth behavior differs from Google docs or ClickHouse S3 OAuth helpers. Mitigation: keep auth scope narrow and fail closed. Rollback: disable native GCS build path until auth is fixed.

Task decomposition guidance:
- Split tasks by build/wrapper validation, client abstraction, auth/deadline/error mapping, and fake-service test seam. Do not implement disk operations in this phase.

### P02: native type and config

Slug: `02-native-type-and-config`

Goal:
Introduce explicit native GCS identity and configuration surfaces while preserving all existing GCS-as-`s3` and default `gcs` behavior.

Scope:
- Add native GCS identity to disk/object-storage type surfaces.
- Register explicit native GCS object storage config through `ObjectStorageFactory`.
- Define build-guarded factory behavior for unsupported builds.
- Establish planning-default user-facing names, subject to maintainer review.
- Update system identity and compatibility switch sites required before behavior phases.

Out of scope:
- Full read/write object storage behavior.
- Table-function implementation beyond reserving/aligning config naming decisions.
- Documentation prose beyond placeholders or minimal config references needed by tests.

Dependencies:
- P01

Phase interface:

Inputs:
- P01 client-layer availability contract.
- Decisions D001, D002, D004.
- Investigation findings F001-F003.

Outputs:
- Native GCS is a distinct internal object storage identity.
- Native GCS can be selected explicitly in disk config, using `object_storage_type=gcs` unless changed by phase validation.
- Existing `type=s3` and default `gcs` paths remain unchanged.

Downstream contract:
- P03 and P04 may assume explicit native GCS disk configuration reaches the native provider factory.
- P05 may assume table-function naming must be explicit and compatibility-preserving.

Assumptions exported:
- Native disk type naming is stable enough for implementation and tests after this phase.

Assumptions not exported:
- That native GCS supports any real operations beyond clear construction/failure paths.

Expected artifacts:
- `src/Disks/DiskType.h` and `src/Disks/DiskType.cpp`: native GCS identity/name updates.
- `src/Disks/DiskObjectStorage/ObjectStorages/ObjectStorageFactory.cpp`: explicit native GCS registration or unsupported-build gate.
- `src/Disks/DiskObjectStorage/RegisterDiskObjectStorage.cpp`: any needed alias or registration wiring.
- Metadata/default/switch-site updates needed for a distinct `ObjectStorageType::GCS`.
- Targeted config/identity tests.

Verification approach:
- Tier: Tier 1
- Method: Targeted unit/config tests that native GCS config resolves to the native provider in supported builds or a clear exception in unsupported builds, plus regression checks that existing S3/GCS XML config still resolves to S3. Build/test logs must be redirected and summarized later.
- Sufficiency: Correct registration and compatibility can be proven before full object operations exist.

Completion criteria:
- Native GCS is explicit and distinguishable in disk identity surfaces.
- Existing S3/XML paths are not redirected.
- Unsupported native builds fail closed with a clear exception.

Risks and rollback:
- Risk: `object_storage_type=gcs` conflicts semantically with existing table-function `GCSDefinition::object_storage_type`. Mitigation: validate naming in this phase; switch to `gcs_grpc` if needed. Rollback: keep internal enum but change config spelling before release.
- Risk: Switch-site changes break unrelated disk identity behavior. Mitigation: targeted identity tests and existing disk tests. Rollback: revert GCS enum exposure while retaining P01 client foundation.

Task decomposition guidance:
- Organize tasks by enum/name surfaces, factory registration, unsupported-build behavior, and compatibility tests. Keep real GCS operations out of this phase.

### P03: core read/write disk semantics

Slug: `03-core-rw-disk`

Goal:
Implement native GCS `IObjectStorage` behavior sufficient for read/write `MergeTree` disk workloads using `local` metadata.

Scope:
- Implement provider operations needed by `DiskObjectStorage` with `local` metadata: existence, listing/iteration, metadata retrieval, range-capable reads, writes, deletes, lifecycle, namespace/prefix handling, settings patching, and key generation.
- Integrate with ClickHouse read/write settings, timeouts, throttling/resource conventions where required.
- Ensure generic buffered copy paths remain correct when native GCS lacks optimized rewrite/copy support.
- Map GCS gRPC statuses to ClickHouse exceptions consistently.

Out of scope:
- `plain` and `plain_rewritable` metadata-specific behavior.
- `gcs` table-function routing.
- Native backup/copy optimization.
- Broad non-GCE auth modes.

Dependencies:
- P01
- P02

Phase interface:

Inputs:
- P01 client abstraction and fake-service seam.
- P02 native config/identity wiring.
- `IObjectStorage` provider contract.

Outputs:
- Native `GCSObjectStorage` supports core read/write disk operations with `local` metadata.
- Fake-service tests exercise representative read/write/list/delete/metadata scenarios.
- Generic fallback copy correctness is validated or a blocker is documented before P06.

Downstream contract:
- P04 may assume core native GCS object operations work for metadata-mode storage.
- P05 may reuse read/list/write primitives for table-function gRPC path where appropriate.
- P06 may run compatibility and performance validation over a functional disk.

Assumptions exported:
- Core `IObjectStorage` operations are stable enough to support metadata modes.
- Native GCS returns sufficient metadata for ClickHouse object metadata needs.

Assumptions not exported:
- That advanced GCS `compose`/`rewrite` optimization is implemented.
- That performance is proven outside the fake-service environment.

Expected artifacts:
- `src/Disks/DiskObjectStorage/ObjectStorages/GCS/`: native provider implementation.
- `src/Disks/IO/` or `src/IO/GCS/`: read/write buffer helpers if needed.
- Unit and fake-service disk tests for core operations.

Verification approach:
- Tier: Tier 2
- Method: Behavioral fake-service disk scenario covering `MergeTree`-relevant read/write/list/delete/metadata flows, plus targeted unit tests for error/status mapping and range behavior. Later execution must redirect logs and use subagent summaries.
- Sufficiency: A behavior scenario is needed because this phase claims end-to-end disk semantics, not just compile-time wiring.

Completion criteria:
- A `MergeTree`-style disk can use native GCS with `local` metadata in the fake/test environment.
- Core object operations honor fail-closed behavior on unsupported or failed gRPC operations.
- Existing S3/GCS XML path tests remain unaffected.

Risks and rollback:
- Risk: GCS gRPC write streaming semantics do not fit existing write-buffer expectations. Mitigation: constrain initial write mode to `Rewrite` semantics required by object storage and validate resumable/streaming behavior early. Rollback: keep read/list implementation behind disabled write support until semantics are correct.
- Risk: Metadata or range reads require behavior not exposed by chosen client layer. Mitigation: revisit P01 choice; direct stubs may replace high-level client. Rollback: revert provider registration while retaining client foundation.

Task decomposition guidance:
- Decompose by `IObjectStorage` operation families and test fixtures, not by broad file edits. Keep metadata-specific and table-function-specific work for later phases.

### P04: metadata modes

Slug: `04-metadata-modes`

Goal:
Make native GCS work correctly with `plain` and `plain_rewritable` metadata modes, including key-prefix compatibility and provider-specific metrics behavior.

Scope:
- Validate native GCS `getCommonKeyPrefix`, namespace, and key-generator behavior with `plain` metadata.
- Support `plain_rewritable` metadata on native GCS, including provider type switches and metrics decisions.
- Ensure no implicit migration from existing GCS-as-S3 metadata layouts.
- Cover metadata operations over the fake-service/native GCS provider.

Out of scope:
- Native GCS backup/copy optimization.
- Table-function gRPC path.
- Keeper metadata unless later requirements explicitly add it.

Dependencies:
- P03

Phase interface:

Inputs:
- P03 core native object operations.
- Existing metadata storage implementations and metrics conventions.
- Assumption AS006 about backup/copy not being MVP.

Outputs:
- Native GCS supports `metadata_type=plain` and `metadata_type=plain_rewritable`.
- Metrics and identity switch sites handle `ObjectStorageType::GCS` deliberately.
- Tests verify metadata behavior and no accidental dependency on existing S3 layout.

Downstream contract:
- P06 may treat all requested disk metadata modes as MVP-complete.
- P05 may rely on core object semantics without revalidating disk metadata modes.

Assumptions exported:
- Native GCS metadata modes are separate from existing GCS-as-S3 metadata and do not require automatic migration.

Assumptions not exported:
- That all replicated/Keeper metadata workflows are supported.

Expected artifacts:
- `src/Disks/DiskObjectStorage/MetadataStorages/MetadataStorageFactory.cpp`: compatibility hints for native GCS if needed.
- `src/Disks/DiskObjectStorage/MetadataStorages/PlainRewritable/PlainRewritableMetrics.cpp`: GCS metrics handling or generic mapping.
- Metadata-mode tests for native GCS.

Verification approach:
- Tier: Tier 2
- Method: Behavioral metadata-mode tests over native GCS fake-service storage for `plain` and `plain_rewritable`, plus targeted checks for metrics/identity switch coverage.
- Sufficiency: Metadata modes affect disk correctness and file layout; behavior tests are required.

Completion criteria:
- `plain` and `plain_rewritable` metadata modes can create, read, move/remove, and observe expected metadata over native GCS in tests.
- Existing S3/Azure/Local metadata-mode behavior is not regressed.
- Metrics/identity behavior for native GCS is explicit.

Risks and rollback:
- Risk: `plain_rewritable` requires provider-specific behavior not present in native GCS. Mitigation: implement only semantically correct operations and fail closed on unsupported operations. Rollback: gate `plain_rewritable` for native GCS until correctness is achieved.
- Risk: Metrics proliferation complicates observability. Mitigation: prefer distinct GCS provider metrics only where needed; reuse generic object-storage metrics otherwise. Rollback: defer nonessential provider-specific metrics.

Task decomposition guidance:
- Separate `plain` compatibility, `plain_rewritable` compatibility, switch-site/metrics updates, and metadata tests. Avoid revisiting core client-layer decisions unless tests expose a contract gap.

### P05: `gcs` table-function gRPC path

Slug: `05-gcs-table-function`

Goal:
Add an explicit native gRPC path for `gcs` table-function use while preserving current default S3/XML-compatible behavior.

Scope:
- Define and implement explicit table-function opt-in semantics for native gRPC.
- Route explicit gRPC table-function usage through the native GCS client/object-storage path or a shared compatible abstraction.
- Preserve default `gcs` table-function behavior and existing access semantics.
- Add tests for default path preservation and explicit gRPC behavior.

Out of scope:
- Replacing `StorageS3Configuration` for default `gcs` usage.
- Broad new SQL syntax beyond the explicit opt-in needed for MVP.
- Full storage-engine redesign unless required by table-function routing.

Dependencies:
- P01
- P03

Phase interface:

Inputs:
- P01 native client abstraction.
- P03 core native object read/list/write operations.
- Existing `GCSDefinition`, `StorageS3Configuration`, and table-function registration context.

Outputs:
- Explicit `gcs` table-function gRPC path exists and is testable.
- Default `gcs` remains S3/XML-compatible.
- User-facing config/setting choice is ready for documentation in P06.

Downstream contract:
- P06 may document the final table-function opt-in behavior and include it in regression/performance validation.

Assumptions exported:
- The explicit table-function option is stable enough for documentation and tests.

Assumptions not exported:
- That all `s3` table-function features automatically work through native GCS gRPC.

Expected artifacts:
- `src/TableFunctions/TableFunctionObjectStorage.cpp` or related object-storage table-function configuration paths.
- `src/Storages/ObjectStorage/` configuration/routing updates if needed.
- Table-function tests for default and explicit gRPC behavior.

Verification approach:
- Tier: Tier 2
- Method: Behavioral tests that default `gcs` uses existing S3/XML-compatible flow and explicit gRPC uses the native path against fake-service or controlled test infrastructure.
- Sufficiency: Table-function behavior is user-facing and must be proven by scenario tests, not just compile checks.

Completion criteria:
- Explicit gRPC opt-in path works for representative read and write table-function usage.
- Existing default `gcs` tests continue to pass unchanged.
- Unsupported native gRPC builds reject explicit gRPC table-function use clearly.

Risks and rollback:
- Risk: Table-function settings/named-collection parsing cannot express transport cleanly. Mitigation: use the smallest existing settings mechanism that preserves defaults. Rollback: defer table-function gRPC path behind a feature flag while keeping disk work.
- Risk: Sharing disk object-storage code with table functions introduces unwanted disk metadata coupling. Mitigation: share client/read/write primitives, not disk metadata state. Rollback: create a table-function-specific adapter over the native client.

Task decomposition guidance:
- Decompose by UX/config selection, routing adapter, default-regression tests, and explicit gRPC tests. Do not broaden to all S3-compatible table-function features unless needed by acceptance criteria.

### P06: compatibility, performance, and docs

Slug: `06-compatibility-performance-docs`

Goal:
Prove the MVP does not regress existing behavior, document the new explicit native GCS gRPC surfaces, and establish performance validation for the same-region GCE direct-connectivity envelope.

Scope:
- Run targeted regression coverage for existing S3/GCS XML disk and table-function behavior.
- Validate native GCS disk/table-function behavior across requested surfaces.
- Add or update user-facing docs for native GCS gRPC configuration, build availability, auth envelope, limitations, and performance prerequisites.
- Add or document environment-gated benchmark/performance validation for same-region GCE-to-GCS direct connectivity.
- Confirm generic backup/copy correctness or explicitly document deferred native optimization.

Out of scope:
- Implementing native GCS copy/rewrite optimization unless P03/P06 validation proves generic correctness is insufficient.
- Expanding auth beyond the selected service-account/direct-connectivity envelope.

Dependencies:
- P02
- P03
- P04
- P05

Phase interface:

Inputs:
- Completed native type/config, disk semantics, metadata modes, and table-function path.
- Existing docs and regression tests.
- Direct-connectivity environment requirements from investigation.

Outputs:
- Compatibility evidence for existing S3/GCS XML behavior.
- MVP correctness evidence for native GCS disk/table-function surfaces.
- Documentation for the explicit native GCS gRPC feature.
- Performance validation method and, where environment is available, benchmark results or a reproducible benchmark harness.

Downstream contract:
- The feature is ready for final review with clear limitations, no silent compatibility changes, and documented validation.

Assumptions exported:
- None; this is the final phase.

Assumptions not exported:
- None.

Expected artifacts:
- `docs/en/operations/storing-data.md`: native GCS disk configuration and limitations.
- `docs/en/sql-reference/table-functions/gcs.md`: explicit gRPC option and default-behavior note.
- Tests or benchmark harness/configuration under existing test/performance locations as appropriate.
- Review evidence captured by the phase review required by `/phase-work`.

Verification approach:
- Tier: Tier 2
- Method: Existing relevant S3/GCS XML regression tests, native GCS fake-service behavioral tests from prior phases, metadata-mode tests, table-function tests, and an environment-gated benchmark or documented manual performance run for same-region GCE direct connectivity.
- Sufficiency: The phase combines compatibility and performance; automated behavior coverage plus environment-gated performance validation is the lowest sufficient tier.

Completion criteria:
- Existing GCS-as-`s3` and default `gcs` behavior is shown not to regress.
- Native GCS gRPC MVP surfaces have passing targeted tests.
- Docs explain explicit opt-in, build guards, limitations, auth/deployment envelope, and no automatic migration.
- Performance validation is reproducible, even if real GCE credentials/environment are not always available in CI.

Risks and rollback:
- Risk: Real GCE direct-connectivity benchmarking is not available in CI. Mitigation: make performance validation environment-gated and document exact manual procedure. Rollback: keep correctness complete and mark performance evidence as manual pre-release validation.
- Risk: Docs expose unstable naming. Mitigation: finalize naming before documentation update in this phase. Rollback: adjust docs/tests before release; do not preserve unreviewed names.

Task decomposition guidance:
- Decompose by regression suite selection, docs, benchmark harness/procedure, and final compatibility review. Avoid adding new feature scope in the final phase unless it fixes a validation failure.

## Plan validation

Status: passed

Hard checks:
- Unique phase ids and slugs: pass
- Dependencies reference existing earlier phases or `none`: pass
- Phase dependency graph has no cycles: pass
- No phase depends on a later phase: pass
- Every phase has expected artifacts or `None` with a reason: pass
- Every phase has a verification approach and tier: pass
- Every plan-level acceptance criterion maps to at least one phase: pass
- If `investigation.md` exists, imported findings, constraints, assumptions, grey areas, and blockers are reflected or explicitly rejected with rationale: pass
- Blocking or plan-shaping open questions are asked directly in the chat response, or plan status is `blocked`: pass
- Plan contains no implementation task checklist: pass
- Plan contains no generic filler: pass

Warnings:
- The `google-cloud-cpp` submodule is not initialized in this checkout, so P01 must revalidate wrapper/client-layer details before later phases rely on them.
- The plan intentionally uses six phases by combining final compatibility, docs, and performance validation; this stays within the requested normal range while preserving reviewable increments.

## Review and handoff expectations

- Each phase must produce `<phase-slug>-review.md` before completion.
- Review findings that require work must become tasks before the next phase starts.
- Notes must capture assumptions, decisions, uncertainties, and handoff summary.

## Plan change log

- 2026-05-08: Initial plan created from ready investigation baseline.

## Plan maintenance

- Update this plan only when scope, phase order, acceptance criteria, or constraints change.
- Record every material plan change in the plan change log.
- Do not use this plan as a task list.
