# gRPC client foundation Notes

Plan: [plan.md](./plan.md)
Phase: P01 / `01-grpc-client-foundation`

## Implementation context

- Phase goal is foundation only: choose and expose a native GCS gRPC client layer, auth/channel setup, error mapping, build guards, and fake-service seam.
- Do not add disk registration, `IObjectStorage`, metadata modes, or table-function routing in this phase.
- C++ implementation must use ClickHouse style, including Allman braces.
- Future build/test commands must redirect output to the build directory and have logs summarized by a subagent before review.

## Client-layer decision

Decision:
- P01 selects direct generated `google.storage.v2` gRPC stubs, not high-level `storage_grpc`/`MakeGrpcClient`.

Evidence:
- `contrib/google-cloud-cpp/google/cloud/storage/google_cloud_cpp_storage_grpc.cmake` shows upstream high-level `google-cloud-cpp::storage_grpc` exists and links `google-cloud-cpp::storage`, `google-cloud-cpp::storage_protos`, gRPC, common, and storage gRPC sources.
- `contrib/google-cloud-cpp/google/cloud/storage/grpc_plugin.cc` shows `MakeGrpcClient` can return the REST client when `GOOGLE_CLOUD_CPP_STORAGE_GRPC_CONFIG=none`, which conflicts with P01/P06 fail-closed native-gRPC expectations.
- `contrib/google-cloud-cpp-cmake/CMakeLists.txt` currently includes REST-oriented `google_cloud_cpp_storage` but already calls `google_cloud_cpp_add_library_protos(storage)` and links `google_cloud_cpp_storage_protos` into `ch_contrib::google_cloud_cpp`.
- `contrib/google-cloud-cpp-cmake/cmake/CompileProtos.cmake` generates both `.pb.*` and `.grpc.pb.*` outputs through `google_cloud_cpp_grpcpp_library`.
- `contrib/google-cloud-cpp/external/googleapis/protolists/storage.list` contains `@com_google_googleapis//google/storage/v2:storage.proto`, and `build/test_gcs_client_foundation_protos.log` confirms `google/storage/v2/storage.grpc.pb.cc` and `storage.pb.cc` build successfully.

Affected artifact:
- `src/IO/GCS/GCSClient.*` uses `google::storage::v2::Storage::StubInterface` through a small ClickHouse-owned wrapper and fake seam.

Uncertainty:
- Future phases must validate exact GCS write protocol semantics for `WriteObjectRequest` and object generation preconditions; P01 only exposes the request/stream entry points.

Next action:
- P03 should implement disk semantics on top of the direct-stub abstraction.

## Build and submodule state

- Current worktree has pre-existing unrelated `contrib` dirt: `contrib` is a symlink to `/Users/tanner/src/github.com/ClickHouse/ClickHouse/contrib`, so Git reports many tracked `contrib/*` files as deleted and `?? contrib`.
- `git submodule status -- contrib/google-cloud-cpp` fails with `expected 'contrib' in submodule path 'contrib/google-cloud-cpp' not to be a symbolic link`.
- Treat this as pre-existing worktree state. Do not reset, rebase, replace, or stage `contrib` while working this phase.
- No `contrib` source edits were needed for P01 because direct generated storage stubs are already exposed through `google_cloud_cpp_storage_protos`.

## Implemented artifacts

- `src/IO/GCS/GCSStatus.h` and `src/IO/GCS/GCSStatus.cpp`: native GCS status categories, gRPC status mapping, retry classification, and ClickHouse error-code mapping.
- `src/IO/GCS/GCSClient.h` and `src/IO/GCS/GCSClient.cpp`: direct-stub client wrapper, build availability guard, credential mode selection, endpoint/deadline context setup, unary request helpers, streaming request helpers, and fake stub/stream seam.
- `src/CMakeLists.txt`: adds `src/IO/GCS` to the `dbms` target, not `clickhouse_common_io`, so Google Cloud C++ linkage remains on `dbms`.
- `src/IO/tests/gtest_gcs_grpc_client.cpp`: targeted tests for credential-mode selection, status/error mapping, availability guard, fake unary requests, fake status failures, and fake streaming requests.

## Auth and channel scope

Decision:
- P01 supports first-scope GCE/service-account-oriented credentials through `google_default` and `service_account_key` modes, plus `insecure_for_tests` for fake/emulator tests.

Evidence:
- `src/IO/GCS/GCSClient.cpp` calls `google::cloud::MakeGoogleDefaultCredentials`, `MakeServiceAccountCredentials`, or `MakeInsecureCredentials`, then uses `google::cloud::internal::CreateAuthenticationStrategy` to create the gRPC channel and configure each `grpc::ClientContext`.
- `ClientSettings::endpoint` defaults to `storage.googleapis.com`, the direct-connectivity endpoint family planned for same-region GCE validation.

Affected artifact:
- `GCS::ClientSettings` and `GCS::createClient`.

Uncertainty:
- P01 does not promise broad ADC refresh-token, HMAC/XML, public/no-sign, or non-GCE auth behavior. Those modes are out of P01 unless a later phase explicitly adds them.

Next action:
- P06 must validate same-region GCE direct-connectivity behavior in the real environment.

## Verification notes

- `ninja -C build google_cloud_cpp_rpc_status_protos google_cloud_cpp_storage_protos > build/test_gcs_client_foundation_protos.log 2>&1` exited 0.
- Direct object compilation for `src/IO/GCS/GCSStatus.cpp`, `src/IO/GCS/GCSClient.cpp`, and `src/IO/tests/gtest_gcs_grpc_client.cpp` exited 0; evidence is `build/test_gcs_client_foundation_direct_objects.log`.
- `ninja -C build unit_tests_dbms > build/test_gcs_client_foundation_build.log 2>&1` failed before test execution due unrelated Rust vendoring: `syn` cannot find crate `quote`.
- A targeted object-build attempt in `build/test_gcs_client_foundation_objects.log` also failed before C++ object execution due unrelated Rust vendoring for `prql`/`syn`/`quote`.
- Reviewer agent re-reviewed after fixes and found no blocker/high findings, approving P01 with the Rust failure recorded as a verification deviation.

## Risks

- Risk: Future write semantics may need more than the raw `WriteObject` stream entry point. Mitigation: P03 owns protocol-level write behavior and fake-service disk scenarios.
- Risk: Current generic error mapping uses existing ClickHouse error codes rather than adding a distinct `GCS_ERROR`. Mitigation: P02/P06 can add identity/observability-specific errors if maintainers require them.
- Risk: Full `unit_tests_dbms` was not runnable in this worktree due unrelated Rust vendor state. Mitigation: direct GCS object compiles and proto targets passed; review records the deviation.

## Deferred or future work

- Native disk identity/config registration is deferred to P02 after P01 exports client-layer availability.
- `IObjectStorage` read/write behavior is deferred to P03 after P01 client and fake-service seams exist.
- `plain` and `plain_rewritable` metadata support is deferred to P04 after core disk operations work.
- `gcs` table-function gRPC routing is deferred to P05 after the client and core object operations exist.
- Environment-gated real GCE performance validation is deferred to P06 after correctness is implemented.

## Handoff summary

Current status:
- P01 implementation is complete pending commit; reviewer approved with the unrelated Rust verification deviation recorded.

Completed artifacts:
- `src/IO/GCS/GCSStatus.*`: status, retry, exception-code mapping, and gRPC status conversion.
- `src/IO/GCS/GCSClient.*`: direct generated-stub client foundation, credential/channel setup, request helpers, build guard, and fake seam.
- `src/IO/tests/gtest_gcs_grpc_client.cpp`: targeted foundation tests compiled successfully as an object.
- `src/CMakeLists.txt`: `src/IO/GCS` added to `dbms`.

Key decisions:
- Direct generated `google.storage.v2` stubs were selected because they are already generated by existing ClickHouse Google APIs wiring and avoid `MakeGrpcClient` REST fallback behavior.
- No `contrib` files were edited because existing `google_cloud_cpp_storage_protos` exposure is sufficient for direct stubs.
- P01 credential scope is `google_default`, `service_account_key`, and `insecure_for_tests` only.

Assumptions:
- Later disk/table-function phases can build on low-level generated-stub request and streaming entry points. Confidence: medium.
- The full build failure is unrelated to P01 because it occurs in Rust vendoring before GCS C++ object failures. Confidence: high.

Uncertainties:
- Exact native GCS write protocol details and generation preconditions remain for P03.

Next likely work:
- After this phase is committed and marked completed, start P02 task generation/work in a later session only.
