#pragma once

#include "config.h"

#if USE_GOOGLE_CLOUD_STORAGE

#include "GCSBlobStorageCommon.h"
#include <Disks/DiskObjectStorage/ObjectStorages/IObjectStorage.h>
#include "Common/MultiVersion.h"
#include <google/cloud/storage/client.h>
#include <google/cloud/storage/grpc_plugin.h>


namespace DB
{

class GCSObjectStorage : public IObjectStorage
{
public:
    using SettingsPtr = std::unique_ptr<GCSBlobStorage::RequestSettings>;
    using ClientPtr = std::unique_ptr<google::cloud::storage::Client>;

    GCSObjectStorage(
        ClientPtr && client_,
        SettingsPtr && settings_,
        const String & object_namespace_,
        const String & description_);

    String getName() const override { return "GCSObjectStorage"; }

    ObjectStorageType getType() const override { return ObjectStorageType::GCS; }

    String getRootPrefix() const override { return object_namespace; }

    /// Need to review the description here
    String getDescription() const override { return description; }

    bool isRemote() const override { return true; }

    String getObjectsNamespace() const override { return object_namespace ; }

    bool isReadOnly() const override { return settings.get()->read_only; }

    bool supportParallelWrite() const override { return true; }

    /// Needs implementation
    ///
    ///
    ///
    bool exists(const StoredObject & object) const override;

    void listObjects(const std::string & path, RelativePathsWithMetadata & children, size_t max_keys) const override;

    ObjectStorageIteratorPtr iterate(const std::string & path_prefix, size_t max_keys, bool with_tags) const override;

    ObjectMetadata getObjectMetadata(const std::string & path, bool with_tags) const override;

    std::optional<ObjectMetadata> tryGetObjectMetadata(const std::string & path, bool with_tags) const override;

    std::unique_ptr<ReadBufferFromFileBase> readObject( /// NOLINT
        const StoredObject & object,
        const ReadSettings & read_settings,
        std::optional<size_t> read_hint = {}) const override;

    SmallObjectDataWithMetadata readSmallObjectAndGetObjectMetadata( /// NOLINT
        const StoredObject & object,
        const ReadSettings & read_settings,
        size_t max_size_bytes,
        std::optional<size_t> read_hint = {}) const override;

    std::unique_ptr<WriteBufferFromFileBase> writeObject( /// NOLINT
        const StoredObject & object,
        WriteMode mode,
        std::optional<ObjectAttributes> attributes = {},
        size_t buf_size = DBMS_DEFAULT_BUFFER_SIZE,
        const WriteSettings & write_settings = {}) override;

    void removeObjectIfExists(const StoredObject & object) override;

    void removeObjectsIfExist(const StoredObjects & objects) override;

    void copyObject( /// NOLINT
        const StoredObject & object_from,
        const StoredObject & object_to,
        const ReadSettings & read_settings,
        const WriteSettings & write_settings,
        std::optional<ObjectAttributes> object_to_attributes = {}) override;

    void copyObjectToAnotherObjectStorage( /// NOLINT
        const StoredObject & object_from,
        const StoredObject & object_to,
        const ReadSettings & read_settings,
        const WriteSettings & write_settings,
        IObjectStorage & object_storage_to,
        std::optional<ObjectAttributes> object_to_attributes = {}) override;

    void shutdown() override;

    void startup() override;

    void applyNewSettings(
        const Poco::Util::AbstractConfiguration & config,
        const std::string & config_prefix,
        ContextPtr context,
        const ApplyNewSettingsOptions & options) override;

    ObjectStorageKeyGeneratorPtr createKeyGenerator() const override;

private:
    std::shared_ptr<google::cloud::storage::Client> getClient() const {return std::const_pointer_cast<google::cloud::storage::Client>(client.get());}

    const String object_namespace; /// bucket

    MultiVersion<google::cloud::storage::Client> client;

    MultiVersion<GCSBlobStorage::RequestSettings> settings;

    /// <Look at again> We use source url without bucket as description, because in GCS there are no limitations for operations between different containers.
    const String description;
};

}

#endif

