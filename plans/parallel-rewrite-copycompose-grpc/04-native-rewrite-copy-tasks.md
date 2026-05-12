# Native rewrite copy Tasks

Phase status: completed

Phase id: P04
Phase slug: 04-native-rewrite-copy
Plan: [plan.md](./plan.md)

Phase goal:
Implement native same-GCS copy using GCS `RewriteObject` token iteration and expose the copy-side compose/rewrite behavior needed by native GCS workflows, while preserving generic cross-provider copy correctness.

Verification tier:
Tier 1

Dependencies:
- P01 / `01-gcs-client-capabilities` completed with reviewer all-clear and exposed `GCS::Client::rewriteObject` plus fake rewrite-token support.

Tasks:
- [x] T001: Inspect pre-existing diffs in `src/Disks/DiskObjectStorage/ObjectStorages/GCS/GCSObjectStorage.*`, `src/IO/GCS/GCSClient.*`, `src/Disks/tests/gtest_gcs_object_storage_config.cpp`, and `src/IO/tests/gtest_gcs_grpc_client.cpp` before editing. Acceptance: `plans/parallel-rewrite-copycompose-grpc/04-native-rewrite-copy-notes.md` records which diffs are prerequisite, unrelated, or already satisfy P04 work, and no unrelated user changes are overwritten.
  Done: local change pending commit; relevant source paths had no pre-existing diffs, while unrelated `contrib/*` and `tmp/` changes were left untouched.
- [x] T002: Validate the native rewrite policy against `04-native-rewrite-copy` scope, the P01 handoff in `01-gcs-client-capabilities-notes.md`, current `GCSObjectStorage::copyObject` / `copyObjectToAnotherObjectStorage`, and `S3ObjectStorage` copy dispatch. Acceptance: notes record the same-GCS compatibility rule, which client issues `RewriteObject`, how unsupported destination preconditions or attributes fail closed, and where generic read/write copy remains allowed.
  Done: local change pending commit; notes record destination-client rewrite, conservative endpoint/authority compatibility, precondition policy, and generic fallback boundaries.
- [x] T003: Add a native rewrite helper for `GCSObjectStorage` in `src/Disks/DiskObjectStorage/ObjectStorages/GCS/GCSObjectStorage.cpp`. Acceptance: the helper builds `google::storage::v2::RewriteObjectRequest` with source and destination bucket/object names, repeats requests with `rewrite_token` until `done=true`, propagates non-OK `GCS::Status` as an exception, and can be exercised without disk setup through fake tests.
  Done: local change pending commit; `rewriteObjectFromGCS` builds and iterates `RewriteObject` requests, and `RewriteTokenIterationUsesContinuationToken` covers continuation.
- [x] T004: Map destination metadata and representable write preconditions for native rewrite in `GCSObjectStorage`. Acceptance: targeted fake tests prove `ObjectAttributes` reach the rewrite destination resource, representable destination create-only preconditions are sent, and unsupported `WriteSettings::object_storage_write_if_none_match` / `object_storage_write_if_match` combinations fail before any native rewrite request reports success.
  Done: local change pending commit; `RewriteMetadataAndPreconditions` covers metadata, `if_generation_match=0`, create-only failure, and unsupported setting rejection before extra rewrite requests.
- [x] T005: Replace same-storage `GCSObjectStorage::copyObject` generic read/write copying with the native rewrite helper. Acceptance: a fake object-storage test copies within one native GCS storage, observes `rewrite_object_requests` and no `read_object_requests` or final-object `write_object_requests` for the copy operation, and verifies destination bytes and metadata.
  Done: local change pending commit; `SameStorageCopyUsesRewriteObject` proves same-storage copy uses `RewriteObject` without generic read/write.
- [x] T006: Add native `copyObjectToAnotherObjectStorage` dispatch when the destination is a compatible `GCSObjectStorage`, while preserving generic read/write copy for non-GCS or incompatible destinations. Acceptance: fake tests prove compatible native GCS-to-GCS copy uses `RewriteObject`, and an incompatible or non-GCS destination still follows the generic `IObjectStorage` copy path with correct final bytes.
  Done: local change pending commit; compatible GCS uses destination-client rewrite, same-endpoint different authority uses generic GCS read/write, and `LocalObjectStorage` proves non-GCS generic dispatch.
- [x] T007: Add fail-closed rewrite failure coverage in `src/Disks/tests/gtest_gcs_object_storage_config.cpp`. Acceptance: tests cover missing source, permission/status failure, and incomplete token-response failure; each case surfaces an exception, leaves no successful destination object, and does not silently fall back to generic buffered copy.
  Done: local change pending commit; `RewriteFailuresDoNotFallback` and `CrossGcsRewriteFailuresDoNotFallback` cover same-storage and compatible cross-GCS failure paths without fallback.
- [x] T008: Run Tier 1 verification with `git diff --check -- src/Disks/DiskObjectStorage/ObjectStorages/GCS/GCSObjectStorage.cpp src/Disks/DiskObjectStorage/ObjectStorages/GCS/GCSObjectStorage.h src/IO/GCS/GCSClient.cpp src/IO/GCS/GCSClient.h src/Disks/tests/gtest_gcs_object_storage_config.cpp src/IO/tests/gtest_gcs_grpc_client.cpp`, then `ninja -C build unit_tests_dbms > build/test_04_native_rewrite_copy_build.log 2>&1`, then `build/src/unit_tests_dbms --gtest_filter='GCSObjectStorageCore.*Copy*:GCSObjectStorageRewriteCopy.*:GCSGrpcClientFoundation.FakeRewrite*' > build/test_04_native_rewrite_copy.log 2>&1`. Acceptance: all commands exit 0, logs are saved under `build/`, subagents summarize the build and test logs, and `04-native-rewrite-copy-review.md` records commands, log paths, results, and any deviations from the filter.
  Done: local change pending commit; `git diff --check` passed, build passed, and the targeted filter passed 11 tests with subagent log summaries recorded in review.
- [x] T009: Resolve review finding R001 by making cross-`GCSObjectStorage` native rewrite compatibility conservative in `GCSObjectStorage::isCompatibleForNativeRewriteFrom` and adding same-endpoint different-authority coverage. Acceptance: native cross-GCS rewrite requires matching endpoint, credential mode, service account JSON, user project, and insecure-test mode; mismatched authority uses generic read/write and targeted tests pass.
  Done: local change pending commit; `SameEndpointDifferentAuthorityGcsCopyUsesGenericReadWrite` covers the generic path and reviewer all-clear confirmed R001 resolved.
- [x] T010: Resolve review finding R002 by adding cross-storage native rewrite failure coverage and true non-GCS generic dispatch coverage in `src/Disks/tests/gtest_gcs_object_storage_config.cpp`. Acceptance: compatible cross-GCS rewrite failure throws without fallback, non-GCS destination copy uses generic read/write, and targeted tests pass.
  Done: local change pending commit; `CrossGcsRewriteFailuresDoNotFallback` and `NonGcsDestinationUsesGenericReadWrite` pass, and reviewer all-clear confirmed R002 resolved.
- [x] T011: Resolve review finding R003 by updating P04 task, notes, and review bookkeeping. Acceptance: tasks are checked off, handoff summary is current, and `04-native-rewrite-copy-review.md` records verification, critique, findings, and reviewer all-clear.
  Done: local change pending commit; P04 bookkeeping is complete.

Task dependencies:

| Task | Depends on | Can run before review? | Reason |
|---|---|---|---|
| T001 | none | yes | Worktree safety must be established before editing files that may carry local diffs. |
| T002 | T001 | yes | Rewrite policy must be decided before changing dispatch, metadata, or fallback behavior. |
| T003 | T002 | yes | The rewrite helper depends on the selected same-GCS compatibility and client ownership policy. |
| T004 | T002, T003 | yes | Metadata and precondition mapping depends on the rewrite request helper. |
| T005 | T003, T004 | yes | Same-storage copy dispatch should call the validated helper with mapped attributes and preconditions. |
| T006 | T003, T004 | yes | Cross-storage dispatch needs the same helper and policy decisions, but can be implemented independently of same-storage tests. |
| T007 | T005, T006 | yes | Failure coverage must prove both native copy dispatch paths do not fall back silently. |
| T008 | T005, T006, T007 | yes | Tier 1 verification should run after implementation and targeted failure coverage are present. |
| T009 | T008 | yes | R001 was discovered during review and required compatibility tightening plus regression coverage. |
| T010 | T008 | yes | R002 was discovered during review and required additional failure and non-GCS generic dispatch coverage. |
| T011 | T008, T009, T010 | yes | R003 is bookkeeping after verification, review finding resolution, and all-clear. |

Review gates:
- Verification must be recorded in `04-native-rewrite-copy-review.md`.
- Critique must be recorded in `04-native-rewrite-copy-review.md`.
- Reviewer all-clear must be recorded before phase completion.
