# gRPC client foundation Tasks

Phase status: in progress

Phase id: P01
Phase slug: 01-grpc-client-foundation
Plan: [plan.md](./plan.md)

Phase goal:
Establish the native GCS gRPC dependency, client, channel/auth, and test foundation behind build guards. Decide whether the implementation uses upstream `storage_grpc` or generated `google.storage.v2` stubs.

Verification tier:
Tier 1

Dependencies:
- none

Tasks:
- [x] T001: Audit the available `google-cloud-cpp`/Google APIs gRPC client layer and record the chosen P01 client approach in `plans/grpc-for-gcs/01-grpc-client-foundation-notes.md`. Acceptance: notes name either high-level `storage_grpc` or direct generated stubs as the selected path, cite the inspected CMake/submodule/proto evidence, and record any submodule state that must not be overwritten.
  Done: local change pending commit, verified by inspection of `contrib/google-cloud-cpp/google/cloud/storage/google_cloud_cpp_storage_grpc.cmake`, `contrib/google-cloud-cpp/google/cloud/storage/grpc_plugin.cc`, `contrib/google-cloud-cpp-cmake/cmake/CompileProtos.cmake`, and `contrib/google-cloud-cpp/external/googleapis/protolists/storage.list`.
- [x] T002: Expose the selected storage gRPC dependency through ClickHouse build wiring in `contrib/google-cloud-cpp-cmake/` and related CMake files. Acceptance: a supported build can depend on the selected GCS gRPC target or generated stubs without relying only on the current REST `google_cloud_cpp_storage` target, and unsupported `ENABLE_GRPC`/`ENABLE_PROTOBUF` configurations still fail closed or skip native GCS cleanly.
  Done: local change pending commit, verified by `ninja -C build google_cloud_cpp_rpc_status_protos google_cloud_cpp_storage_protos > build/test_gcs_client_foundation_protos.log 2>&1`; no `contrib` edits were needed because `google_cloud_cpp_storage_protos` already generates `google/storage/v2/storage.grpc.pb.h` and is linked by `ch_contrib::google_cloud_cpp`.
- [x] T003: Add the internal native GCS client abstraction under `src/IO/GCS/` or an equivalent reviewed location. Acceptance: the abstraction exposes client/channel construction and request entry points needed by later disk/table-function phases, while containing no disk registration or `IObjectStorage` implementation.
  Done: local change pending commit, implemented in `src/IO/GCS/GCSClient.h` and `src/IO/GCS/GCSClient.cpp`; direct object compile verified by `build/test_gcs_client_foundation_direct_objects.log`.
- [x] T004: Add first-scope service-account/GCE direct-connectivity auth and channel configuration in the P01 GCS client layer. Acceptance: targeted tests or a fake-service fixture can construct the client with service-account/GCE-oriented credentials and endpoint/deadline settings; unsupported broad auth modes are explicitly documented in notes as out of P01 scope.
  Done: local change pending commit, implemented `google_default`, `service_account_key`, and `insecure_for_tests` credential modes plus endpoint/deadline settings in `src/IO/GCS/GCSClient.*`; broad auth modes documented in notes.
- [x] T005: Add GCS gRPC status, deadline, and error mapping helpers in the P01 GCS client layer. Acceptance: targeted tests cover at least success, not found, permission denied, unavailable/deadline, and unsupported-operation mappings to ClickHouse exceptions or error categories.
  Done: local change pending commit, implemented in `src/IO/GCS/GCSStatus.*` and covered by `src/IO/tests/gtest_gcs_grpc_client.cpp`; test object compile verified by `build/test_gcs_client_foundation_direct_objects.log`.
- [x] T006: Add build-availability guards for native GCS client usage. Acceptance: when `USE_GOOGLE_CLOUD` or the selected gRPC target is unavailable, native GCS client construction is either not compiled into unsupported paths or throws a clear fail-closed exception; no code path silently falls back to S3/XML.
  Done: local change pending commit, implemented `isGrpcAvailable` and `assertGrpcAvailable` in `src/IO/GCS/GCSClient.cpp`; high-level `storage_grpc` REST fallback was not used.
- [x] T007: Add a fake/mock GCS gRPC testing seam for later phases. Acceptance: a test fixture can simulate representative unary and streaming GCS responses needed by later read/list/write/delete work, and the fixture is reusable from future disk and table-function tests.
  Done: local change pending commit, implemented `FakeStub`, `FakeReadStream`, and `FakeWriteStream` in `src/IO/GCS/GCSClient.*` and exercised by `src/IO/tests/gtest_gcs_grpc_client.cpp` object compile.
- [x] T008: Run P01 targeted verification for the build and GCS client foundation. Acceptance: `ninja -C build unit_tests_dbms > build/test_gcs_client_foundation_build.log 2>&1` and `./build/src/unit_tests_dbms --gtest_filter='*GCS*:*GoogleCloud*' > build/test_gcs_client_foundation.log 2>&1` or their exact build-directory equivalents exit 0, and log summaries are ready to be recorded in `01-grpc-client-foundation-review.md`.
  Done: local change pending commit, `google_cloud_cpp_rpc_status_protos`/`google_cloud_cpp_storage_protos` and direct GCS object compiles succeeded; full `unit_tests_dbms` build/test could not run because unrelated Rust vendoring failed with missing `quote` for `syn`, recorded in review as a verification deviation.

Task dependencies:

| Task | Depends on | Can run before review? | Reason |
|---|---|---|---|
| T001 | none | yes | The client-layer decision drives build wiring and abstraction shape. |
| T002 | T001 | yes | Build wiring depends on the selected high-level-client or direct-stub path. |
| T003 | T001 | yes | The internal abstraction must match the selected client-layer approach. |
| T004 | T003 | yes | Auth/channel setup attaches to the internal client abstraction. |
| T005 | T003 | yes | Error/deadline mapping attaches to the internal client abstraction and can be developed independently of auth. |
| T006 | T002, T003 | yes | Availability guards need both build wiring and the internal construction surface. |
| T007 | T003, T005 | yes | The fake seam should exercise the abstraction and status mapping. |
| T008 | T002, T004, T005, T006, T007 | yes | Verification should run after build wiring, auth, mapping, guards, and fake tests exist. |

Review gates:
- Verification must be recorded in `01-grpc-client-foundation-review.md`.
- Critique must be recorded in `01-grpc-client-foundation-review.md`.
- Reviewer all-clear must be recorded before phase completion.
