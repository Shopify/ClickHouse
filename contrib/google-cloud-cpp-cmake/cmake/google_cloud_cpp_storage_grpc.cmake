# ~~~
# Copyright 2022 Google LLC
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     https://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
# ~~~

# File copied from google-cloud-cpp/google/cloud/storage/google_cloud_cpp_storage_grpc.cmake with minor modifications.

add_library(
    google_cloud_cpp_storage_grpc # cmake-format: sort
    ${GOOGLE_CLOUD_CPP_STORAGE_DIR}/async/bucket_name.cc
    ${GOOGLE_CLOUD_CPP_STORAGE_DIR}/async/bucket_name.h
    ${GOOGLE_CLOUD_CPP_STORAGE_DIR}/async/client.cc
    ${GOOGLE_CLOUD_CPP_STORAGE_DIR}/async/client.h
    ${GOOGLE_CLOUD_CPP_STORAGE_DIR}/async/connection.h
    ${GOOGLE_CLOUD_CPP_STORAGE_DIR}/async/idempotency_policy.cc
    ${GOOGLE_CLOUD_CPP_STORAGE_DIR}/async/idempotency_policy.h
    ${GOOGLE_CLOUD_CPP_STORAGE_DIR}/async/object_responses.cc
    ${GOOGLE_CLOUD_CPP_STORAGE_DIR}/async/object_responses.h
    ${GOOGLE_CLOUD_CPP_STORAGE_DIR}/async/options.h
    ${GOOGLE_CLOUD_CPP_STORAGE_DIR}/async/read_all.cc
    ${GOOGLE_CLOUD_CPP_STORAGE_DIR}/async/read_all.h
    ${GOOGLE_CLOUD_CPP_STORAGE_DIR}/async/reader.cc
    ${GOOGLE_CLOUD_CPP_STORAGE_DIR}/async/reader.h
    ${GOOGLE_CLOUD_CPP_STORAGE_DIR}/async/reader_connection.h
    ${GOOGLE_CLOUD_CPP_STORAGE_DIR}/async/resume_policy.cc
    ${GOOGLE_CLOUD_CPP_STORAGE_DIR}/async/resume_policy.h
    ${GOOGLE_CLOUD_CPP_STORAGE_DIR}/async/rewriter.cc
    ${GOOGLE_CLOUD_CPP_STORAGE_DIR}/async/rewriter.h
    ${GOOGLE_CLOUD_CPP_STORAGE_DIR}/async/rewriter_connection.h
    ${GOOGLE_CLOUD_CPP_STORAGE_DIR}/async/token.h
    ${GOOGLE_CLOUD_CPP_STORAGE_DIR}/async/write_payload.h
    ${GOOGLE_CLOUD_CPP_STORAGE_DIR}/async/writer.cc
    ${GOOGLE_CLOUD_CPP_STORAGE_DIR}/async/writer.h
    ${GOOGLE_CLOUD_CPP_STORAGE_DIR}/async/writer_connection.h
    ${GOOGLE_CLOUD_CPP_STORAGE_DIR}/grpc_plugin.cc
    ${GOOGLE_CLOUD_CPP_STORAGE_DIR}/grpc_plugin.h
    ${GOOGLE_CLOUD_CPP_STORAGE_DIR}/internal/async/connection_fwd.h
    ${GOOGLE_CLOUD_CPP_STORAGE_DIR}/internal/async/connection_impl.cc
    ${GOOGLE_CLOUD_CPP_STORAGE_DIR}/internal/async/connection_impl.h
    ${GOOGLE_CLOUD_CPP_STORAGE_DIR}/internal/async/connection_tracing.cc
    ${GOOGLE_CLOUD_CPP_STORAGE_DIR}/internal/async/connection_tracing.h
    ${GOOGLE_CLOUD_CPP_STORAGE_DIR}/internal/async/default_options.cc
    ${GOOGLE_CLOUD_CPP_STORAGE_DIR}/internal/async/default_options.h
    ${GOOGLE_CLOUD_CPP_STORAGE_DIR}/internal/async/insert_object.cc
    ${GOOGLE_CLOUD_CPP_STORAGE_DIR}/internal/async/insert_object.h
    ${GOOGLE_CLOUD_CPP_STORAGE_DIR}/internal/async/partial_upload.cc
    ${GOOGLE_CLOUD_CPP_STORAGE_DIR}/internal/async/partial_upload.h
    ${GOOGLE_CLOUD_CPP_STORAGE_DIR}/internal/async/read_payload_fwd.h
    ${GOOGLE_CLOUD_CPP_STORAGE_DIR}/internal/async/read_payload_impl.h
    ${GOOGLE_CLOUD_CPP_STORAGE_DIR}/internal/async/reader_connection_factory.cc
    ${GOOGLE_CLOUD_CPP_STORAGE_DIR}/internal/async/reader_connection_factory.h
    ${GOOGLE_CLOUD_CPP_STORAGE_DIR}/internal/async/reader_connection_impl.cc
    ${GOOGLE_CLOUD_CPP_STORAGE_DIR}/internal/async/reader_connection_impl.h
    ${GOOGLE_CLOUD_CPP_STORAGE_DIR}/internal/async/reader_connection_resume.cc
    ${GOOGLE_CLOUD_CPP_STORAGE_DIR}/internal/async/reader_connection_resume.h
    ${GOOGLE_CLOUD_CPP_STORAGE_DIR}/internal/async/reader_connection_tracing.cc
    ${GOOGLE_CLOUD_CPP_STORAGE_DIR}/internal/async/reader_connection_tracing.h
    ${GOOGLE_CLOUD_CPP_STORAGE_DIR}/internal/async/rewriter_connection_impl.cc
    ${GOOGLE_CLOUD_CPP_STORAGE_DIR}/internal/async/rewriter_connection_impl.h
    ${GOOGLE_CLOUD_CPP_STORAGE_DIR}/internal/async/rewriter_connection_tracing.cc
    ${GOOGLE_CLOUD_CPP_STORAGE_DIR}/internal/async/rewriter_connection_tracing.h
    ${GOOGLE_CLOUD_CPP_STORAGE_DIR}/internal/async/token_impl.cc
    ${GOOGLE_CLOUD_CPP_STORAGE_DIR}/internal/async/token_impl.h
    ${GOOGLE_CLOUD_CPP_STORAGE_DIR}/internal/async/write_payload_fwd.h
    ${GOOGLE_CLOUD_CPP_STORAGE_DIR}/internal/async/write_payload_impl.h
    ${GOOGLE_CLOUD_CPP_STORAGE_DIR}/internal/async/writer_connection_buffered.cc
    ${GOOGLE_CLOUD_CPP_STORAGE_DIR}/internal/async/writer_connection_buffered.h
    ${GOOGLE_CLOUD_CPP_STORAGE_DIR}/internal/async/writer_connection_finalized.cc
    ${GOOGLE_CLOUD_CPP_STORAGE_DIR}/internal/async/writer_connection_finalized.h
    ${GOOGLE_CLOUD_CPP_STORAGE_DIR}/internal/async/writer_connection_impl.cc
    ${GOOGLE_CLOUD_CPP_STORAGE_DIR}/internal/async/writer_connection_impl.h
    ${GOOGLE_CLOUD_CPP_STORAGE_DIR}/internal/async/writer_connection_tracing.cc
    ${GOOGLE_CLOUD_CPP_STORAGE_DIR}/internal/async/writer_connection_tracing.h
    ${GOOGLE_CLOUD_CPP_STORAGE_DIR}/internal/grpc/bucket_access_control_parser.cc
    ${GOOGLE_CLOUD_CPP_STORAGE_DIR}/internal/grpc/bucket_access_control_parser.h
    ${GOOGLE_CLOUD_CPP_STORAGE_DIR}/internal/grpc/bucket_metadata_parser.cc
    ${GOOGLE_CLOUD_CPP_STORAGE_DIR}/internal/grpc/bucket_metadata_parser.h
    ${GOOGLE_CLOUD_CPP_STORAGE_DIR}/internal/grpc/bucket_name.cc
    ${GOOGLE_CLOUD_CPP_STORAGE_DIR}/internal/grpc/bucket_name.h
    ${GOOGLE_CLOUD_CPP_STORAGE_DIR}/internal/grpc/bucket_request_parser.cc
    ${GOOGLE_CLOUD_CPP_STORAGE_DIR}/internal/grpc/bucket_request_parser.h
    ${GOOGLE_CLOUD_CPP_STORAGE_DIR}/internal/grpc/buffer_read_object_data.cc
    ${GOOGLE_CLOUD_CPP_STORAGE_DIR}/internal/grpc/buffer_read_object_data.h
    ${GOOGLE_CLOUD_CPP_STORAGE_DIR}/internal/grpc/channel_refresh.cc
    ${GOOGLE_CLOUD_CPP_STORAGE_DIR}/internal/grpc/channel_refresh.h
    ${GOOGLE_CLOUD_CPP_STORAGE_DIR}/internal/grpc/configure_client_context.cc
    ${GOOGLE_CLOUD_CPP_STORAGE_DIR}/internal/grpc/configure_client_context.h
    ${GOOGLE_CLOUD_CPP_STORAGE_DIR}/internal/grpc/ctype_cord_workaround.h
    ${GOOGLE_CLOUD_CPP_STORAGE_DIR}/internal/grpc/default_options.cc
    ${GOOGLE_CLOUD_CPP_STORAGE_DIR}/internal/grpc/default_options.h
    ${GOOGLE_CLOUD_CPP_STORAGE_DIR}/internal/grpc/enable_metrics.cc
    ${GOOGLE_CLOUD_CPP_STORAGE_DIR}/internal/grpc/enable_metrics.h
    ${GOOGLE_CLOUD_CPP_STORAGE_DIR}/internal/grpc/make_cord.cc
    ${GOOGLE_CLOUD_CPP_STORAGE_DIR}/internal/grpc/make_cord.h
    ${GOOGLE_CLOUD_CPP_STORAGE_DIR}/internal/grpc/metrics_exporter_impl.cc
    ${GOOGLE_CLOUD_CPP_STORAGE_DIR}/internal/grpc/metrics_exporter_impl.h
    ${GOOGLE_CLOUD_CPP_STORAGE_DIR}/internal/grpc/metrics_exporter_options.cc
    ${GOOGLE_CLOUD_CPP_STORAGE_DIR}/internal/grpc/metrics_exporter_options.h
    ${GOOGLE_CLOUD_CPP_STORAGE_DIR}/internal/grpc/metrics_histograms.cc
    ${GOOGLE_CLOUD_CPP_STORAGE_DIR}/internal/grpc/metrics_histograms.h
    ${GOOGLE_CLOUD_CPP_STORAGE_DIR}/internal/grpc/metrics_meter_provider.cc
    ${GOOGLE_CLOUD_CPP_STORAGE_DIR}/internal/grpc/metrics_meter_provider.h
    ${GOOGLE_CLOUD_CPP_STORAGE_DIR}/internal/grpc/monitoring_project.cc
    ${GOOGLE_CLOUD_CPP_STORAGE_DIR}/internal/grpc/monitoring_project.h
    ${GOOGLE_CLOUD_CPP_STORAGE_DIR}/internal/grpc/object_access_control_parser.cc
    ${GOOGLE_CLOUD_CPP_STORAGE_DIR}/internal/grpc/object_access_control_parser.h
    ${GOOGLE_CLOUD_CPP_STORAGE_DIR}/internal/grpc/object_metadata_parser.cc
    ${GOOGLE_CLOUD_CPP_STORAGE_DIR}/internal/grpc/object_metadata_parser.h
    ${GOOGLE_CLOUD_CPP_STORAGE_DIR}/internal/grpc/object_read_source.cc
    ${GOOGLE_CLOUD_CPP_STORAGE_DIR}/internal/grpc/object_read_source.h
    ${GOOGLE_CLOUD_CPP_STORAGE_DIR}/internal/grpc/object_request_parser.cc
    ${GOOGLE_CLOUD_CPP_STORAGE_DIR}/internal/grpc/object_request_parser.h
    ${GOOGLE_CLOUD_CPP_STORAGE_DIR}/internal/grpc/owner_parser.cc
    ${GOOGLE_CLOUD_CPP_STORAGE_DIR}/internal/grpc/owner_parser.h
    ${GOOGLE_CLOUD_CPP_STORAGE_DIR}/internal/grpc/scale_stall_timeout.cc
    ${GOOGLE_CLOUD_CPP_STORAGE_DIR}/internal/grpc/scale_stall_timeout.h
    ${GOOGLE_CLOUD_CPP_STORAGE_DIR}/internal/grpc/sign_blob_request_parser.cc
    ${GOOGLE_CLOUD_CPP_STORAGE_DIR}/internal/grpc/sign_blob_request_parser.h
    ${GOOGLE_CLOUD_CPP_STORAGE_DIR}/internal/grpc/split_write_object_data.cc
    ${GOOGLE_CLOUD_CPP_STORAGE_DIR}/internal/grpc/split_write_object_data.h
    ${GOOGLE_CLOUD_CPP_STORAGE_DIR}/internal/grpc/stub.cc
    ${GOOGLE_CLOUD_CPP_STORAGE_DIR}/internal/grpc/stub.h
    ${GOOGLE_CLOUD_CPP_STORAGE_DIR}/internal/grpc/synthetic_self_link.cc
    ${GOOGLE_CLOUD_CPP_STORAGE_DIR}/internal/grpc/synthetic_self_link.h
    ${GOOGLE_CLOUD_CPP_STORAGE_DIR}/internal/storage_auth_decorator.cc
    ${GOOGLE_CLOUD_CPP_STORAGE_DIR}/internal/storage_auth_decorator.h
    ${GOOGLE_CLOUD_CPP_STORAGE_DIR}/internal/storage_logging_decorator.cc
    ${GOOGLE_CLOUD_CPP_STORAGE_DIR}/internal/storage_logging_decorator.h
    ${GOOGLE_CLOUD_CPP_STORAGE_DIR}/internal/storage_metadata_decorator.cc
    ${GOOGLE_CLOUD_CPP_STORAGE_DIR}/internal/storage_metadata_decorator.h
    ${GOOGLE_CLOUD_CPP_STORAGE_DIR}/internal/storage_round_robin_decorator.cc
    ${GOOGLE_CLOUD_CPP_STORAGE_DIR}/internal/storage_round_robin_decorator.h
    ${GOOGLE_CLOUD_CPP_STORAGE_DIR}/internal/storage_stub.cc
    ${GOOGLE_CLOUD_CPP_STORAGE_DIR}/internal/storage_stub.h
    ${GOOGLE_CLOUD_CPP_STORAGE_DIR}/internal/storage_stub_factory.cc
    ${GOOGLE_CLOUD_CPP_STORAGE_DIR}/internal/storage_stub_factory.h
    ${GOOGLE_CLOUD_CPP_STORAGE_DIR}/internal/storage_tracing_stub.cc
    ${GOOGLE_CLOUD_CPP_STORAGE_DIR}/internal/storage_tracing_stub.h)
target_link_libraries(
    google_cloud_cpp_storage_grpc
    PUBLIC google-cloud-cpp::storage
           google-cloud-cpp::storage_protos
           google-cloud-cpp::grpc_utils
           google-cloud-cpp::common
           nlohmann_json::nlohmann_json
           gRPC::grpc++
           absl::optional
           absl::strings
           absl::time
           Threads::Threads)
target_include_directories(
    google_cloud_cpp_storage_grpc
    PUBLIC $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}>
           $<INSTALL_INTERFACE:include>)
target_compile_options(google_cloud_cpp_storage_grpc
                       PUBLIC ${GOOGLE_CLOUD_CPP_EXCEPTIONS_FLAG})
target_compile_definitions(google_cloud_cpp_storage_grpc
                           PUBLIC GOOGLE_CLOUD_CPP_STORAGE_HAVE_GRPC)
set_target_properties(
    google_cloud_cpp_storage_grpc
    PROPERTIES EXPORT_NAME "google-cloud-cpp::storage_grpc"
               VERSION ${PROJECT_VERSION}
               SOVERSION ${PROJECT_VERSION_MAJOR})
add_library(google-cloud-cpp::storage_grpc ALIAS google_cloud_cpp_storage_grpc)
