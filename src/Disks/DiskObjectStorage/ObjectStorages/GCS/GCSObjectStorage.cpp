#include <Disks/DiskObjectStorage/ObjectStorages/GCS/GCSObjectStorage.h>

#include <Common/Exception.h>
#include <Common/Macros.h>
#include <Common/ObjectStorageKeyGenerator.h>
#include <Interpreters/Context.h>

#include <fmt/format.h>

namespace DB
{

namespace ErrorCodes
{
    extern const int BAD_ARGUMENTS;
    extern const int NOT_IMPLEMENTED;
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

bool GCSObjectStorage::exists(const StoredObject &) const
{
    throwNotImplemented("exists");
}

std::unique_ptr<ReadBufferFromFileBase> GCSObjectStorage::readObject(
    const StoredObject &,
    const ReadSettings &,
    std::optional<size_t>) const
{
    throwNotImplemented("readObject");
}

std::unique_ptr<WriteBufferFromFileBase> GCSObjectStorage::writeObject(
    const StoredObject &,
    WriteMode,
    std::optional<ObjectAttributes>,
    size_t,
    const WriteSettings &)
{
    throwNotImplemented("writeObject");
}

void GCSObjectStorage::removeObjectIfExists(const StoredObject &)
{
    throwNotImplemented("removeObjectIfExists");
}

void GCSObjectStorage::removeObjectsIfExist(const StoredObjects &)
{
    throwNotImplemented("removeObjectsIfExist");
}

void GCSObjectStorage::copyObject(
    const StoredObject &,
    const StoredObject &,
    const ReadSettings &,
    const WriteSettings &,
    std::optional<ObjectAttributes>)
{
    throwNotImplemented("copyObject");
}

ObjectMetadata GCSObjectStorage::getObjectMetadata(const std::string &, bool) const
{
    throwNotImplemented("getObjectMetadata");
}

std::optional<ObjectMetadata> GCSObjectStorage::tryGetObjectMetadata(const std::string &, bool) const
{
    throwNotImplemented("tryGetObjectMetadata");
}

void GCSObjectStorage::listObjects(const std::string &, RelativePathsWithMetadata &, size_t) const
{
    throwNotImplemented("listObjects");
}

ObjectStorageIteratorPtr GCSObjectStorage::iterate(
    const std::string &,
    size_t,
    bool,
    const std::optional<std::string> &) const
{
    throwNotImplemented("iterate");
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
