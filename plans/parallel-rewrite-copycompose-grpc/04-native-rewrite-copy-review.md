# Native rewrite copy Review

## Verification

- Commands run:
  - `git diff --check -- src/Disks/DiskObjectStorage/ObjectStorages/GCS/GCSObjectStorage.cpp src/Disks/DiskObjectStorage/ObjectStorages/GCS/GCSObjectStorage.h src/IO/GCS/GCSClient.cpp src/IO/GCS/GCSClient.h src/Disks/tests/gtest_gcs_object_storage_config.cpp src/IO/tests/gtest_gcs_grpc_client.cpp`
  - `ninja -C build unit_tests_dbms > build/test_04_native_rewrite_copy_build.log 2>&1`
  - `build/src/unit_tests_dbms --gtest_filter='GCSObjectStorageCore.*Copy*:GCSObjectStorageRewriteCopy.*:GCSGrpcClientFoundation.FakeRewrite*' > build/test_04_native_rewrite_copy.log 2>&1`
- Results:
  - `git diff --check` passed.
  - `ninja -C build unit_tests_dbms` passed.
  - Targeted P04 tests passed: 11 tests run, 11 passed.
- Evidence:
  - `build/test_04_native_rewrite_copy_build.log`: build completed, `src/unit_tests_dbms` is present and executable, and no warnings/errors were found by log reviewers.
  - `build/test_04_native_rewrite_copy.log`: targeted run passed all 11 tests.
  - Subagent log summaries reported successful build and successful test run.
- Verification tier used:
  - Tier 1
- Deviations from planned verification:
  - The planned filter was used. It matched 11 tests after review follow-up coverage was added.

## Critique

- Risks:
  - Real GCS authorization behavior for cross-bucket or cross-project rewrite is not proven by fake tests; the implementation uses a conservative endpoint/authority compatibility gate and keeps generic copy for incompatible cases.
  - Native rewrite default metadata preservation may differ from the previous generic GCS read/write copy when no replacement attributes are supplied; explicit replacement attributes are covered, and broader metadata compatibility belongs to P05 if needed.
  - No real GCS validation was run in P04; P06 owns real service copy/rewrite performance and behavior validation.
- Gaps:
  - No dedicated `GCS*` profile events were added for rewrite, matching the plan's external-metrics constraint.
  - Persistent checkpointing for long-running rewrites is not implemented; P04 uses blocking token iteration and throws if the service returns an incomplete response without a token.
- Over-scope or under-scope concerns:
  - The phase added true non-GCS generic dispatch coverage with `LocalObjectStorage` to satisfy review feedback, but did not change non-GCS copy behavior.
  - P05 compatibility and P06 performance validation were not started.

## Review findings

- [x] R001: Cross-`GCSObjectStorage` native rewrite compatibility was too broad when it only checked endpoint equality.
  Severity: high
  Evidence: Initial reviewer observed same-endpoint storages with different clients or authority settings would use destination-client native rewrite and skip a generic copy that might otherwise be valid.
  Required follow-up: T009 completed by requiring matching endpoint, credential mode, service account JSON, `user_project`, and insecure-test mode, plus same-endpoint different-authority generic path coverage.

- [x] R002: Tier 1 coverage missed compatible cross-GCS native failure and true non-GCS generic dispatch.
  Severity: medium
  Evidence: Initial reviewer observed `RewriteFailuresDoNotFallback` covered only same-storage `copyObject`, and the generic dispatch test used incompatible GCS rather than non-GCS destination.
  Required follow-up: T010 completed by adding `CrossGcsRewriteFailuresDoNotFallback` and `NonGcsDestinationUsesGenericReadWrite`.

- [x] R003: Phase bookkeeping and review artifacts were stale or missing during initial review.
  Severity: medium
  Evidence: Initial reviewer observed P04 tasks unchecked, notes still saying implementation not started, and no review file.
  Required follow-up: T011 completed by updating tasks, notes, and this review file.

## Tasks added from findings

- T009
- T010
- T011

## Reviewer all-clear

Reviewer: critic agent
Status: approved
Notes: Final reviewer pass approved P04 after compatibility tightening and additional coverage. It confirmed native same-GCS rewrite token iteration, metadata/precondition mapping, fail-closed same-storage and cross-GCS failures, conservative native cross-GCS dispatch, generic fallback for incompatible GCS authority, true non-GCS generic dispatch, and green build/test logs.
