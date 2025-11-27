#include <Disks/DiskObjectStorage/ObjectStorages/GCS/GCSObjectStorage.h>
#include <Disks/DiskObjectStorage/ObjectStorages/ObjectStorageIteratorAsync.h>
#include <Common/setThreadName.h>
#include <Common/Exception.h>

namespace CurrentMetrics
{
    extern const Metric ObjectStorageGCSThreads;
    extern const Metric ObjectStorageGCSThreadsActive;
    extern const Metric ObjectStorageGCSThreadsScheduled;
}

namespace DB
{

namespace ErrorCodes
{
    extern const int NOT_IMPLEMENTED;
    extern const int GCS_ERROR;
}

namespace
{

class GCSIteratorAsync final : public IObjectStorageIteratorAsync
{
public:
    GCSIteratorAsync(
        const std::string & path_prefix,
        std::shared_ptr<const google::cloud::storage::Client> client_,
        const String & bucket_,
        size_t max_list_size)
        : IObjectStorageIteratorAsync(
            CurrentMetrics::ObjectStorageGCSThreads,
            CurrentMetrics::ObjectStorageGCSThreadsActive,
            CurrentMetrics::ObjectStorageGCSThreadsScheduled,
            ThreadName::GCS_LIST_POOL)
        , client(client_)
        , bucket(bucket_)
    {
        namespace gcs = google::cloud::storage;
        options = gcs::Prefix(path_prefix);
        max_results = static_cast<int>(max_list_size);
    }

    ~GCSIteratorAsync() override
    {
        if (!deactivated)
            deactivate();
    }

private:
    bool getBatchAndCheckNext(RelativePathsWithMetadata & batch) override
    {
        // TODO: Add ProfileEvents tracking
        // ProfileEvents::increment(ProfileEvents::GCSListObjects);

        chassert(batch.empty());

        namespace gcs = google::cloud::storage;

        // Create mutable client pointer for API calls
        auto client_ptr = std::const_pointer_cast<google::cloud::storage::Client>(client);

        bool has_more = false;
        size_t count = 0;

        // List objects with continuation token if available
        for (auto&& object_metadata : client_ptr->ListObjects(bucket, options, gcs::MaxResults(max_results)))
        {
            if (!object_metadata.ok())
                throw Exception(ErrorCodes::GCS_ERROR, "Failed to list objects: {}", object_metadata.status().message());

            batch.emplace_back(std::make_shared<RelativePathWithMetadata>(
                object_metadata->name(),
                ObjectMetadata{
                    object_metadata->size(),
                    Poco::Timestamp::fromEpochTime(
                        std::chrono::duration_cast<std::chrono::seconds>(
                            object_metadata->time_created().time_since_epoch()).count()),
                    object_metadata->etag(),
                    {},
                    {}}));

            ++count;

            // Check if we have more results after this batch
            if (count >= static_cast<size_t>(max_results))
            {
                // GCS will continue iteration if there are more objects
                // We need to track if there's a next page
                has_more = true;
                break;
            }
        }

        return has_more;
    }

    std::shared_ptr<const google::cloud::storage::Client> client;
    String bucket;
    google::cloud::storage::Prefix options;
    int max_results;
};

}



GCSObjectStorage::GCSObjectStorage(
    ClientPtr && client_,
    SettingsPtr && settings_,
    const String & object_namespace_,
    const String & description_)
    : object_namespace(object_namespace_)
    , client(std::move(client_))
    , settings(std::move(settings_))
    , description(description_)
{
}


ObjectStorageKeyGeneratorPtr GCSObjectStorage::createKeyGenerator() const
{
    return createObjectStorageKeyGeneratorByTemplate("[a-z]{32}");
}

bool GCSObjectStorage::exists(const StoredObject & object) const
{
    auto client_ptr = getClient();
    auto metadata = client_ptr->GetObjectMetadata(object_namespace, object.remote_path);

    return metadata.ok();
}

void GCSObjectStorage::listObjects(const std::string & path, RelativePathsWithMetadata & children, size_t max_keys) const
{
    auto client_ptr = getClient();

    // Set up list options
    namespace gcs = google::cloud::storage;

    // MaxResults limits total objects across all pages. Use max_keys if specified, otherwise use default.
    size_t max_results = max_keys ? max_keys : settings.get()->list_object_keys_size;

    // TODO: Add ProfileEvents tracking
    // ProfileEvents::increment(ProfileEvents::GCSListObjects);

    // List objects with prefix - iterator returns objects one-by-one, handling pagination internally
    for (auto&& object_metadata : client_ptr->ListObjects(object_namespace, gcs::Prefix(path), gcs::MaxResults(static_cast<int>(max_results))))
    {
        if (!object_metadata.ok())
            throw Exception(ErrorCodes::GCS_ERROR, "Failed to list objects: {}", object_metadata.status().message());

        children.emplace_back(std::make_shared<RelativePathWithMetadata>(
            object_metadata->name(),
            ObjectMetadata{
                object_metadata->size(),
                Poco::Timestamp::fromEpochTime(
                    std::chrono::duration_cast<std::chrono::seconds>(
                        object_metadata->time_created().time_since_epoch()).count()),
                object_metadata->etag(),
                {},
                {}}));

        // Safety check: stop if we've collected the requested number of objects
        if (max_keys && children.size() >= max_keys)
            break;
    }
}

ObjectStorageIteratorPtr GCSObjectStorage::iterate(const std::string & path_prefix, size_t max_keys, bool) const
{
    auto settings_ptr = settings.get();
    auto client_ptr = client.get();

    return std::make_shared<GCSIteratorAsync>(
        path_prefix,
        client_ptr,
        object_namespace,
        max_keys ? max_keys : settings_ptr->list_object_keys_size);
}

ObjectMetadata GCSObjectStorage::getObjectMetadata(const std::string & path, bool with_tags) const
{
    throw Exception(ErrorCodes::NOT_IMPLEMENTED, "GCSObjectStorage::getObjectMetadata is not implemented");
}

std::optional<ObjectMetadata> GCSObjectStorage::tryGetObjectMetadata(const std::string & path, bool with_tags) const
{
    return std::nullopt;
}

std::unique_ptr<ReadBufferFromFileBase> GCSObjectStorage::readObject(
    const StoredObject & object,
    const ReadSettings & read_settings,
    std::optional<size_t> read_hint) const
{
    throw Exception(ErrorCodes::NOT_IMPLEMENTED, "GCSObjectStorage::readObject is not implemented");
}

SmallObjectDataWithMetadata GCSObjectStorage::readSmallObjectAndGetObjectMetadata(
    const StoredObject & object,
    const ReadSettings & read_settings,
    size_t max_size_bytes,
    std::optional<size_t> read_hint) const
{
    throw Exception(ErrorCodes::NOT_IMPLEMENTED, "GCSObjectStorage::readSmallObjectAndGetObjectMetadata is not implemented");
}

std::unique_ptr<WriteBufferFromFileBase> GCSObjectStorage::writeObject(
    const StoredObject & object,
    WriteMode mode,
    std::optional<ObjectAttributes> attributes,
    size_t buf_size,
    const WriteSettings & write_settings)
{
    throw Exception(ErrorCodes::NOT_IMPLEMENTED, "GCSObjectStorage::writeObject is not implemented");
}

void GCSObjectStorage::removeObjectIfExists(const StoredObject & object)
{
    throw Exception(ErrorCodes::NOT_IMPLEMENTED, "GCSObjectStorage::removeObjectIfExists is not implemented");
}

void GCSObjectStorage::removeObjectsIfExist(const StoredObjects & objects)
{
    throw Exception(ErrorCodes::NOT_IMPLEMENTED, "GCSObjectStorage::removeObjectsIfExist is not implemented");
}

void GCSObjectStorage::copyObject(
    const StoredObject & object_from,
    const StoredObject & object_to,
    const ReadSettings & read_settings,
    const WriteSettings & write_settings,
    std::optional<ObjectAttributes> object_to_attributes)
{
    throw Exception(ErrorCodes::NOT_IMPLEMENTED, "GCSObjectStorage::copyObject is not implemented");
}

void GCSObjectStorage::copyObjectToAnotherObjectStorage(
    const StoredObject & object_from,
    const StoredObject & object_to,
    const ReadSettings & read_settings,
    const WriteSettings & write_settings,
    IObjectStorage & object_storage_to,
    std::optional<ObjectAttributes> object_to_attributes)
{
    throw Exception(ErrorCodes::NOT_IMPLEMENTED, "GCSObjectStorage::copyObjectToAnotherObjectStorage is not implemented");
}

void GCSObjectStorage::shutdown()
{
    // No-op for now
}

void GCSObjectStorage::startup()
{
    // No-op for now
}

void GCSObjectStorage::applyNewSettings(
    const Poco::Util::AbstractConfiguration & config,
    const std::string & config_prefix,
    ContextPtr context,
    const ApplyNewSettingsOptions & options)
{
    throw Exception(ErrorCodes::NOT_IMPLEMENTED, "GCSObjectStorage::applyNewSettings is not implemented");
}

}
