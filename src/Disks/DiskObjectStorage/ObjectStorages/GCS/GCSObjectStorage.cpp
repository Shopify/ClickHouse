#include <Disks/DiskObjectStorage/ObjectStorages/GCS/GCSObjectStorage.h>

#include <Common/Exception.h>
#include <Common/Macros.h>
#include <Common/ObjectStorageKeyGenerator.h>
#include <Disks/DiskObjectStorage/ObjectStorages/ObjectStorageIterator.h>
#include <IO/ReadBufferFromMemory.h>
#include <IO/WriteBufferFromFileBase.h>
#include <IO/copyData.h>
#include <Interpreters/Context.h>

#include <fmt/format.h>

#include <cstring>

#if USE_GOOGLE_CLOUD
#    include <absl/strings/cord.h>
#endif

namespace DB
{

namespace ErrorCodes
{
    extern const int BAD_ARGUMENTS;
    extern const int FILE_DOESNT_EXIST;
    extern const int NOT_IMPLEMENTED;
    extern const int S3_ERROR;
}

static String expandConfigString(const Poco::Util::AbstractConfiguration & config, const String & key, const ContextPtr & context)
{
    return context->getMacros()->expand(config.getString(key));
}

static String normalizeKeyPrefix(String key_prefix)
{
    if (key_prefix.empty())
        return key_prefix;

    if (key_prefix.starts_with('/'))
        throw Exception(ErrorCodes::BAD_ARGUMENTS, "Native GCS key prefix must be relative, got '{}'", key_prefix);

    if (!key_prefix.ends_with('/'))
        key_prefix.push_back('/');

    return key_prefix;
}


#if USE_GOOGLE_CLOUD
namespace
{

String bucketResourceName(const String & bucket)
{
    if (bucket.starts_with("projects/"))
        return bucket;
    return fmt::format("projects/_/buckets/{}", bucket);
}

const String & objectName(const String & path)
{
    if (path.starts_with('/'))
        throw Exception(ErrorCodes::BAD_ARGUMENTS, "Native GCS object name must be relative, got '{}'", path);
    return path;
}

Poco::Timestamp timestampFromProto(const google::protobuf::Timestamp & timestamp)
{
    return Poco::Timestamp(
        static_cast<Poco::Timestamp::TimeVal>(timestamp.seconds()) * 1000000
        + static_cast<Poco::Timestamp::TimeVal>(timestamp.nanos() / 1000));
}

ObjectMetadata metadataFromProto(const google::storage::v2::Object & object, bool with_tags)
{
    ObjectMetadata metadata;
    metadata.size_bytes = object.size() >= 0 ? static_cast<uint64_t>(object.size()) : 0;
    metadata.is_size_known = true;
    if (object.has_update_time())
        metadata.last_modified = timestampFromProto(object.update_time());
    metadata.etag = object.etag();
    for (const auto & [key, value] : object.metadata())
        metadata.attributes.emplace(key, value);
    if (with_tags)
        metadata.tags = {};
    return metadata;
}

std::string cordToString(const absl::Cord & cord)
{
    std::string result;
    absl::CopyCordToString(cord, &result);
    return result;
}

class GCSReadBuffer final : public ReadBufferFromFileBase
{
public:
    GCSReadBuffer(
        std::shared_ptr<GCS::Client> client_,
        String bucket_,
        String object_name_,
        size_t buf_size,
        std::optional<size_t> file_size_)
        : ReadBufferFromFileBase(buf_size, nullptr, 0, file_size_)
        , client(std::move(client_))
        , bucket(std::move(bucket_))
        , object_name(std::move(object_name_))
    {
    }

    String getFileName() const override { return object_name; }

    off_t seek(off_t off, int whence) override
    {
        off_t new_position = 0;
        if (whence == SEEK_SET)
            new_position = off;
        else if (whence == SEEK_CUR)
            new_position = getPosition() + off;
        else
            throw Exception(ErrorCodes::BAD_ARGUMENTS, "Native GCS read buffer supports only SEEK_SET and SEEK_CUR");

        if (new_position < 0)
            throw Exception(ErrorCodes::BAD_ARGUMENTS, "Native GCS read buffer cannot seek to negative offset {}", new_position);
        if (file_size && static_cast<size_t>(new_position) > *file_size)
            throw Exception(ErrorCodes::BAD_ARGUMENTS, "Native GCS read buffer seek offset {} is past object size {}", new_position, *file_size);

        read_offset = static_cast<size_t>(new_position);
        set(internal_buffer.begin(), internal_buffer.size());
        return new_position;
    }

    off_t getPosition() override
    {
        return static_cast<off_t>(read_offset - available());
    }

    bool supportsReadAt() override { return true; }
    bool supportsRightBoundedReads() const override { return true; }

    size_t readBigAt(char * to, size_t n, size_t offset, const std::function<bool(size_t)> & progress_callback) const override
    {
        auto data = fetchRange(offset, n);
        memcpy(to, data.data(), data.size());
        if (progress_callback)
            progress_callback(data.size());
        return data.size();
    }

    std::optional<size_t> getRemoteFileSize() const override { return file_size; }
    size_t getFileOffsetOfBufferEnd() const override { return read_offset; }

private:
    size_t available() const
    {
        return static_cast<size_t>(working_buffer.end() - pos);
    }

    bool nextImpl() override
    {
        if (file_size && read_offset >= *file_size)
            return false;

        size_t bytes_to_read = internal_buffer.size();
        if (file_size)
            bytes_to_read = std::min(bytes_to_read, *file_size - read_offset);
        if (bytes_to_read == 0)
            return false;

        auto data = fetchRange(read_offset, bytes_to_read);
        if (data.empty())
            return false;

        memcpy(internal_buffer.begin(), data.data(), data.size());
        working_buffer = Buffer(internal_buffer.begin(), internal_buffer.begin() + data.size());
        pos = working_buffer.begin();
        read_offset += data.size();
        return true;
    }

    String fetchRange(size_t offset, size_t limit) const
    {
        if (limit == 0)
            return {};

        google::storage::v2::ReadObjectRequest request;
        request.set_bucket(bucketResourceName(bucket));
        request.set_object(object_name);
        request.set_read_offset(static_cast<int64_t>(offset));
        request.set_read_limit(static_cast<int64_t>(limit));

        auto stream_result = client->readObject(request);
        GCS::throwIfError(stream_result.status, "ReadObject");

        String data;
        google::storage::v2::ReadObjectResponse response;
        while (stream_result.stream->Read(&response))
        {
            if (response.has_checksummed_data())
                data += cordToString(response.checksummed_data().content());
        }
        GCS::throwIfError(GCS::fromGrpcStatus(stream_result.stream->Finish()), "ReadObject");
        return data;
    }

    std::shared_ptr<GCS::Client> client;
    String bucket;
    String object_name;
    size_t read_offset = 0;
};

class GCSWriteBuffer final : public WriteBufferFromFileBase
{
public:
    GCSWriteBuffer(
        std::shared_ptr<GCS::Client> client_,
        String bucket_,
        String object_name_,
        std::optional<ObjectAttributes> attributes_,
        size_t buf_size)
        : WriteBufferFromFileBase(buf_size, nullptr, 0)
        , client(std::move(client_))
        , bucket(std::move(bucket_))
        , object_name(std::move(object_name_))
        , attributes(std::move(attributes_))
    {
    }

    std::string getFileName() const override { return object_name; }
    void sync() override { next(); }

private:
    static constexpr size_t max_write_chunk_bytes = google::storage::v2::ServiceConstants::MAX_WRITE_CHUNK_BYTES;

    void nextImpl() override
    {
        sendChunks(working_buffer.begin(), offset(), /* finish */ false);
    }

    void finalizeImpl() override
    {
        sendChunks(working_buffer.begin(), offset(), /* finish */ true);
        finishStream();
    }

    void ensureStream()
    {
        if (stream_result)
            return;

        response.emplace();
        stream_result.emplace(client->writeObject(*response, bucketResourceName(bucket)));
        GCS::throwIfError(stream_result->status, "starting WriteObject");
    }

    void sendChunks(const char * source, size_t size, bool finish)
    {
        ensureStream();

        size_t sent = 0;
        while (sent < size || (finish && sent == 0))
        {
            const size_t chunk_size = sent < size ? std::min(max_write_chunk_bytes, size - sent) : 0;
            const bool last_chunk = finish && sent + chunk_size >= size;

            google::storage::v2::WriteObjectRequest request;
            if (!started)
            {
                auto & resource = *request.mutable_write_object_spec()->mutable_resource();
                resource.set_bucket(bucketResourceName(bucket));
                resource.set_name(object_name);
                if (attributes)
                {
                    for (const auto & [key, value] : *attributes)
                        (*resource.mutable_metadata())[key] = value;
                }
                started = true;
            }

            request.set_write_offset(write_offset);
            request.set_finish_write(last_chunk);
            if (chunk_size)
                request.mutable_checksummed_data()->set_content(std::string_view(source + sent, chunk_size));

            if (!stream_result->stream->Write(request, grpc::WriteOptions{}))
                throwWriteFailure("sending");

            write_offset += chunk_size;
            sent += chunk_size;

            if (chunk_size == 0)
                break;
        }
    }

    void finishStream()
    {
        if (stream_finished)
            return;

        if (!stream_result)
            sendChunks(nullptr, 0, /* finish */ true);

        if (!stream_result->stream->WritesDone())
            throwWriteFailure("finishing writes for");

        stream_finished = true;
        GCS::throwIfError(GCS::fromGrpcStatus(stream_result->stream->Finish()), "WriteObject");
    }

    [[noreturn]] void throwWriteFailure(std::string_view action)
    {
        stream_finished = true;
        auto status = GCS::fromGrpcStatus(stream_result->stream->Finish());
        if (!status.ok())
        {
            throw Exception(
                GCS::errorCodeForStatus(status.code),
                "GCS gRPC WriteObject failed while {} object '{}' at offset {} with {}: {}",
                action,
                object_name,
                write_offset,
                GCS::statusCodeName(status.code),
                status.message);
        }

        throw Exception(
            ErrorCodes::S3_ERROR,
            "GCS gRPC WriteObject stream closed while {} object '{}' at offset {} without a final gRPC error status",
            action,
            object_name,
            write_offset);
    }

    std::shared_ptr<GCS::Client> client;
    String bucket;
    String object_name;
    std::optional<ObjectAttributes> attributes;
    std::optional<google::storage::v2::WriteObjectResponse> response;
    std::optional<GCS::StreamResult<grpc::ClientWriterInterface<google::storage::v2::WriteObjectRequest>>> stream_result;
    bool started = false;
    bool stream_finished = false;
    int64_t write_offset = 0;
};

}
#endif


#if USE_GOOGLE_CLOUD
GCSObjectStorage::GCSObjectStorage(GCSObjectStorageSettings settings_, std::shared_ptr<GCS::Client> client_)
    : settings(std::move(settings_))
    , client(std::move(client_))
{
}
#else
GCSObjectStorage::GCSObjectStorage(GCSObjectStorageSettings settings_)
    : settings(std::move(settings_))
{
}
#endif

bool GCSObjectStorage::exists(const StoredObject & object) const
{
#if USE_GOOGLE_CLOUD
    return tryGetObjectMetadata(object.remote_path, /* with_tags */ false).has_value();
#else
    (void)object;
    throwNotImplemented("exists");
#endif
}

std::unique_ptr<ReadBufferFromFileBase> GCSObjectStorage::readObject(
    const StoredObject & object,
    const ReadSettings & read_settings,
    std::optional<size_t>) const
{
#if USE_GOOGLE_CLOUD
    std::optional<size_t> file_size;
    if (object.bytes_size != std::numeric_limits<uint64_t>::max())
        file_size = static_cast<size_t>(object.bytes_size);

    return std::make_unique<GCSReadBuffer>(
        client,
        settings.bucket,
        objectName(object.remote_path),
        read_settings.remote_fs_buffer_size,
        file_size);
#else
    (void)object;
    (void)read_settings;
    throwNotImplemented("readObject");
#endif
}

std::unique_ptr<WriteBufferFromFileBase> GCSObjectStorage::writeObject(
    const StoredObject & object,
    WriteMode mode,
    std::optional<ObjectAttributes> attributes,
    size_t buf_size,
    const WriteSettings &)
{
#if USE_GOOGLE_CLOUD
    if (mode != WriteMode::Rewrite)
        throw Exception(ErrorCodes::BAD_ARGUMENTS, "Native GCS object storage supports only WriteMode::Rewrite");
    if (settings.read_only)
        throw Exception(ErrorCodes::BAD_ARGUMENTS, "Native GCS object storage disk '{}' is read-only", settings.disk_name);

    return std::make_unique<GCSWriteBuffer>(client, settings.bucket, objectName(object.remote_path), std::move(attributes), buf_size);
#else
    (void)object;
    (void)mode;
    (void)attributes;
    (void)buf_size;
    throwNotImplemented("writeObject");
#endif
}

void GCSObjectStorage::removeObjectIfExists(const StoredObject & object)
{
#if USE_GOOGLE_CLOUD
    google::storage::v2::DeleteObjectRequest request;
    request.set_bucket(bucketResourceName(settings.bucket));
    request.set_object(objectName(object.remote_path));

    auto status = client->deleteObject(request);
    if (status.code == GCS::StatusCode::NotFound)
        return;
    GCS::throwIfError(status, "DeleteObject");
#else
    (void)object;
    throwNotImplemented("removeObjectIfExists");
#endif
}

void GCSObjectStorage::removeObjectsIfExist(const StoredObjects & objects)
{
    for (const auto & object : objects)
        removeObjectIfExists(object);
}

void GCSObjectStorage::copyObject(
    const StoredObject & object_from,
    const StoredObject & object_to,
    const ReadSettings & read_settings,
    const WriteSettings & write_settings,
    std::optional<ObjectAttributes> object_to_attributes)
{
    auto in = readObject(object_from, read_settings);
    auto out = writeObject(object_to, WriteMode::Rewrite, std::move(object_to_attributes), DBMS_DEFAULT_BUFFER_SIZE, write_settings);
    copyData(*in, *out);
    out->finalize();
}

void GCSObjectStorage::copyObjectToAnotherObjectStorage(
    const StoredObject & object_from,
    const StoredObject & object_to,
    const ReadSettings & read_settings,
    const WriteSettings & write_settings,
    IObjectStorage & object_storage_to,
    std::optional<ObjectAttributes> object_to_attributes)
{
    if (&object_storage_to == this)
    {
        copyObject(object_from, object_to, read_settings, write_settings, std::move(object_to_attributes));
        return;
    }

    auto in = readObject(object_from, read_settings);
    auto out = object_storage_to.writeObject(object_to, WriteMode::Rewrite, std::move(object_to_attributes), DBMS_DEFAULT_BUFFER_SIZE, write_settings);
    copyData(*in, *out);
    out->finalize();
}

ObjectMetadata GCSObjectStorage::getObjectMetadata(const std::string & path, bool with_tags) const
{
    auto metadata = tryGetObjectMetadata(path, with_tags);
    if (!metadata)
        throw Exception(ErrorCodes::FILE_DOESNT_EXIST, "Native GCS object '{}' does not exist", path);
    return *metadata;
}

std::optional<ObjectMetadata> GCSObjectStorage::tryGetObjectMetadata(const std::string & path, bool with_tags) const
{
#if USE_GOOGLE_CLOUD
    google::storage::v2::GetObjectRequest request;
    request.set_bucket(bucketResourceName(settings.bucket));
    request.set_object(objectName(path));

    auto result = client->getObject(request);
    if (result.status.code == GCS::StatusCode::NotFound)
        return std::nullopt;
    GCS::throwIfError(result.status, "GetObject");
    return metadataFromProto(result.response, with_tags);
#else
    (void)path;
    (void)with_tags;
    throwNotImplemented("tryGetObjectMetadata");
#endif
}

void GCSObjectStorage::listObjects(const std::string & path, RelativePathsWithMetadata & children, size_t max_keys) const
{
#if USE_GOOGLE_CLOUD
    const size_t target_keys = max_keys;
    google::storage::v2::ListObjectsRequest request;
    request.set_parent(bucketResourceName(settings.bucket));
    request.set_prefix(objectName(path));
    request.set_page_size(max_keys ? static_cast<int32_t>(max_keys) : 1000);

    do
    {
        auto result = client->listObjects(request);
        GCS::throwIfError(result.status, "ListObjects");

        for (const auto & object : result.response.objects())
        {
            children.emplace_back(std::make_shared<RelativePathWithMetadata>(
                object.name(),
                metadataFromProto(object, /* with_tags */ false)));
            if (target_keys && children.size() >= target_keys)
                return;
        }

        request.set_page_token(result.response.next_page_token());
    } while (!request.page_token().empty());
#else
    (void)path;
    (void)children;
    (void)max_keys;
    throwNotImplemented("listObjects");
#endif
}

ObjectStorageIteratorPtr GCSObjectStorage::iterate(
    const std::string & path_prefix,
    size_t max_keys,
    bool with_tags,
    const std::optional<std::string> & start_after) const
{
#if USE_GOOGLE_CLOUD
    RelativePathsWithMetadata files;
    google::storage::v2::ListObjectsRequest request;
    request.set_parent(bucketResourceName(settings.bucket));
    request.set_prefix(objectName(path_prefix));
    if (max_keys)
        request.set_page_size(static_cast<int32_t>(max_keys));
    else
        request.set_page_size(1000);
    if (start_after && !start_after->empty())
        request.set_lexicographic_start(*start_after);

    do
    {
        auto result = client->listObjects(request);
        GCS::throwIfError(result.status, "ListObjects");

        for (const auto & object : result.response.objects())
        {
            if (start_after && !start_after->empty() && object.name() <= *start_after)
                continue;

            files.emplace_back(std::make_shared<RelativePathWithMetadata>(
                object.name(),
                metadataFromProto(object, with_tags)));
            if (max_keys && files.size() >= max_keys)
                return std::make_shared<ObjectStorageIteratorFromList>(std::move(files));
        }

        request.set_page_token(result.response.next_page_token());
    } while (!request.page_token().empty());

    return std::make_shared<ObjectStorageIteratorFromList>(std::move(files));
#else
    (void)path_prefix;
    (void)max_keys;
    (void)with_tags;
    (void)start_after;
    throwNotImplemented("iterate");
#endif
}

ReadSettings GCSObjectStorage::patchSettings(const ReadSettings & read_settings) const
{
    return read_settings;
}

WriteSettings GCSObjectStorage::patchSettings(const WriteSettings & write_settings) const
{
    return write_settings;
}

ObjectStorageKeyGeneratorPtr GCSObjectStorage::createKeyGenerator() const
{
    return createObjectStorageKeyGeneratorByPrefix(settings.key_prefix);
}

void GCSObjectStorage::throwNotImplemented(std::string_view operation) const
{
    throw Exception(
        ErrorCodes::NOT_IMPLEMENTED,
        "Native GCS object storage operation '{}' is not implemented before the core read/write disk phase",
        operation);
}

GCSObjectStorageSettings getGCSObjectStorageSettings(
    const std::string & name,
    const Poco::Util::AbstractConfiguration & config,
    const std::string & config_prefix,
    const ContextPtr & context)
{
    if (!config.has(config_prefix + ".bucket"))
        throw Exception(ErrorCodes::BAD_ARGUMENTS, "Native GCS object storage requires `bucket` in config");

    GCSObjectStorageSettings settings;
    settings.disk_name = name;
    settings.bucket = expandConfigString(config, config_prefix + ".bucket", context);
    if (settings.bucket.empty())
        throw Exception(ErrorCodes::BAD_ARGUMENTS, "Native GCS object storage bucket cannot be empty");

    if (config.has(config_prefix + ".key_prefix"))
        settings.key_prefix = normalizeKeyPrefix(expandConfigString(config, config_prefix + ".key_prefix", context));

    if (config.has(config_prefix + ".endpoint"))
        settings.client_settings.endpoint = expandConfigString(config, config_prefix + ".endpoint", context);
    if (settings.client_settings.endpoint.empty())
        throw Exception(ErrorCodes::BAD_ARGUMENTS, "Native GCS object storage endpoint cannot be empty");

    settings.client_settings.request_timeout_ms = config.getUInt64(
        config_prefix + ".request_timeout_ms", settings.client_settings.request_timeout_ms);
    settings.client_settings.service_account_json = config.getString(config_prefix + ".service_account_json", "");
    settings.client_settings.user_project = config.getString(config_prefix + ".user_project", "");
    settings.client_settings.use_insecure_credentials_for_tests = config.getBool(
        config_prefix + ".use_insecure_credentials_for_tests", false);
    settings.read_only = config.getBool(config_prefix + ".read_only", config.getBool(config_prefix + ".readonly", false));
    settings.description = fmt::format("{}/{}", settings.client_settings.endpoint, settings.bucket);

    return settings;
}

ObjectStoragePtr createGCSObjectStorage(
    const std::string & name,
    const Poco::Util::AbstractConfiguration & config,
    const std::string & config_prefix,
    const ContextPtr & context,
    bool)
{
    auto settings = getGCSObjectStorageSettings(name, config, config_prefix, context);
    GCS::assertGrpcAvailable();

#if USE_GOOGLE_CLOUD
    auto client = GCS::createClient(settings.client_settings);
    return std::make_shared<GCSObjectStorage>(std::move(settings), std::move(client));
#else
    throw Exception(
        ErrorCodes::NOT_IMPLEMENTED,
        "Native GCS gRPC support is not available because ClickHouse was built without Google Cloud C++ gRPC support");
#endif
}
}
