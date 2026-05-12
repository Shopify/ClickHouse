#include <Disks/DiskObjectStorage/ObjectStorages/GCS/GCSObjectStorage.h>

#include <Disks/DiskObjectStorage/ObjectStorages/ObjectStorageIterator.h>
#include <IO/ReadBufferFromMemory.h>
#include <IO/WriteBufferFromFileBase.h>
#include <IO/copyData.h>
#include <Interpreters/Context.h>
#include <Common/BlobStorageLogWriter.h>
#include <Common/CurrentThread.h>
#include <Common/Exception.h>
#include <Common/Macros.h>
#include <Common/ObjectStorageKeyGenerator.h>
#include <Common/ProfileEvents.h>
#include <Common/Stopwatch.h>
#include <Common/Throttler.h>

#include <fmt/format.h>

#include <cstring>

#if USE_GOOGLE_CLOUD
#    include <absl/strings/cord.h>
#endif

namespace ProfileEvents
{
extern const Event GCSGetRequestThrottlerCount;
extern const Event GCSGetRequestThrottlerSleepMicroseconds;
extern const Event GCSPutRequestThrottlerCount;
extern const Event GCSPutRequestThrottlerSleepMicroseconds;
extern const Event ReadBufferFromGCSMicroseconds;
extern const Event ReadBufferFromGCSInitMicroseconds;
extern const Event ReadBufferFromGCSBytes;
extern const Event ReadBufferFromGCSRequestsErrors;
extern const Event WriteBufferFromGCSMicroseconds;
extern const Event WriteBufferFromGCSBytes;
extern const Event WriteBufferFromGCSRequestsErrors;
}

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



String statusLogMessage(const GCS::Status & status)
{
    if (status.ok())
        return {};
    if (status.message.empty())
        return GCS::statusCodeName(status.code);
    return fmt::format("{}: {}", GCS::statusCodeName(status.code), status.message);
}

class GCSReadBuffer final : public ReadBufferFromFileBase
{
public:
    GCSReadBuffer(
        std::shared_ptr<GCS::Client> client_,
        String bucket_,
        String object_name_,
        size_t buf_size,
        std::optional<size_t> file_size_,
        std::optional<size_t> read_hint_,
        bool remote_fs_prefetch_,
        size_t prefetch_buffer_size_,
        ThrottlerPtr remote_throttler_,
        BlobStorageLogWriterPtr blob_storage_log_)
        : ReadBufferFromFileBase(buf_size, nullptr, 0, file_size_)
        , client(std::move(client_))
        , bucket(std::move(bucket_))
        , object_name(std::move(object_name_))
        , read_hint(read_hint_)
        , remote_fs_prefetch(remote_fs_prefetch_)
        , prefetch_buffer_size(prefetch_buffer_size_)
        , remote_throttler(std::move(remote_throttler_))
        , blob_storage_log(std::move(blob_storage_log_))
    {
    }

    ~GCSReadBuffer() override
    {
        try
        {
            finishSequentialStream(/* require_expected_bytes */ false);
        }
        catch (...)
        {
            tryLogCurrentException(__PRETTY_FUNCTION__);
        }
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
            throw Exception(
                ErrorCodes::BAD_ARGUMENTS, "Native GCS read buffer seek offset {} is past object size {}", new_position, *file_size);

        read_offset = static_cast<size_t>(new_position);
        resetSequentialStream();
        set(internal_buffer.begin(), internal_buffer.size());
        return new_position;
    }

    off_t getPosition() override { return static_cast<off_t>(read_offset - available()); }

    bool supportsReadAt() override { return true; }
    bool supportsRightBoundedReads() const override { return true; }

    size_t readBigAt(char * to, size_t n, size_t offset, const std::function<bool(size_t)> & progress_callback) const override
    {
        if (file_size && offset >= *file_size)
            return 0;

        size_t bytes_to_read = n;
        if (file_size)
            bytes_to_read = std::min(bytes_to_read, *file_size - offset);

        return fetchRangeInto(to, offset, bytes_to_read, progress_callback);
    }

    std::optional<size_t> getRemoteFileSize() const override { return file_size; }
    size_t getFileOffsetOfBufferEnd() const override { return read_offset; }

private:
    using ReadObjectStream = grpc::ClientReaderInterface<google::storage::v2::ReadObjectResponse>;

    struct SequentialStream
    {
        GCS::StreamResult<ReadObjectStream> result;
        std::optional<size_t> limit;
        size_t bytes_read_from_stream = 0;
    };

    size_t available() const { return static_cast<size_t>(working_buffer.end() - pos); }

    bool nextImpl() override
    {
        Stopwatch watch;
        Int32 error_code = 0;
        String error_message;
        size_t bytes_read = 0;

        try
        {
            CurrentThread::ReadThrottlingScope read_throttling_scope(remote_throttler);

            if (file_size && read_offset >= *file_size)
            {
                if (sequential_stream)
                    finishSequentialStream();
                return false;
            }
            bytes_read = copyPending(internal_buffer.begin(), internal_buffer.size());
            while (bytes_read < internal_buffer.size() && !sequential_eof)
            {
                ensureSequentialStream(read_offset + bytes_read);
                if (sequential_eof)
                    break;

                google::storage::v2::ReadObjectResponse response;
                if (!sequential_stream->result.stream->Read(&response))
                {
                    finishSequentialStream();
                    continue;
                }

                if (!response.has_checksummed_data())
                    continue;

                const auto & content = response.checksummed_data().content();
                if (content.empty())
                    continue;

                sequential_stream->bytes_read_from_stream += content.size();
                bytes_read += copyCordToBuffer(content, internal_buffer.begin() + bytes_read, internal_buffer.size() - bytes_read);
            }

            if (bytes_read == 0)
                return false;

            if (remote_throttler)
                remote_throttler->throttle(bytes_read);

            working_buffer = Buffer(internal_buffer.begin(), internal_buffer.begin() + bytes_read);
            pos = working_buffer.begin();
            read_offset += bytes_read;

            ProfileEvents::increment(ProfileEvents::ReadBufferFromGCSBytes, bytes_read);
            ProfileEvents::increment(ProfileEvents::ReadBufferFromGCSMicroseconds, watch.elapsedMicroseconds());
            addBlobLogEvent(bytes_read, watch.elapsedMicroseconds(), 0, {});
            return true;
        }
        catch (...)
        {
            if (!error_code)
            {
                error_code = getCurrentExceptionCode();
                error_message = getCurrentExceptionMessage(false);
                ProfileEvents::increment(ProfileEvents::ReadBufferFromGCSRequestsErrors);
                addBlobLogEvent(bytes_read, watch.elapsedMicroseconds(), error_code, error_message);
            }
            throw;
        }
    }

    std::optional<size_t> sequentialReadLimit(size_t offset) const
    {
        if (file_size)
            return std::min(*file_size - offset, sequentialWindowSizeForKnownFile());

        size_t limit = 0;
        if (read_hint)
            limit = std::max(limit, *read_hint);
        if (remote_fs_prefetch)
            limit = std::max(limit, prefetch_buffer_size);

        if (limit > internal_buffer.size())
            return limit;
        return {};
    }

    size_t sequentialWindowSizeForKnownFile() const
    {
        size_t limit = std::max(internal_buffer.size(), internal_buffer.size() * 16);
        limit = std::max(limit, prefetch_buffer_size);
        if (read_hint)
            limit = std::max(limit, *read_hint);
        return limit;
    }

    void ensureSequentialStream(size_t offset)
    {
        if (sequential_stream || sequential_eof)
            return;

        if (file_size && offset >= *file_size)
        {
            sequential_eof = true;
            return;
        }

        google::storage::v2::ReadObjectRequest request;
        request.set_bucket(bucketResourceName(bucket));
        request.set_object(object_name);
        request.set_read_offset(static_cast<int64_t>(offset));

        auto limit = sequentialReadLimit(offset);
        if (limit && *limit > 0)
            request.set_read_limit(static_cast<int64_t>(*limit));

        Stopwatch init_watch;
        sequential_stream.emplace();
        sequential_stream->limit = limit;
        sequential_stream->result = client->readObject(request);
        ProfileEvents::increment(ProfileEvents::ReadBufferFromGCSInitMicroseconds, init_watch.elapsedMicroseconds());

        if (!sequential_stream->result.ok())
        {
            auto status = sequential_stream->result.status;
            sequential_stream.reset();
            GCS::throwIfError(status, "ReadObject");
        }
    }

    void finishSequentialStream(bool require_expected_bytes = true)
    {
        if (!sequential_stream)
            return;

        const auto finish_status = GCS::fromGrpcStatus(sequential_stream->result.stream->Finish());
        const auto bytes_read_from_stream = sequential_stream->bytes_read_from_stream;
        const auto limit = sequential_stream->limit;
        sequential_stream.reset();

        if (!finish_status.ok())
            GCS::throwIfError(finish_status, "ReadObject");

        if (require_expected_bytes && file_size && limit && bytes_read_from_stream < *limit)
            throw Exception(
                ErrorCodes::S3_ERROR,
                "Native GCS ReadObject for '{}' ended after {} bytes before requested {} bytes",
                object_name,
                bytes_read_from_stream,
                *limit);

        if (file_size)
        {
            sequential_eof = read_offset >= *file_size;
            return;
        }
        if (!limit || bytes_read_from_stream < *limit)
            sequential_eof = true;
    }

    size_t copyPending(char * to, size_t limit)
    {
        if (pending_data.empty() || limit == 0)
            return 0;

        const size_t pending_size = pending_data.size() - pending_data_offset;
        const size_t bytes_to_copy = std::min(limit, pending_size);
        memcpy(to, pending_data.data() + pending_data_offset, bytes_to_copy);
        pending_data_offset += bytes_to_copy;

        if (pending_data_offset == pending_data.size())
        {
            pending_data.clear();
            pending_data_offset = 0;
        }

        return bytes_to_copy;
    }

    size_t copyCordToBuffer(const absl::Cord & cord, char * to, size_t limit)
    {
        size_t copied = 0;
        for (absl::string_view chunk : cord.Chunks())
        {
            if (copied == limit)
            {
                pending_data.append(chunk.data(), chunk.size());
                continue;
            }

            const size_t bytes_to_copy = std::min<size_t>(chunk.size(), limit - copied);
            memcpy(to + copied, chunk.data(), bytes_to_copy);
            copied += bytes_to_copy;

            if (bytes_to_copy < chunk.size())
                pending_data.append(chunk.data() + bytes_to_copy, chunk.size() - bytes_to_copy);
        }
        return copied;
    }

    void resetSequentialStream()
    {
        try
        {
            finishSequentialStream(/* require_expected_bytes */ false);
        }
        catch (...)
        {
            const auto error_code = getCurrentExceptionCode();
            const auto error_message = getCurrentExceptionMessage(false);
            ProfileEvents::increment(ProfileEvents::ReadBufferFromGCSRequestsErrors);
            addBlobLogEvent(0, 0, error_code, error_message);
            throw;
        }
        resetSequentialState();
    }

    void resetSequentialState()
    {
        pending_data.clear();
        pending_data_offset = 0;
        sequential_eof = false;
    }

    void addBlobLogEvent(size_t data_size, size_t elapsed_microseconds, Int32 code, const String & message) const
    {
        if (blob_storage_log)
            blob_storage_log->addEvent(
                BlobStorageLogElement::EventType::Read, bucket, object_name, {}, data_size, elapsed_microseconds, code, message);
    }

    size_t fetchRangeInto(
        char * to, size_t offset, size_t limit, const std::function<bool(size_t)> & progress_callback) const
    {
        if (limit == 0)
            return 0;

        Stopwatch watch;
        Int32 error_code = 0;
        String error_message;
        size_t bytes_read = 0;

        try
        {
            CurrentThread::ReadThrottlingScope read_throttling_scope(remote_throttler);

            google::storage::v2::ReadObjectRequest request;
            request.set_bucket(bucketResourceName(bucket));
            request.set_object(object_name);
            request.set_read_offset(static_cast<int64_t>(offset));
            request.set_read_limit(static_cast<int64_t>(limit));

            Stopwatch init_watch;
            auto stream_result = client->readObject(request);
            ProfileEvents::increment(ProfileEvents::ReadBufferFromGCSInitMicroseconds, init_watch.elapsedMicroseconds());
            if (!stream_result.ok())
            {
                error_code = GCS::errorCodeForStatus(stream_result.status.code);
                error_message = statusLogMessage(stream_result.status);
                ProfileEvents::increment(ProfileEvents::ReadBufferFromGCSRequestsErrors);
                addBlobLogEvent(bytes_read, watch.elapsedMicroseconds(), error_code, error_message);
                GCS::throwIfError(stream_result.status, "ReadObject");
            }

            bool cancelled = false;
            google::storage::v2::ReadObjectResponse response;
            while (bytes_read < limit && !cancelled && stream_result.stream->Read(&response))
            {
                if (!response.has_checksummed_data())
                    continue;

                for (absl::string_view chunk : response.checksummed_data().content().Chunks())
                {
                    size_t chunk_offset = 0;
                    while (chunk_offset < chunk.size() && bytes_read < limit)
                    {
                        const size_t bytes_to_copy = std::min<size_t>(
                            std::min(chunk.size() - chunk_offset, limit - bytes_read), internal_buffer.size());
                        memcpy(to + bytes_read, chunk.data() + chunk_offset, bytes_to_copy);
                        chunk_offset += bytes_to_copy;
                        bytes_read += bytes_to_copy;

                        if (bytes_to_copy && progress_callback && progress_callback(bytes_read))
                        {
                            cancelled = true;
                            break;
                        }
                    }
                    if (cancelled)
                        break;
                }
            }

            if (cancelled && stream_result.context)
                stream_result.context->TryCancel();

            auto finish_status = GCS::fromGrpcStatus(stream_result.stream->Finish());
            if (!finish_status.ok() && !cancelled)
            {
                error_code = GCS::errorCodeForStatus(finish_status.code);
                error_message = statusLogMessage(finish_status);
                ProfileEvents::increment(ProfileEvents::ReadBufferFromGCSRequestsErrors);
                addBlobLogEvent(bytes_read, watch.elapsedMicroseconds(), error_code, error_message);
                GCS::throwIfError(finish_status, "ReadObject");
            }

            if (remote_throttler && bytes_read)
                remote_throttler->throttle(bytes_read);

            ProfileEvents::increment(ProfileEvents::ReadBufferFromGCSBytes, bytes_read);
            ProfileEvents::increment(ProfileEvents::ReadBufferFromGCSMicroseconds, watch.elapsedMicroseconds());
            addBlobLogEvent(bytes_read, watch.elapsedMicroseconds(), 0, {});
            return bytes_read;
        }
        catch (...)
        {
            if (!error_code)
            {
                error_code = getCurrentExceptionCode();
                error_message = getCurrentExceptionMessage(false);
                ProfileEvents::increment(ProfileEvents::ReadBufferFromGCSRequestsErrors);
                addBlobLogEvent(bytes_read, watch.elapsedMicroseconds(), error_code, error_message);
            }
            throw;
        }
    }

    std::shared_ptr<GCS::Client> client;
    String bucket;
    String object_name;
    std::optional<size_t> read_hint;
    bool remote_fs_prefetch;
    size_t prefetch_buffer_size;
    ThrottlerPtr remote_throttler;
    BlobStorageLogWriterPtr blob_storage_log;
    std::optional<SequentialStream> sequential_stream;
    String pending_data;
    size_t pending_data_offset = 0;
    size_t read_offset = 0;
    bool sequential_eof = false;
};

class GCSWriteBuffer final : public WriteBufferFromFileBase
{
public:
    GCSWriteBuffer(
        std::shared_ptr<GCS::Client> client_,
        String bucket_,
        String object_name_,
        std::optional<ObjectAttributes> attributes_,
        size_t buf_size,
        ThrottlerPtr remote_throttler_,
        BlobStorageLogWriterPtr blob_storage_log_)
        : WriteBufferFromFileBase(buf_size, nullptr, 0)
        , client(std::move(client_))
        , bucket(std::move(bucket_))
        , object_name(std::move(object_name_))
        , attributes(std::move(attributes_))
        , remote_throttler(std::move(remote_throttler_))
        , blob_storage_log(std::move(blob_storage_log_))
    {
    }

    std::string getFileName() const override { return object_name; }
    void sync() override { next(); }

private:
    static constexpr size_t max_write_chunk_bytes = google::storage::v2::ServiceConstants::MAX_WRITE_CHUNK_BYTES;

    void nextImpl() override { sendChunks(working_buffer.begin(), offset(), /* finish */ false); }

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
        if (!stream_result->status.ok())
        {
            recordUploadFailure(GCS::errorCodeForStatus(stream_result->status.code), statusLogMessage(stream_result->status));
            GCS::throwIfError(stream_result->status, "starting WriteObject");
        }
    }

    void sendChunks(const char * source, size_t size, bool finish)
    {
        ensureStream();

        CurrentThread::WriteThrottlingScope write_throttling_scope(remote_throttler);

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

            if (chunk_size)
            {
                if (remote_throttler)
                    remote_throttler->throttle(chunk_size);
                ProfileEvents::increment(ProfileEvents::WriteBufferFromGCSBytes, chunk_size);
                accepted_payload_bytes += chunk_size;
            }

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
        auto finish_status = GCS::fromGrpcStatus(stream_result->stream->Finish());
        if (!finish_status.ok())
        {
            recordUploadFailure(GCS::errorCodeForStatus(finish_status.code), statusLogMessage(finish_status));
            GCS::throwIfError(finish_status, "WriteObject");
        }

        recordUploadElapsed();
        addUploadBlobLogEvent(0, {});
    }

    void recordUploadElapsed()
    {
        if (!upload_elapsed_recorded)
        {
            ProfileEvents::increment(ProfileEvents::WriteBufferFromGCSMicroseconds, upload_watch.elapsedMicroseconds());
            upload_elapsed_recorded = true;
        }
    }

    void addUploadBlobLogEvent(Int32 error_code, const String & error_message)
    {
        if (!blob_storage_log || upload_blob_log_written)
            return;

        blob_storage_log->addEvent(
            BlobStorageLogElement::EventType::Upload,
            bucket,
            object_name,
            {},
            accepted_payload_bytes,
            upload_watch.elapsedMicroseconds(),
            error_code,
            error_message);
        upload_blob_log_written = true;
    }

    void recordUploadFailure(Int32 error_code, const String & error_message)
    {
        ProfileEvents::increment(ProfileEvents::WriteBufferFromGCSRequestsErrors);
        recordUploadElapsed();
        addUploadBlobLogEvent(error_code, error_message);
    }

    [[noreturn]] void throwWriteFailure(std::string_view action)
    {
        stream_finished = true;
        auto status = GCS::fromGrpcStatus(stream_result->stream->Finish());
        if (!status.ok())
        {
            auto error_message = statusLogMessage(status);
            recordUploadFailure(GCS::errorCodeForStatus(status.code), error_message);
            throw Exception(
                GCS::errorCodeForStatus(status.code),
                "GCS gRPC WriteObject failed while {} object '{}' at offset {} with {}: {}",
                action,
                object_name,
                write_offset,
                GCS::statusCodeName(status.code),
                status.message);
        }

        recordUploadFailure(ErrorCodes::S3_ERROR, fmt::format("stream closed while {} object '{}'", action, object_name));
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
    ThrottlerPtr remote_throttler;
    BlobStorageLogWriterPtr blob_storage_log;
    Stopwatch upload_watch;
    std::optional<google::storage::v2::WriteObjectResponse> response;
    std::optional<GCS::StreamResult<grpc::ClientWriterInterface<google::storage::v2::WriteObjectRequest>>> stream_result;
    bool started = false;
    bool stream_finished = false;
    bool upload_elapsed_recorded = false;
    bool upload_blob_log_written = false;
    int64_t write_offset = 0;
    size_t accepted_payload_bytes = 0;
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

std::unique_ptr<ReadBufferFromFileBase>
GCSObjectStorage::readObject(const StoredObject & object, const ReadSettings & read_settings, std::optional<size_t> read_hint) const
{
#if USE_GOOGLE_CLOUD
    std::optional<size_t> file_size;
    if (object.bytes_size != std::numeric_limits<uint64_t>::max())
        file_size = static_cast<size_t>(object.bytes_size);

    BlobStorageLogWriterPtr blob_storage_log;
    if (read_settings.enable_blob_storage_log_for_read_operations)
    {
        blob_storage_log = createBlobStorageLogWriter();
        if (blob_storage_log)
            blob_storage_log->local_path = object.local_path;
    }

    return std::make_unique<GCSReadBuffer>(
        client,
        settings.bucket,
        objectName(object.remote_path),
        read_settings.remote_fs_buffer_size,
        file_size,
        read_hint,
        read_settings.remote_fs_prefetch,
        read_settings.prefetch_buffer_size,
        read_settings.remote_throttler,
        std::move(blob_storage_log));
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
    const WriteSettings & write_settings)
{
#if USE_GOOGLE_CLOUD
    if (mode != WriteMode::Rewrite)
        throw Exception(ErrorCodes::BAD_ARGUMENTS, "Native GCS object storage supports only WriteMode::Rewrite");
    if (settings.read_only)
        throw Exception(ErrorCodes::BAD_ARGUMENTS, "Native GCS object storage disk '{}' is read-only", settings.disk_name);

    auto blob_storage_log = createBlobStorageLogWriter();
    if (blob_storage_log)
        blob_storage_log->local_path = object.local_path;

    return std::make_unique<GCSWriteBuffer>(
        client,
        settings.bucket,
        objectName(object.remote_path),
        std::move(attributes),
        buf_size,
        write_settings.remote_throttler,
        std::move(blob_storage_log));
#else
    (void)object;
    (void)mode;
    (void)attributes;
    (void)buf_size;
    (void)write_settings;
    throwNotImplemented("writeObject");
#endif
}

void GCSObjectStorage::removeObjectIfExists(const StoredObject & object)
{
    auto blob_storage_log = createBlobStorageLogWriter();
    removeObjectIfExistsImpl(object, blob_storage_log);
}

void GCSObjectStorage::removeObjectIfExistsImpl(const StoredObject & object, const BlobStorageLogWriterPtr & blob_storage_log) const
{
#if USE_GOOGLE_CLOUD
    google::storage::v2::DeleteObjectRequest request;
    request.set_bucket(bucketResourceName(settings.bucket));
    request.set_object(objectName(object.remote_path));

    Stopwatch watch;
    auto status = client->deleteObject(request);
    if (blob_storage_log)
        blob_storage_log->addEvent(
            BlobStorageLogElement::EventType::Delete,
            settings.bucket,
            object.remote_path,
            object.local_path,
            object.bytes_size,
            watch.elapsedMicroseconds(),
            status.ok() ? 0 : GCS::errorCodeForStatus(status.code),
            statusLogMessage(status));

    if (status.code == GCS::StatusCode::NotFound)
        return;
    GCS::throwIfError(status, "DeleteObject");
#else
    (void)object;
    (void)blob_storage_log;
    throwNotImplemented("removeObjectIfExists");
#endif
}

void GCSObjectStorage::removeObjectsIfExist(const StoredObjects & objects)
{
    auto blob_storage_log = createBlobStorageLogWriter();
    for (const auto & object : objects)
        removeObjectIfExistsImpl(object, blob_storage_log);
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
    auto out = object_storage_to.writeObject(
        object_to, WriteMode::Rewrite, std::move(object_to_attributes), DBMS_DEFAULT_BUFFER_SIZE, write_settings);
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
            children.emplace_back(
                std::make_shared<RelativePathWithMetadata>(object.name(), metadataFromProto(object, /* with_tags */ false)));
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
    const std::string & path_prefix, size_t max_keys, bool with_tags, const std::optional<std::string> & start_after) const
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

            files.emplace_back(std::make_shared<RelativePathWithMetadata>(object.name(), metadataFromProto(object, with_tags)));
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

BlobStorageLogWriterPtr GCSObjectStorage::createBlobStorageLogWriter() const
{
    if (settings.blob_storage_log_writer_factory)
        return settings.blob_storage_log_writer_factory(settings.disk_name);
    return BlobStorageLogWriter::create(settings.disk_name);
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

    settings.client_settings.request_timeout_ms
        = config.getUInt64(config_prefix + ".request_timeout_ms", settings.client_settings.request_timeout_ms);
    settings.client_settings.max_retry_attempts
        = config.getUInt64(config_prefix + ".max_retry_attempts", settings.client_settings.max_retry_attempts);
    settings.client_settings.service_account_json = config.getString(config_prefix + ".service_account_json", "");
    settings.client_settings.user_project = config.getString(config_prefix + ".user_project", "");
    settings.client_settings.use_insecure_credentials_for_tests
        = config.getBool(config_prefix + ".use_insecure_credentials_for_tests", false);
    settings.client_settings.for_disk = true;

    const auto get_request_throttler_max_speed = config.getUInt64(config_prefix + ".get_request_throttler_max_speed", 0);
    if (get_request_throttler_max_speed)
        settings.client_settings.request_throttler.get_throttler = std::make_shared<Throttler>(
            get_request_throttler_max_speed,
            ProfileEvents::GCSGetRequestThrottlerCount,
            ProfileEvents::GCSGetRequestThrottlerSleepMicroseconds);

    const auto put_request_throttler_max_speed = config.getUInt64(config_prefix + ".put_request_throttler_max_speed", 0);
    if (put_request_throttler_max_speed)
        settings.client_settings.request_throttler.put_throttler = std::make_shared<Throttler>(
            put_request_throttler_max_speed,
            ProfileEvents::GCSPutRequestThrottlerCount,
            ProfileEvents::GCSPutRequestThrottlerSleepMicroseconds);

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
