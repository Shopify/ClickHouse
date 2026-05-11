#include <IO/GCS/GCSClient.h>
#include <IO/GCS/GCSStatus.h>

#include <gtest/gtest.h>
#include <Common/CurrentThread.h>
#include <Common/Exception.h>
#include <Common/ProfileEvents.h>
#include <Common/Throttler.h>

#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace DB::ErrorCodes
{
extern const int ACCESS_DENIED;
extern const int BAD_ARGUMENTS;
extern const int FILE_DOESNT_EXIST;
extern const int NETWORK_ERROR;
extern const int NOT_IMPLEMENTED;
extern const int TIMEOUT_EXCEEDED;
}

namespace ProfileEvents
{
extern const Event GCSGetObject;
extern const Event GCSListObjects;
extern const Event GCSDeleteObject;
extern const Event GCSReadObject;
extern const Event GCSWriteObject;
extern const Event GCSReadRequestsCount;
extern const Event GCSReadRequestsErrors;
extern const Event GCSReadRequestsThrottling;
extern const Event GCSReadRequestAttempts;
extern const Event GCSReadRequestRetryableErrors;
extern const Event GCSWriteRequestsCount;
extern const Event GCSWriteRequestsErrors;
extern const Event GCSWriteRequestsThrottling;
extern const Event GCSWriteRequestAttempts;
extern const Event GCSWriteRequestRetryableErrors;
extern const Event GCSGetRequestThrottlerCount;
extern const Event GCSGetRequestThrottlerBlocked;
extern const Event GCSGetRequestThrottlerSleepMicroseconds;
extern const Event GCSPutRequestThrottlerCount;
extern const Event GCSPutRequestThrottlerBlocked;
extern const Event GCSPutRequestThrottlerSleepMicroseconds;
extern const Event DiskGCSGetRequestThrottlerCount;
extern const Event DiskGCSGetRequestThrottlerBlocked;
extern const Event DiskGCSGetRequestThrottlerSleepMicroseconds;
extern const Event DiskGCSPutRequestThrottlerCount;
extern const Event DiskGCSPutRequestThrottlerBlocked;
extern const Event DiskGCSPutRequestThrottlerSleepMicroseconds;
}

using namespace DB;

TEST(GCSGrpcClientFoundation, CredentialMode)
{
    GCS::ClientSettings settings;
    EXPECT_EQ(GCS::CredentialMode::GoogleDefault, GCS::credentialMode(settings));
    EXPECT_STREQ("google_default", GCS::credentialModeName(GCS::credentialMode(settings)));

    settings.service_account_json = "{}";
    EXPECT_EQ(GCS::CredentialMode::ServiceAccountKey, GCS::credentialMode(settings));
    EXPECT_STREQ("service_account_key", GCS::credentialModeName(GCS::credentialMode(settings)));

    settings.use_insecure_credentials_for_tests = true;
    EXPECT_EQ(GCS::CredentialMode::InsecureForTests, GCS::credentialMode(settings));
    EXPECT_STREQ("insecure_for_tests", GCS::credentialModeName(GCS::credentialMode(settings)));
}

TEST(GCSGrpcClientFoundation, ErrorCodeMapping)
{
    EXPECT_EQ(0, GCS::errorCodeForStatus(GCS::StatusCode::OK));
    EXPECT_EQ(ErrorCodes::FILE_DOESNT_EXIST, GCS::errorCodeForStatus(GCS::StatusCode::NotFound));
    EXPECT_EQ(ErrorCodes::ACCESS_DENIED, GCS::errorCodeForStatus(GCS::StatusCode::PermissionDenied));
    EXPECT_EQ(ErrorCodes::TIMEOUT_EXCEEDED, GCS::errorCodeForStatus(GCS::StatusCode::DeadlineExceeded));
    EXPECT_EQ(ErrorCodes::NETWORK_ERROR, GCS::errorCodeForStatus(GCS::StatusCode::ResourceExhausted));
    EXPECT_EQ(ErrorCodes::NETWORK_ERROR, GCS::errorCodeForStatus(GCS::StatusCode::Unavailable));
    EXPECT_EQ(ErrorCodes::BAD_ARGUMENTS, GCS::errorCodeForStatus(GCS::StatusCode::InvalidArgument));
    EXPECT_EQ(ErrorCodes::NOT_IMPLEMENTED, GCS::errorCodeForStatus(GCS::StatusCode::Unsupported));

    EXPECT_TRUE(GCS::isRetryableStatus(GCS::StatusCode::ResourceExhausted));
    EXPECT_TRUE(GCS::isRetryableStatus(GCS::StatusCode::Unavailable));
    EXPECT_TRUE(GCS::isRetryableStatus(GCS::StatusCode::DeadlineExceeded));
    EXPECT_FALSE(GCS::isRetryableStatus(GCS::StatusCode::NotFound));

    EXPECT_TRUE(GCS::isThrottlingStatus(GCS::StatusCode::ResourceExhausted));
    EXPECT_FALSE(GCS::isThrottlingStatus(GCS::StatusCode::Unavailable));
    EXPECT_FALSE(GCS::isThrottlingStatus(GCS::StatusCode::DeadlineExceeded));
    EXPECT_FALSE(GCS::isThrottlingStatus(GCS::StatusCode::NotFound));
}

TEST(GCSGrpcClientFoundation, AvailabilityGuard)
{
#if USE_GOOGLE_CLOUD
    EXPECT_TRUE(GCS::isGrpcAvailable());
    EXPECT_NO_THROW(GCS::assertGrpcAvailable());
#else
    EXPECT_FALSE(GCS::isGrpcAvailable());
    EXPECT_THROW(GCS::assertGrpcAvailable(), DB::Exception);
#endif
}

#if USE_GOOGLE_CLOUD
namespace
{

std::vector<std::string> metadataValues(const std::multimap<std::string, std::string> & metadata, const std::string & key)
{
    std::vector<std::string> values;
    auto range = metadata.equal_range(key);
    for (auto it = range.first; it != range.second; ++it)
        values.push_back(it->second);
    return values;
}

void expectSingleMetadata(const std::multimap<std::string, std::string> & metadata, const std::string & key, const std::string & value)
{
    auto values = metadataValues(metadata, key);
    ASSERT_EQ(1, values.size());
    EXPECT_EQ(value, values.front());
}

UInt64 profileEventValue(ProfileEvents::Event event)
{
    return CurrentThread::getProfileEvents()[event];
}

void resetProfileEvents()
{
    CurrentThread::getProfileEvents().reset();
}

std::shared_ptr<Throttler> blockingRequestThrottler(ProfileEvents::Event amount, ProfileEvents::Event sleep)
{
    return std::make_shared<Throttler>(1000, 0, nullptr, amount, sleep);
}

class FailingAuth final : public google::cloud::internal::GrpcAuthenticationStrategy
{
public:
    explicit FailingAuth(google::cloud::Status failure_)
        : failure(std::move(failure_))
    {
    }

    std::shared_ptr<grpc::Channel> CreateChannel(std::string const &, grpc::ChannelArguments const &) override
    {
        return grpc::CreateChannel("localhost", grpc::InsecureChannelCredentials());
    }

    bool RequiresConfigureContext() const override { return true; }

    google::cloud::Status ConfigureContext(grpc::ClientContext &) override { return failure; }

    google::cloud::future<google::cloud::StatusOr<std::shared_ptr<grpc::ClientContext>>>
    AsyncConfigureContext(std::shared_ptr<grpc::ClientContext>) override
    {
        return google::cloud::make_ready_future<google::cloud::StatusOr<std::shared_ptr<grpc::ClientContext>>>(failure);
    }

private:
    google::cloud::Status failure;
};

}

TEST(GCSGrpcClientFoundation, GrpcStatusIncludesDetailsAndNumericCode)
{
    auto status = GCS::fromGrpcStatus(grpc::Status(grpc::StatusCode::PERMISSION_DENIED, "denied", "quota project missing"));

    EXPECT_EQ(GCS::StatusCode::PermissionDenied, status.code);
    EXPECT_NE(std::string::npos, status.message.find("denied"));
    EXPECT_NE(std::string::npos, status.message.find("details: quota project missing"));
    EXPECT_NE(std::string::npos, status.message.find("grpc_status_code: 7"));
}

TEST(GCSGrpcClientFoundation, GrpcStatusMapping)
{
    EXPECT_TRUE(GCS::fromGrpcStatus(grpc::Status::OK).ok());
    EXPECT_EQ(GCS::StatusCode::NotFound, GCS::fromGrpcStatus(grpc::Status(grpc::StatusCode::NOT_FOUND, "missing")).code);
    EXPECT_EQ(GCS::StatusCode::PermissionDenied, GCS::fromGrpcStatus(grpc::Status(grpc::StatusCode::PERMISSION_DENIED, "denied")).code);
    EXPECT_EQ(
        GCS::StatusCode::PermissionDenied, GCS::fromGrpcStatus(grpc::Status(grpc::StatusCode::UNAUTHENTICATED, "unauthenticated")).code);
    EXPECT_EQ(GCS::StatusCode::DeadlineExceeded, GCS::fromGrpcStatus(grpc::Status(grpc::StatusCode::DEADLINE_EXCEEDED, "deadline")).code);
    EXPECT_EQ(GCS::StatusCode::Unavailable, GCS::fromGrpcStatus(grpc::Status(grpc::StatusCode::UNAVAILABLE, "unavailable")).code);
    EXPECT_EQ(
        GCS::StatusCode::ResourceExhausted,
        GCS::fromGrpcStatus(grpc::Status(grpc::StatusCode::RESOURCE_EXHAUSTED, "resource exhausted")).code);
    EXPECT_EQ(GCS::StatusCode::Unsupported, GCS::fromGrpcStatus(grpc::Status(grpc::StatusCode::UNIMPLEMENTED, "unsupported")).code);
}

TEST(GCSGrpcClientFoundation, FakeUnaryRequests)
{
    auto fake_stub = std::make_shared<GCS::FakeStub>();
    fake_stub->get_object_response.set_name("projects/_/buckets/test/objects/path");

    GCS::ClientSettings settings;
    settings.request_timeout_ms = 1000;
    settings.user_project = "billing-project";

    GCS::Client client(settings, fake_stub);

    google::storage::v2::GetObjectRequest request;
    request.set_bucket("projects/_/buckets/test");
    request.set_object("path");

    auto result = client.getObject(request);
    ASSERT_TRUE(result.ok()) << result.status.message;
    EXPECT_EQ("projects/_/buckets/test/objects/path", result.response.name());
    ASSERT_EQ(1, fake_stub->get_object_requests.size());
    EXPECT_EQ("path", fake_stub->get_object_requests.front().object());
    EXPECT_GE(fake_stub->last_deadline, std::chrono::system_clock::now());
}

TEST(GCSGrpcClientFoundation, RoutingMetadataForUnaryRequests)
{
    auto fake_stub = std::make_shared<GCS::FakeStub>();

    GCS::ClientSettings settings;
    settings.user_project = "billing-project";
    GCS::Client client(settings, fake_stub);

    google::storage::v2::GetObjectRequest get_request;
    get_request.set_bucket("projects/_/buckets/foo");
    get_request.set_object("path");
    ASSERT_TRUE(client.getObject(get_request).ok());
    expectSingleMetadata(fake_stub->last_metadata, "x-goog-request-params", "bucket=projects%2F_%2Fbuckets%2Ffoo");
    expectSingleMetadata(fake_stub->last_metadata, "x-goog-user-project", "billing-project");

    google::storage::v2::ListObjectsRequest list_request;
    list_request.set_parent("projects/_/buckets/foo");
    ASSERT_TRUE(client.listObjects(list_request).ok());
    expectSingleMetadata(fake_stub->last_metadata, "x-goog-request-params", "bucket=projects%2F_%2Fbuckets%2Ffoo");
    expectSingleMetadata(fake_stub->last_metadata, "x-goog-user-project", "billing-project");

    google::storage::v2::DeleteObjectRequest delete_request;
    delete_request.set_bucket("projects/_/buckets/foo");
    delete_request.set_object("path");
    EXPECT_TRUE(client.deleteObject(delete_request).ok());
    expectSingleMetadata(fake_stub->last_metadata, "x-goog-request-params", "bucket=projects%2F_%2Fbuckets%2Ffoo");
    expectSingleMetadata(fake_stub->last_metadata, "x-goog-user-project", "billing-project");
}

TEST(GCSGrpcClientFoundation, RoutingMetadataForReadStream)
{
    auto fake_stub = std::make_shared<GCS::FakeStub>();
    GCS::Client client({}, fake_stub);

    google::storage::v2::ReadObjectRequest request;
    request.set_bucket("projects/_/buckets/foo");
    request.set_object("path");

    auto stream = client.readObject(request);
    ASSERT_TRUE(stream.ok()) << stream.status.message;
    expectSingleMetadata(fake_stub->last_metadata, "x-goog-request-params", "bucket=projects%2F_%2Fbuckets%2Ffoo");
}

TEST(GCSGrpcClientFoundation, RoutingMetadataForWriteStream)
{
    auto fake_stub = std::make_shared<GCS::FakeStub>();
    GCS::Client client({}, fake_stub);

    google::storage::v2::WriteObjectResponse response;
    auto stream = client.writeObject(response, "projects/_/buckets/foo");
    ASSERT_TRUE(stream.ok()) << stream.status.message;
    expectSingleMetadata(fake_stub->last_metadata, "x-goog-request-params", "bucket=projects%2F_%2Fbuckets%2Ffoo");
    EXPECT_TRUE(stream.stream->WritesDone());
    EXPECT_TRUE(stream.stream->Finish().ok());
}

TEST(GCSGrpcClientFoundation, AuthContextFailurePropagation)
{
    auto fake_stub = std::make_shared<GCS::FakeStub>();
    auto auth = std::make_shared<FailingAuth>(google::cloud::Status(google::cloud::StatusCode::kPermissionDenied, "auth denied"));
    GCS::Client client({}, fake_stub, auth);

    google::storage::v2::GetObjectRequest get_request;
    get_request.set_bucket("projects/_/buckets/foo");
    auto get_result = client.getObject(get_request);
    EXPECT_FALSE(get_result.ok());
    EXPECT_EQ(GCS::StatusCode::PermissionDenied, get_result.status.code);
    EXPECT_NE(std::string::npos, get_result.status.message.find("auth denied"));
    EXPECT_TRUE(fake_stub->get_object_requests.empty());

    google::storage::v2::ReadObjectRequest read_request;
    read_request.set_bucket("projects/_/buckets/foo");
    auto read_result = client.readObject(read_request);
    EXPECT_FALSE(read_result.ok());
    EXPECT_EQ(GCS::StatusCode::PermissionDenied, read_result.status.code);
    EXPECT_EQ(nullptr, read_result.stream.get());
    EXPECT_TRUE(fake_stub->read_object_requests.empty());
}

TEST(GCSGrpcClientFoundation, FakeStatusFailure)
{
    auto fake_stub = std::make_shared<GCS::FakeStub>();
    fake_stub->get_object_status = grpc::Status(grpc::StatusCode::NOT_FOUND, "missing");

    GCS::Client client({}, fake_stub);
    google::storage::v2::GetObjectRequest request;

    auto result = client.getObject(request);
    EXPECT_FALSE(result.ok());
    EXPECT_EQ(GCS::StatusCode::NotFound, result.status.code);
}

TEST(GCSGrpcClientFoundation, FakeStreamingRequests)
{
    auto fake_stub = std::make_shared<GCS::FakeStub>();

    google::storage::v2::ReadObjectResponse first_read;
    first_read.mutable_checksummed_data()->set_content("abc");
    google::storage::v2::ReadObjectResponse second_read;
    second_read.mutable_checksummed_data()->set_content("def");
    fake_stub->read_object_responses = {first_read, second_read};

    GCS::Client client({}, fake_stub);

    google::storage::v2::ReadObjectRequest read_request;
    read_request.set_bucket("projects/_/buckets/test");
    read_request.set_object("path");

    auto read_stream = client.readObject(read_request);
    ASSERT_TRUE(read_stream.ok()) << read_stream.status.message;

    google::storage::v2::ReadObjectResponse read_response;
    ASSERT_TRUE(read_stream.stream->Read(&read_response));
    EXPECT_EQ("abc", read_response.checksummed_data().content());
    ASSERT_TRUE(read_stream.stream->Read(&read_response));
    EXPECT_EQ("def", read_response.checksummed_data().content());
    EXPECT_FALSE(read_stream.stream->Read(&read_response));
    EXPECT_TRUE(read_stream.stream->Finish().ok());

    google::storage::v2::WriteObjectResponse write_response;
    auto write_stream = client.writeObject(write_response, "projects/_/buckets/test");
    ASSERT_TRUE(write_stream.ok()) << write_stream.status.message;

    google::storage::v2::WriteObjectRequest write_request;
    write_request.mutable_write_object_spec()->mutable_resource()->set_bucket("projects/_/buckets/test");
    write_request.mutable_write_object_spec()->mutable_resource()->set_name("path");
    ASSERT_TRUE(write_stream.stream->Write(write_request));
    EXPECT_TRUE(write_stream.stream->WritesDone());
    EXPECT_TRUE(write_stream.stream->Finish().ok());
}

TEST(GCSGrpcClientFoundation, RetryableUnaryRequestRetriesAndAccounts)
{
    resetProfileEvents();

    auto fake_stub = std::make_shared<GCS::FakeStub>();
    fake_stub->get_object_statuses = {grpc::Status(grpc::StatusCode::UNAVAILABLE, "temporarily unavailable")};
    fake_stub->get_object_response.set_name("projects/_/buckets/test/objects/path");

    GCS::ClientSettings settings;
    settings.max_retry_attempts = 2;
    GCS::Client client(settings, fake_stub);

    google::storage::v2::GetObjectRequest request;
    request.set_bucket("projects/_/buckets/test");
    request.set_object("path");

    auto result = client.getObject(request);
    ASSERT_TRUE(result.ok()) << result.status.message;
    EXPECT_EQ(2, fake_stub->get_object_requests.size());
    EXPECT_EQ(2, profileEventValue(ProfileEvents::GCSGetObject));
    EXPECT_EQ(2, profileEventValue(ProfileEvents::GCSReadRequestsCount));
    EXPECT_EQ(2, profileEventValue(ProfileEvents::GCSReadRequestAttempts));
    EXPECT_EQ(1, profileEventValue(ProfileEvents::GCSReadRequestRetryableErrors));
    EXPECT_EQ(1, profileEventValue(ProfileEvents::GCSReadRequestsErrors));
    EXPECT_EQ(0, profileEventValue(ProfileEvents::GCSReadRequestsThrottling));
}

TEST(GCSGrpcClientFoundation, ThrottledUnaryRequestRetriesAndAccounts)
{
    resetProfileEvents();

    auto fake_stub = std::make_shared<GCS::FakeStub>();
    fake_stub->list_objects_statuses = {grpc::Status(grpc::StatusCode::RESOURCE_EXHAUSTED, "quota exhausted")};

    GCS::ClientSettings settings;
    settings.max_retry_attempts = 2;
    GCS::Client client(settings, fake_stub);

    google::storage::v2::ListObjectsRequest request;
    request.set_parent("projects/_/buckets/test");

    auto result = client.listObjects(request);
    ASSERT_TRUE(result.ok()) << result.status.message;
    EXPECT_EQ(2, fake_stub->list_objects_requests.size());
    EXPECT_EQ(2, profileEventValue(ProfileEvents::GCSListObjects));
    EXPECT_EQ(2, profileEventValue(ProfileEvents::GCSWriteRequestsCount));
    EXPECT_EQ(2, profileEventValue(ProfileEvents::GCSWriteRequestAttempts));
    EXPECT_EQ(1, profileEventValue(ProfileEvents::GCSWriteRequestRetryableErrors));
    EXPECT_EQ(1, profileEventValue(ProfileEvents::GCSWriteRequestsThrottling));
    EXPECT_EQ(0, profileEventValue(ProfileEvents::GCSWriteRequestsErrors));
}

TEST(GCSGrpcClientFoundation, NonRetryableUnaryRequestFailsOnceAndAccounts)
{
    resetProfileEvents();

    auto fake_stub = std::make_shared<GCS::FakeStub>();
    fake_stub->delete_object_status = grpc::Status(grpc::StatusCode::PERMISSION_DENIED, "denied");

    GCS::ClientSettings settings;
    settings.max_retry_attempts = 3;
    GCS::Client client(settings, fake_stub);

    google::storage::v2::DeleteObjectRequest request;
    request.set_bucket("projects/_/buckets/test");
    request.set_object("path");

    auto status = client.deleteObject(request);
    EXPECT_FALSE(status.ok());
    EXPECT_EQ(GCS::StatusCode::PermissionDenied, status.code);
    EXPECT_EQ(1, fake_stub->delete_object_requests.size());
    EXPECT_EQ(1, profileEventValue(ProfileEvents::GCSDeleteObject));
    EXPECT_EQ(1, profileEventValue(ProfileEvents::GCSWriteRequestAttempts));
    EXPECT_EQ(1, profileEventValue(ProfileEvents::GCSWriteRequestsErrors));
    EXPECT_EQ(0, profileEventValue(ProfileEvents::GCSWriteRequestRetryableErrors));
    EXPECT_EQ(0, profileEventValue(ProfileEvents::GCSWriteRequestsThrottling));
}

TEST(GCSGrpcClientFoundation, RequestThrottlersAccountProviderAndDiskEvents)
{
    resetProfileEvents();

    auto fake_stub = std::make_shared<GCS::FakeStub>();
    fake_stub->get_object_response.set_name("projects/_/buckets/test/objects/path");

    GCS::ClientSettings settings;
    settings.for_disk = true;
    settings.request_throttler.get_throttler
        = blockingRequestThrottler(ProfileEvents::GCSGetRequestThrottlerCount, ProfileEvents::GCSGetRequestThrottlerSleepMicroseconds);
    settings.request_throttler.put_throttler
        = blockingRequestThrottler(ProfileEvents::GCSPutRequestThrottlerCount, ProfileEvents::GCSPutRequestThrottlerSleepMicroseconds);
    GCS::Client client(settings, fake_stub);

    google::storage::v2::GetObjectRequest get_request;
    get_request.set_bucket("projects/_/buckets/test");
    get_request.set_object("path");
    ASSERT_TRUE(client.getObject(get_request).ok());

    google::storage::v2::ListObjectsRequest list_request;
    list_request.set_parent("projects/_/buckets/test");
    ASSERT_TRUE(client.listObjects(list_request).ok());

    EXPECT_EQ(1, profileEventValue(ProfileEvents::GCSGetRequestThrottlerCount));
    EXPECT_EQ(1, profileEventValue(ProfileEvents::GCSGetRequestThrottlerBlocked));
    EXPECT_GT(profileEventValue(ProfileEvents::GCSGetRequestThrottlerSleepMicroseconds), 0);
    EXPECT_EQ(1, profileEventValue(ProfileEvents::DiskGCSGetRequestThrottlerCount));
    EXPECT_EQ(1, profileEventValue(ProfileEvents::DiskGCSGetRequestThrottlerBlocked));
    EXPECT_GT(profileEventValue(ProfileEvents::DiskGCSGetRequestThrottlerSleepMicroseconds), 0);

    EXPECT_EQ(1, profileEventValue(ProfileEvents::GCSPutRequestThrottlerCount));
    EXPECT_EQ(1, profileEventValue(ProfileEvents::GCSPutRequestThrottlerBlocked));
    EXPECT_GT(profileEventValue(ProfileEvents::GCSPutRequestThrottlerSleepMicroseconds), 0);
    EXPECT_EQ(1, profileEventValue(ProfileEvents::DiskGCSPutRequestThrottlerCount));
    EXPECT_EQ(1, profileEventValue(ProfileEvents::DiskGCSPutRequestThrottlerBlocked));
    EXPECT_GT(profileEventValue(ProfileEvents::DiskGCSPutRequestThrottlerSleepMicroseconds), 0);
}

TEST(GCSGrpcClientFoundation, StreamCreationRetriesBeforeReturningStream)
{
    resetProfileEvents();

    auto fake_stub = std::make_shared<GCS::FakeStub>();
    fake_stub->read_object_null_streams = 1;
    google::storage::v2::ReadObjectResponse read_response;
    read_response.mutable_checksummed_data()->set_content("abc");
    fake_stub->read_object_responses = {read_response};

    GCS::ClientSettings settings;
    settings.max_retry_attempts = 2;
    GCS::Client client(settings, fake_stub);

    google::storage::v2::ReadObjectRequest request;
    request.set_bucket("projects/_/buckets/test");
    request.set_object("path");

    auto stream = client.readObject(request);
    ASSERT_TRUE(stream.ok()) << stream.status.message;
    EXPECT_EQ(2, fake_stub->read_object_requests.size());
    EXPECT_EQ(2, profileEventValue(ProfileEvents::GCSReadObject));
    EXPECT_EQ(2, profileEventValue(ProfileEvents::GCSReadRequestAttempts));
    EXPECT_EQ(1, profileEventValue(ProfileEvents::GCSReadRequestRetryableErrors));
    EXPECT_EQ(1, profileEventValue(ProfileEvents::GCSReadRequestsErrors));
    EXPECT_TRUE(stream.stream->Finish().ok());
}

TEST(GCSGrpcClientFoundation, WriteStreamFailureAfterPayloadIsNotReplayed)
{
    resetProfileEvents();

    auto fake_stub = std::make_shared<GCS::FakeStub>();
    fake_stub->write_object_finish_status = grpc::Status(grpc::StatusCode::UNAVAILABLE, "finish failed");

    GCS::ClientSettings settings;
    settings.max_retry_attempts = 3;
    GCS::Client client(settings, fake_stub);

    google::storage::v2::WriteObjectResponse response;
    auto stream = client.writeObject(response, "projects/_/buckets/test");
    ASSERT_TRUE(stream.ok()) << stream.status.message;

    google::storage::v2::WriteObjectRequest write_request;
    write_request.mutable_write_object_spec()->mutable_resource()->set_bucket("projects/_/buckets/test");
    write_request.mutable_write_object_spec()->mutable_resource()->set_name("path");
    ASSERT_TRUE(stream.stream->Write(write_request));
    ASSERT_TRUE(stream.stream->WritesDone());

    auto finish_status = stream.stream->Finish();
    EXPECT_FALSE(finish_status.ok());
    EXPECT_EQ(1, fake_stub->write_object_stream_creations);
    EXPECT_EQ(1, fake_stub->write_object_finish_calls);
    EXPECT_EQ(1, profileEventValue(ProfileEvents::GCSWriteObject));
    EXPECT_EQ(1, profileEventValue(ProfileEvents::GCSWriteRequestAttempts));
    EXPECT_EQ(1, profileEventValue(ProfileEvents::GCSWriteRequestRetryableErrors));
    EXPECT_EQ(1, profileEventValue(ProfileEvents::GCSWriteRequestsErrors));
    EXPECT_EQ(0, profileEventValue(ProfileEvents::GCSWriteRequestsThrottling));
}
#endif
