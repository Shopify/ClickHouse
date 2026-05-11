#include <IO/GCS/GCSClient.h>

#include <Common/Exception.h>

#include <algorithm>
#include <limits>
#include <string>
#include <string_view>
#include <vector>
#if USE_GOOGLE_CLOUD
#    include <absl/strings/cord.h>
#    include <google/cloud/completion_queue.h>
#    include <google/cloud/credentials.h>
#    include <google/cloud/internal/unified_grpc_credentials.h>
#    include <google/cloud/internal/url_encode.h>
#    include <grpcpp/test/client_context_test_peer.h>
#endif

namespace DB::ErrorCodes
{
extern const int NOT_IMPLEMENTED;
}

namespace DB::GCS
{

CredentialMode credentialMode(const ClientSettings & settings)
{
    if (settings.use_insecure_credentials_for_tests)
        return CredentialMode::InsecureForTests;
    if (!settings.service_account_json.empty())
        return CredentialMode::ServiceAccountKey;
    return CredentialMode::GoogleDefault;
}

const char * credentialModeName(CredentialMode mode)
{
    switch (mode)
    {
        case CredentialMode::GoogleDefault:
            return "google_default";
        case CredentialMode::ServiceAccountKey:
            return "service_account_key";
        case CredentialMode::InsecureForTests:
            return "insecure_for_tests";
    }
}

bool isGrpcAvailable()
{
    return USE_GOOGLE_CLOUD;
}

void assertGrpcAvailable()
{
#if !USE_GOOGLE_CLOUD
    throw Exception(
        ErrorCodes::NOT_IMPLEMENTED,
        "Native GCS gRPC support is not available because ClickHouse was built without Google Cloud C++ gRPC support");
#endif
}

#if USE_GOOGLE_CLOUD
namespace
{


Status fromCloudStatus(const google::cloud::Status & status)
{
    if (status.ok())
        return {};

    switch (status.code())
    {
        case google::cloud::StatusCode::kNotFound:
            return makeStatus(StatusCode::NotFound, status.message());
        case google::cloud::StatusCode::kPermissionDenied:
        case google::cloud::StatusCode::kUnauthenticated:
            return makeStatus(StatusCode::PermissionDenied, status.message());
        case google::cloud::StatusCode::kDeadlineExceeded:
            return makeStatus(StatusCode::DeadlineExceeded, status.message());
        case google::cloud::StatusCode::kResourceExhausted:
            return makeStatus(StatusCode::ResourceExhausted, status.message());
        case google::cloud::StatusCode::kUnavailable:
            return makeStatus(StatusCode::Unavailable, status.message());
        case google::cloud::StatusCode::kInvalidArgument:
        case google::cloud::StatusCode::kFailedPrecondition:
        case google::cloud::StatusCode::kOutOfRange:
            return makeStatus(StatusCode::InvalidArgument, status.message());
        case google::cloud::StatusCode::kUnimplemented:
            return makeStatus(StatusCode::Unsupported, status.message());
        default:
            return makeStatus(StatusCode::Unknown, status.message());
    }
}

class GeneratedStub final : public IStub
{
public:
    explicit GeneratedStub(std::unique_ptr<google::storage::v2::Storage::StubInterface> stub_)
        : stub(std::move(stub_))
    {
    }

    grpc::Status getObject(
        grpc::ClientContext & context,
        const google::storage::v2::GetObjectRequest & request,
        google::storage::v2::Object & response) override
    {
        return stub->GetObject(&context, request, &response);
    }

    grpc::Status listObjects(
        grpc::ClientContext & context,
        const google::storage::v2::ListObjectsRequest & request,
        google::storage::v2::ListObjectsResponse & response) override
    {
        return stub->ListObjects(&context, request, &response);
    }

    grpc::Status deleteObject(
        grpc::ClientContext & context,
        const google::storage::v2::DeleteObjectRequest & request,
        google::protobuf::Empty & response) override
    {
        return stub->DeleteObject(&context, request, &response);
    }

    std::unique_ptr<grpc::ClientReaderInterface<google::storage::v2::ReadObjectResponse>>
    readObject(grpc::ClientContext & context, const google::storage::v2::ReadObjectRequest & request) override
    {
        return stub->ReadObject(&context, request);
    }

    std::unique_ptr<grpc::ClientWriterInterface<google::storage::v2::WriteObjectRequest>>
    writeObject(grpc::ClientContext & context, google::storage::v2::WriteObjectResponse & response) override
    {
        return stub->WriteObject(&context, &response);
    }

private:
    std::unique_ptr<google::storage::v2::Storage::StubInterface> stub;
};

std::shared_ptr<google::cloud::Credentials> makeCredentials(const ClientSettings & settings)
{
    switch (credentialMode(settings))
    {
        case CredentialMode::InsecureForTests:
            return google::cloud::MakeInsecureCredentials();
        case CredentialMode::ServiceAccountKey:
            return google::cloud::MakeServiceAccountCredentials(settings.service_account_json);
        case CredentialMode::GoogleDefault:
            return google::cloud::MakeGoogleDefaultCredentials();
    }
}

std::string bucketRoutingParameter(const std::string & bucket)
{
    return "bucket=" + google::cloud::internal::UrlEncode(bucket);
}

}

Client::Client(
    ClientSettings settings_, std::shared_ptr<IStub> stub_, std::shared_ptr<google::cloud::internal::GrpcAuthenticationStrategy> auth_)
    : settings(std::move(settings_))
    , stub(std::move(stub_))
    , auth(std::move(auth_))
{
}

std::unique_ptr<grpc::ClientContext> Client::makeContext(Status & status, const std::string & request_params) const
{
    auto context = std::make_unique<grpc::ClientContext>();
    context->set_deadline(std::chrono::system_clock::now() + std::chrono::milliseconds(settings.request_timeout_ms));

    if (!request_params.empty())
        context->AddMetadata("x-goog-request-params", request_params);

    if (!settings.user_project.empty())
        context->AddMetadata("x-goog-user-project", settings.user_project);

    status = {};
    if (auth && auth->RequiresConfigureContext())
        status = fromCloudStatus(auth->ConfigureContext(*context));

    return context;
}

Result<google::storage::v2::Object> Client::getObject(const google::storage::v2::GetObjectRequest & request) const
{
    Result<google::storage::v2::Object> result;
    auto context = makeContext(result.status, bucketRoutingParameter(request.bucket()));
    if (!result.ok())
        return result;

    result.status = fromGrpcStatus(stub->getObject(*context, request, result.response));
    return result;
}

Result<google::storage::v2::ListObjectsResponse> Client::listObjects(const google::storage::v2::ListObjectsRequest & request) const
{
    Result<google::storage::v2::ListObjectsResponse> result;
    auto context = makeContext(result.status, bucketRoutingParameter(request.parent()));
    if (!result.ok())
        return result;

    result.status = fromGrpcStatus(stub->listObjects(*context, request, result.response));
    return result;
}

Status Client::deleteObject(const google::storage::v2::DeleteObjectRequest & request) const
{
    Status status;
    auto context = makeContext(status, bucketRoutingParameter(request.bucket()));
    if (!status.ok())
        return status;

    google::protobuf::Empty response;
    return fromGrpcStatus(stub->deleteObject(*context, request, response));
}

StreamResult<grpc::ClientReaderInterface<google::storage::v2::ReadObjectResponse>>
Client::readObject(const google::storage::v2::ReadObjectRequest & request) const
{
    StreamResult<grpc::ClientReaderInterface<google::storage::v2::ReadObjectResponse>> result;
    result.context = makeContext(result.status, bucketRoutingParameter(request.bucket()));
    if (!result.status.ok())
        return result;

    result.stream = stub->readObject(*result.context, request);
    if (!result.stream)
        result.status = makeStatus(StatusCode::Unknown, "GCS gRPC ReadObject did not create a stream");
    return result;
}

StreamResult<grpc::ClientWriterInterface<google::storage::v2::WriteObjectRequest>>
Client::writeObject(google::storage::v2::WriteObjectResponse & response, const std::string & bucket) const
{
    StreamResult<grpc::ClientWriterInterface<google::storage::v2::WriteObjectRequest>> result;
    result.context = makeContext(result.status, bucketRoutingParameter(bucket));
    if (!result.status.ok())
        return result;

    result.stream = stub->writeObject(*result.context, response);
    if (!result.stream)
        result.status = makeStatus(StatusCode::Unknown, "GCS gRPC WriteObject did not create a stream");
    return result;
}

std::shared_ptr<Client> createClient(const ClientSettings & settings)
{
    assertGrpcAvailable();

    grpc::ChannelArguments channel_arguments;
    google::cloud::CompletionQueue completion_queue;
    auto auth = google::cloud::internal::CreateAuthenticationStrategy(*makeCredentials(settings), completion_queue);
    auto channel = auth->CreateChannel(settings.endpoint, channel_arguments);
    auto stub = std::make_shared<GeneratedStub>(google::storage::v2::Storage::NewStub(channel));
    return std::make_shared<Client>(settings, std::move(stub), std::move(auth));
}

FakeReadStream::FakeReadStream(std::vector<google::storage::v2::ReadObjectResponse> responses_, grpc::Status finish_status_)
    : responses(std::move(responses_))
    , finish_status(std::move(finish_status_))
{
}

bool FakeReadStream::NextMessageSize(uint32_t * size)
{
    if (next_response >= responses.size())
    {
        *size = 0;
        return false;
    }

    const auto response_size = responses[next_response].ByteSizeLong();
    *size = response_size > std::numeric_limits<uint32_t>::max() ? std::numeric_limits<uint32_t>::max()
                                                                 : static_cast<uint32_t>(response_size);
    return true;
}

bool FakeReadStream::Read(google::storage::v2::ReadObjectResponse * message)
{
    if (next_response >= responses.size())
        return false;

    *message = responses[next_response++];
    return true;
}

grpc::Status FakeReadStream::Finish()
{
    return finish_status;
}

FakeWriteStream::FakeWriteStream(
    google::storage::v2::WriteObjectResponse * response_out_,
    google::storage::v2::WriteObjectResponse response_,
    grpc::Status finish_status_,
    FinishCallback finish_callback_,
    bool write_returns_false_,
    bool writes_done_returns_false_,
    int * finish_calls_)
    : response_out(response_out_)
    , response(std::move(response_))
    , finish_status(std::move(finish_status_))
    , finish_callback(std::move(finish_callback_))
    , write_returns_false(write_returns_false_)
    , writes_done_returns_false(writes_done_returns_false_)
    , finish_calls(finish_calls_)
{
}

bool FakeWriteStream::Write(const google::storage::v2::WriteObjectRequest & message, grpc::WriteOptions)
{
    if (writes_done || write_returns_false)
        return false;

    writes.push_back(message);
    return true;
}

bool FakeWriteStream::WritesDone()
{
    writes_done = true;
    return !writes_done_returns_false;
}

grpc::Status FakeWriteStream::Finish()
{
    if (finish_calls)
        ++*finish_calls;

    if (!finish_status.ok())
        return finish_status;
    if (finish_callback)
    {
        auto callback_status = finish_callback(writes, response);
        if (!callback_status.ok())
            return callback_status;
    }

    if (response_out)
        *response_out = response;

    return finish_status;
}


namespace
{

std::string fakeObjectKey(const std::string & bucket, const std::string & object)
{
    return bucket + "\n" + object;
}

std::string cordToString(const absl::Cord & cord)
{
    std::string result;
    absl::CopyCordToString(cord, &result);
    return result;
}

}

grpc::Status FakeStub::getObject(
    grpc::ClientContext & context, const google::storage::v2::GetObjectRequest & request, google::storage::v2::Object & response)
{
    last_deadline = context.deadline();
    last_metadata = grpc::testing::ClientContextTestPeer(&context).GetSendInitialMetadata();
    get_object_requests.push_back(request);

    if (!get_object_status.ok())
        return get_object_status;

    if (use_object_map)
    {
        auto it = objects.find(fakeObjectKey(request.bucket(), request.object()));
        if (it == objects.end())
            return grpc::Status(grpc::StatusCode::NOT_FOUND, "fake object not found");
        response = it->second.metadata;
        return grpc::Status::OK;
    }

    response = get_object_response;
    return get_object_status;
}

grpc::Status FakeStub::listObjects(
    grpc::ClientContext & context,
    const google::storage::v2::ListObjectsRequest & request,
    google::storage::v2::ListObjectsResponse & response)
{
    last_deadline = context.deadline();
    last_metadata = grpc::testing::ClientContextTestPeer(&context).GetSendInitialMetadata();
    list_objects_requests.push_back(request);

    if (!list_objects_status.ok())
        return list_objects_status;

    if (use_object_map)
    {
        std::vector<const google::storage::v2::Object *> matched;
        for (const auto & [key, object] : objects)
        {
            (void)key;
            if (object.metadata.bucket() != request.parent())
                continue;
            if (!request.prefix().empty() && !object.metadata.name().starts_with(request.prefix()))
                continue;
            if (!request.lexicographic_start().empty() && object.metadata.name() < request.lexicographic_start())
                continue;
            matched.push_back(&object.metadata);
        }

        const size_t start = request.page_token().empty() ? 0 : std::stoull(request.page_token());
        const size_t limit = request.page_size() > 0 ? static_cast<size_t>(request.page_size()) : std::numeric_limits<size_t>::max();
        for (size_t i = start; i < matched.size() && static_cast<size_t>(response.objects_size()) < limit; ++i)
            *response.add_objects() = *matched[i];

        const size_t next = start + static_cast<size_t>(response.objects_size());
        if (next < matched.size())
            response.set_next_page_token(std::to_string(next));

        return grpc::Status::OK;
    }

    response = list_objects_response;
    return list_objects_status;
}

grpc::Status
FakeStub::deleteObject(grpc::ClientContext & context, const google::storage::v2::DeleteObjectRequest & request, google::protobuf::Empty &)
{
    last_deadline = context.deadline();
    last_metadata = grpc::testing::ClientContextTestPeer(&context).GetSendInitialMetadata();
    delete_object_requests.push_back(request);

    if (!delete_object_status.ok())
        return delete_object_status;

    if (use_object_map)
    {
        auto it = objects.find(fakeObjectKey(request.bucket(), request.object()));
        if (it == objects.end())
            return grpc::Status(grpc::StatusCode::NOT_FOUND, "fake object not found");
        objects.erase(it);
    }

    return grpc::Status::OK;
}

std::unique_ptr<grpc::ClientReaderInterface<google::storage::v2::ReadObjectResponse>>
FakeStub::readObject(grpc::ClientContext & context, const google::storage::v2::ReadObjectRequest & request)
{
    last_deadline = context.deadline();
    last_metadata = grpc::testing::ClientContextTestPeer(&context).GetSendInitialMetadata();
    read_object_requests.push_back(request);

    if (use_object_map)
    {
        auto it = objects.find(fakeObjectKey(request.bucket(), request.object()));
        if (it == objects.end())
            return std::make_unique<FakeReadStream>(
                std::vector<google::storage::v2::ReadObjectResponse>{}, grpc::Status(grpc::StatusCode::NOT_FOUND, "fake object not found"));

        const auto & data = it->second.data;
        size_t offset = request.read_offset() > 0 ? static_cast<size_t>(request.read_offset()) : 0;
        if (offset > data.size())
            offset = data.size();
        size_t size = data.size() - offset;
        if (request.read_limit() > 0)
            size = std::min(size, static_cast<size_t>(request.read_limit()));

        google::storage::v2::ReadObjectResponse response;
        *response.mutable_metadata() = it->second.metadata;
        response.mutable_checksummed_data()->set_content(std::string_view(data).substr(offset, size));
        return std::make_unique<FakeReadStream>(std::vector<google::storage::v2::ReadObjectResponse>{response}, grpc::Status::OK);
    }

    return std::make_unique<FakeReadStream>(read_object_responses, read_object_finish_status);
}

std::unique_ptr<grpc::ClientWriterInterface<google::storage::v2::WriteObjectRequest>>
FakeStub::writeObject(grpc::ClientContext & context, google::storage::v2::WriteObjectResponse & response)
{
    last_deadline = context.deadline();
    last_metadata = grpc::testing::ClientContextTestPeer(&context).GetSendInitialMetadata();
    response = write_object_response;

    auto finish_callback =
        [this](
            const std::vector<google::storage::v2::WriteObjectRequest> & writes, google::storage::v2::WriteObjectResponse & write_response)
    {
        write_object_requests.insert(write_object_requests.end(), writes.begin(), writes.end());

        if (!use_object_map)
            return grpc::Status::OK;

        if (writes.empty() || !writes.front().has_write_object_spec())
            return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT, "missing fake write object spec");

        const auto & resource = writes.front().write_object_spec().resource();
        std::string data;
        for (const auto & write : writes)
        {
            if (write.has_checksummed_data())
                data += cordToString(write.checksummed_data().content());
        }

        FakeObject object;
        object.data = std::move(data);
        object.metadata = resource;
        object.metadata.set_size(static_cast<int64_t>(object.data.size()));
        objects[fakeObjectKey(resource.bucket(), resource.name())] = object;
        *write_response.mutable_resource() = object.metadata;
        return grpc::Status::OK;
    };

    return std::make_unique<FakeWriteStream>(
        &response,
        write_object_response,
        write_object_finish_status,
        std::move(finish_callback),
        write_object_write_returns_false,
        write_object_writes_done_returns_false,
        &write_object_finish_calls);
}

#endif

}
