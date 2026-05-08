#include <gtest/gtest.h>

#include <Disks/DiskObjectStorage/ObjectStorages/GCS/GCSObjectStorage.h>
#include <Disks/DiskObjectStorage/ObjectStorages/ObjectStorageFactory.h>
#include <Disks/registerDisks.h>
#include <Storages/ObjectStorage/StorageObjectStorageDefinitions.h>
#include <Common/Exception.h>
#include <Common/tests/gtest_global_context.h>

#include <Poco/Util/MapConfiguration.h>

namespace DB::ErrorCodes
{
    extern const int NOT_IMPLEMENTED;
}

using namespace DB;

namespace
{

Poco::AutoPtr<Poco::Util::MapConfiguration> makeNativeGCSConfig()
{
    Poco::AutoPtr<Poco::Util::MapConfiguration> config = new Poco::Util::MapConfiguration;
    config->setString("disk.object_storage_type", "gcs");
    config->setString("disk.bucket", "native-bucket");
    config->setString("disk.key_prefix", "clickhouse-data");
    config->setString("disk.endpoint", "storage.googleapis.com");
    config->setBool("disk.use_insecure_credentials_for_tests", true);
    config->setUInt64("disk.request_timeout_ms", 1000);
    return config;
}

class GCSObjectStorageConfigTest : public testing::Test
{
public:
    void SetUp() override
    {
        clearDiskRegistry();
        registerDisks(/* global_skip_access_check */ true);
    }

    void TearDown() override
    {
        clearDiskRegistry();
    }
};

}

TEST(GCSDiskType, NativeGCSIdentity)
{
    DataSourceDescription native_gcs;
    native_gcs.type = DataSourceType::ObjectStorage;
    native_gcs.object_storage_type = ObjectStorageType::GCS;
    native_gcs.description = "storage.googleapis.com/native-bucket";

    DataSourceDescription native_gcs_with_trailing_slash;
    native_gcs_with_trailing_slash.type = DataSourceType::ObjectStorage;
    native_gcs_with_trailing_slash.object_storage_type = ObjectStorageType::GCS;
    native_gcs_with_trailing_slash.description = "storage.googleapis.com/native-bucket/";

    DataSourceDescription s3_compatible_gcs;
    s3_compatible_gcs.type = DataSourceType::ObjectStorage;
    s3_compatible_gcs.object_storage_type = ObjectStorageType::S3;
    s3_compatible_gcs.description = "storage.googleapis.com/native-bucket";

    EXPECT_EQ("gcs", native_gcs.name());
    EXPECT_TRUE(native_gcs.sameKind(native_gcs_with_trailing_slash));
    EXPECT_FALSE(native_gcs.sameKind(s3_compatible_gcs));
}

TEST_F(GCSObjectStorageConfigTest, NativeGCSConfigUsesNativeFactoryEntry)
{
    auto config = makeNativeGCSConfig();

#if USE_GOOGLE_CLOUD
    auto storage = ObjectStorageFactory::instance().create(
        "native_gcs_disk", *config, "disk", getContext().context, /* skip_access_check */ true);

    EXPECT_EQ(ObjectStorageType::GCS, storage->getType());
    EXPECT_NE(ObjectStorageType::S3, storage->getType());
    EXPECT_EQ("GCS", storage->getName());
    EXPECT_EQ("native_gcs_disk", storage->getDiskName());
    EXPECT_EQ("native-bucket", storage->getObjectsNamespace());
    EXPECT_EQ("native-bucket", storage->getRootPrefix());
    EXPECT_EQ("clickhouse-data/", storage->getCommonKeyPrefix());
    EXPECT_EQ("storage.googleapis.com/native-bucket", storage->getDescription());

    EXPECT_THROW(storage->exists(StoredObject("clickhouse-data/object")), Exception);
#else
    try
    {
        (void)ObjectStorageFactory::instance().create(
            "native_gcs_disk", *config, "disk", getContext().context, /* skip_access_check */ true);
        FAIL() << "Native GCS object storage unexpectedly constructed without Google Cloud support";
    }
    catch (const Exception & e)
    {
        EXPECT_EQ(ErrorCodes::NOT_IMPLEMENTED, e.code());
        EXPECT_NE(std::string(e.message()).find("Native GCS gRPC support is not available"), std::string::npos);
    }
#endif
}

TEST_F(GCSObjectStorageConfigTest, NativeGCSConfigKeepsTableFunctionGCSDefinitionSeparate)
{
    EXPECT_STREQ("gcs", GCSDefinition::name);
    EXPECT_STREQ("GCS", GCSDefinition::storage_engine_name);
    EXPECT_STREQ("gcs", GCSDefinition::object_storage_type);

    auto config = makeNativeGCSConfig();
#if USE_GOOGLE_CLOUD
    auto storage = ObjectStorageFactory::instance().create(
        "native_gcs_disk", *config, "disk", getContext().context, /* skip_access_check */ true);
    EXPECT_EQ(ObjectStorageType::GCS, storage->getType());
#else
    EXPECT_THROW(
        ObjectStorageFactory::instance().create(
            "native_gcs_disk", *config, "disk", getContext().context, /* skip_access_check */ true),
        Exception);
#endif
}

TEST_F(GCSObjectStorageConfigTest, NativeGCSConfigRequiresBucket)
{
    auto config = makeNativeGCSConfig();
    config->remove("disk.bucket");

    EXPECT_THROW(
        ObjectStorageFactory::instance().create(
            "native_gcs_disk", *config, "disk", getContext().context, /* skip_access_check */ true),
        Exception);
}
