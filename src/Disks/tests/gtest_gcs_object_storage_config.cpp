#include <gtest/gtest.h>

#include <Disks/DiskLocal.h>
#include <Disks/DiskObjectStorage/DiskObjectStorage.h>
#include <Disks/DiskObjectStorage/MetadataStorages/Local/MetadataStorageFromDisk.h>
#include <Disks/DiskObjectStorage/ObjectStorages/GCS/GCSObjectStorage.h>
#include <Disks/DiskObjectStorage/ObjectStorages/ObjectStorageIterator.h>
#include <Disks/DiskObjectStorage/ObjectStorages/ObjectStorageFactory.h>
#include <Disks/DiskObjectStorage/Replication/ClusterConfiguration.h>
#include <Disks/DiskObjectStorage/Replication/ObjectStorageRouter.h>
#include <Disks/registerDisks.h>
#include <Storages/ObjectStorage/StorageObjectStorageDefinitions.h>
#include <Common/Exception.h>
#include <Common/tests/gtest_global_context.h>
#include <IO/ReadHelpers.h>
#include <IO/WriteHelpers.h>

#include <Poco/TemporaryFile.h>
#include <Poco/Util/MapConfiguration.h>
#include <Poco/Util/XMLConfiguration.h>

#if USE_GOOGLE_CLOUD
#    include <absl/strings/cord.h>
#endif

#include <filesystem>
#include <optional>
#include <string>

namespace DB::ErrorCodes
{
    extern const int NOT_IMPLEMENTED;
}

using namespace DB;

namespace
{


#if USE_GOOGLE_CLOUD
String readAll(ReadBuffer & in)
{
    String result;
    readStringUntilEOF(result, in);
    return result;
}

String readBytes(ReadBuffer & in, size_t size)
{
    String result(size, '\0');
    in.readStrict(result.data(), size);
    return result;
}

String cordToString(const absl::Cord & cord)
{
    String result;
    absl::CopyCordToString(cord, &result);
    return result;
}

ReadSettings readSettings(size_t buffer_size)
{
    ReadSettings settings;
    settings.remote_fs_buffer_size = buffer_size;
    return settings;
}

std::shared_ptr<GCSObjectStorage> makeFakeGCSObjectStorage(const std::shared_ptr<GCS::FakeStub> & fake_stub, bool read_only = false)
{
    fake_stub->use_object_map = true;

    GCSObjectStorageSettings settings;
    settings.disk_name = "native_gcs_disk";
    settings.bucket = "native-bucket";
    settings.key_prefix = "clickhouse-data/";
    settings.description = "fake/native-bucket";
    settings.read_only = read_only;
    settings.client_settings.use_insecure_credentials_for_tests = true;

    return std::make_shared<GCSObjectStorage>(settings, std::make_shared<GCS::Client>(settings.client_settings, fake_stub));
}

void addFakeObject(const std::shared_ptr<GCS::FakeStub> & fake_stub, const String & path, const String & data = {})
{
    GCS::FakeStub::FakeObject object;
    object.data = data;
    object.metadata.set_bucket("projects/_/buckets/native-bucket");
    object.metadata.set_name(path);
    object.metadata.set_size(static_cast<int64_t>(data.size()));
    fake_stub->objects["projects/_/buckets/native-bucket\n" + path] = std::move(object);
}

StoredObject writeFakeObject(
    const std::shared_ptr<GCSObjectStorage> & storage,
    const String & path,
    const String & data,
    const ObjectAttributes & attributes = {})
{
    StoredObject object(path, path, data.size());
    std::optional<ObjectAttributes> object_attributes;
    if (!attributes.empty())
        object_attributes = attributes;
    auto out = storage->writeObject(object, WriteMode::Rewrite, std::move(object_attributes), 4, {});
    writeString(data, *out);
    out->finalize();
    return object;
}
#endif

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


#if USE_GOOGLE_CLOUD
TEST(GCSObjectStorageCore, FakeReadWriteListDeleteAndCopy)
{
    auto fake_stub = std::make_shared<GCS::FakeStub>();
    auto storage = makeFakeGCSObjectStorage(fake_stub);

    StoredObject object("clickhouse-data/object", "object");
    {
        auto out = storage->writeObject(object, WriteMode::Rewrite, ObjectAttributes{{"owner", "clickhouse"}}, 4, {});
        writeString("abcdef", *out);
        out->finalize();
    }

    ASSERT_TRUE(storage->exists(object));
    auto metadata = storage->getObjectMetadata(object.remote_path, /* with_tags */ true);
    EXPECT_EQ(6, metadata.size_bytes);
    EXPECT_EQ("clickhouse", metadata.attributes["owner"]);

    auto in = storage->readObject(object, {}, {});
    EXPECT_EQ("abcdef", readAll(*in));

    StoredObject bounded_object("clickhouse-data/object", "object", 3);
    auto bounded_in = storage->readObject(bounded_object, {}, {});
    EXPECT_EQ("abc", readAll(*bounded_in));
    ASSERT_FALSE(fake_stub->read_object_requests.empty());
    EXPECT_EQ(3, fake_stub->read_object_requests.back().read_limit());

    RelativePathsWithMetadata children;
    storage->listObjects("clickhouse-data/", children, 10);
    ASSERT_EQ(1, children.size());
    EXPECT_EQ("clickhouse-data/object", children.front()->relative_path);
    ASSERT_TRUE(children.front()->metadata.has_value());
    EXPECT_EQ(6, children.front()->metadata->size_bytes);

    StoredObject copied("clickhouse-data/copied", "copied");
    storage->copyObject(object, copied, {}, {}, {});
    auto copied_in = storage->readObject(copied, {}, {});
    EXPECT_EQ("abcdef", readAll(*copied_in));

    fake_stub->write_object_requests.clear();
    const size_t max_chunk = google::storage::v2::ServiceConstants::MAX_WRITE_CHUNK_BYTES;
    String large_payload(max_chunk + 7, 'x');
    StoredObject large_object("clickhouse-data/large", "large");
    {
        auto out = storage->writeObject(large_object, WriteMode::Rewrite, {}, large_payload.size(), {});
        out->write(large_payload.data(), large_payload.size());
        out->finalize();
    }
    ASSERT_GE(fake_stub->write_object_requests.size(), 2);
    int64_t current_write_offset = 0;
    for (size_t i = 0; i < fake_stub->write_object_requests.size(); ++i)
    {
        const auto & request = fake_stub->write_object_requests[i];
        EXPECT_EQ(current_write_offset, request.write_offset());
        if (request.has_checksummed_data())
        {
            EXPECT_LE(request.checksummed_data().content().size(), max_chunk);
            current_write_offset += request.checksummed_data().content().size();
        }
        EXPECT_EQ(i + 1 == fake_stub->write_object_requests.size(), request.finish_write());
    }
    EXPECT_EQ(static_cast<int64_t>(large_payload.size()), current_write_offset);

    EXPECT_THROW(storage->writeObject(object, WriteMode::Append, {}, 4, {}), Exception);

    storage->removeObjectIfExists(object);
    EXPECT_FALSE(storage->exists(object));
    storage->removeObjectIfExists(object);
    EXPECT_TRUE(storage->exists(copied));
}

TEST(GCSObjectStorageCore, FakeIteratorStatusAndDeleteFailures)
{
    auto fake_stub = std::make_shared<GCS::FakeStub>();
    auto storage = makeFakeGCSObjectStorage(fake_stub);

    StoredObject first("clickhouse-data/a", "a");
    StoredObject second("clickhouse-data/b", "b");
    {
        auto out = storage->writeObject(first, WriteMode::Rewrite, {}, 4, {});
        writeString("a", *out);
        out->finalize();
    }
    {
        auto out = storage->writeObject(second, WriteMode::Rewrite, {}, 4, {});
        writeString("b", *out);
        out->finalize();
    }

    auto iterator = storage->iterate("clickhouse-data/", 1, /* with_tags */ false, {});
    ASSERT_TRUE(iterator->isValid());
    EXPECT_EQ("clickhouse-data/a", iterator->current()->relative_path);
    EXPECT_EQ(1, iterator->getAccumulatedSize());

    auto resumed_iterator = storage->iterate("clickhouse-data/", 10, /* with_tags */ false, first.remote_path);
    ASSERT_TRUE(resumed_iterator->isValid());
    EXPECT_EQ("clickhouse-data/b", resumed_iterator->current()->relative_path);

    fake_stub->get_object_status = grpc::Status(grpc::StatusCode::PERMISSION_DENIED, "denied");
    EXPECT_THROW(storage->getObjectMetadata(first.remote_path, false), Exception);
    fake_stub->get_object_status = grpc::Status::OK;

    fake_stub->delete_object_status = grpc::Status(grpc::StatusCode::UNAVAILABLE, "unavailable");
    EXPECT_THROW(storage->removeObjectIfExists(first), Exception);
}

TEST(GCSObjectStorageReadBuffer, SeekPositionAndOffsetContracts)
{
    auto fake_stub = std::make_shared<GCS::FakeStub>();
    auto storage = makeFakeGCSObjectStorage(fake_stub);
    auto object = writeFakeObject(storage, "clickhouse-data/seek", "0123456789");
    auto settings = readSettings(4);

    auto in = storage->readObject(object, settings, {});
    EXPECT_EQ(0, in->getPosition());
    EXPECT_EQ("012", readBytes(*in, 3));
    EXPECT_EQ(3, in->getPosition());
    EXPECT_EQ(4, in->getFileOffsetOfBufferEnd());

    EXPECT_EQ(5, in->seek(5, SEEK_SET));
    EXPECT_EQ("56", readBytes(*in, 2));
    EXPECT_EQ(4, in->seek(-3, SEEK_CUR));
    EXPECT_EQ("45", readBytes(*in, 2));

    EXPECT_THROW(in->seek(-100, SEEK_CUR), Exception);
    EXPECT_THROW(in->seek(11, SEEK_SET), Exception);
}

TEST(GCSObjectStorageReadBuffer, RangeReadsAndEOF)
{
    auto fake_stub = std::make_shared<GCS::FakeStub>();
    auto storage = makeFakeGCSObjectStorage(fake_stub);
    auto settings = readSettings(4);

    auto empty = writeFakeObject(storage, "clickhouse-data/empty", "");
    EXPECT_EQ("", readAll(*storage->readObject(empty, settings, {})));

    auto exact = writeFakeObject(storage, "clickhouse-data/exact", "abcd");
    EXPECT_EQ("abcd", readAll(*storage->readObject(exact, settings, {})));

    auto object = writeFakeObject(storage, "clickhouse-data/range", "abcdefghij");
    fake_stub->read_object_requests.clear();
    auto in = storage->readObject(object, settings, {});
    String buffer(4, '\0');
    EXPECT_EQ(4, in->readBigAt(buffer.data(), buffer.size(), 2, {}));
    EXPECT_EQ("cdef", buffer);
    ASSERT_FALSE(fake_stub->read_object_requests.empty());
    EXPECT_EQ(2, fake_stub->read_object_requests.back().read_offset());
    EXPECT_EQ(4, fake_stub->read_object_requests.back().read_limit());

    StoredObject bounded(object.remote_path, object.local_path, 3);
    EXPECT_EQ("abc", readAll(*storage->readObject(bounded, settings, {})));
    ASSERT_FALSE(fake_stub->read_object_requests.empty());
    EXPECT_EQ(3, fake_stub->read_object_requests.back().read_limit());
}

TEST(GCSObjectStorageReadBuffer, StreamFinishFailure)
{
    auto fake_stub = std::make_shared<GCS::FakeStub>();
    auto storage = makeFakeGCSObjectStorage(fake_stub);
    fake_stub->use_object_map = false;

    google::storage::v2::ReadObjectResponse response;
    response.mutable_checksummed_data()->set_content("abc");
    fake_stub->read_object_responses = {response};
    fake_stub->read_object_finish_status = grpc::Status(grpc::StatusCode::UNAVAILABLE, "unavailable");

    auto in = storage->readObject(StoredObject("clickhouse-data/fails", "fails", 3), readSettings(4), {});
    EXPECT_THROW(readAll(*in), Exception);
    ASSERT_EQ(1, fake_stub->read_object_requests.size());
    EXPECT_EQ("clickhouse-data/fails", fake_stub->read_object_requests.front().object());
}

TEST(GCSObjectStorageWriteBuffer, EmptySmallAndChunkedWrites)
{
    auto fake_stub = std::make_shared<GCS::FakeStub>();
    auto storage = makeFakeGCSObjectStorage(fake_stub);

    StoredObject empty("clickhouse-data/empty-write", "empty-write");
    {
        auto out = storage->writeObject(empty, WriteMode::Rewrite, {}, 4, {});
        out->finalize();
    }
    ASSERT_EQ(1, fake_stub->write_object_requests.size());
    EXPECT_TRUE(fake_stub->write_object_requests.front().finish_write());
    EXPECT_EQ(0, fake_stub->write_object_requests.front().write_offset());
    EXPECT_FALSE(fake_stub->write_object_requests.front().has_checksummed_data());

    fake_stub->write_object_requests.clear();
    StoredObject small("clickhouse-data/small-write", "small-write");
    {
        auto out = storage->writeObject(small, WriteMode::Rewrite, ObjectAttributes{{"owner", "clickhouse"}}, 8, {});
        writeString("abc", *out);
        out->finalize();
    }
    ASSERT_EQ(1, fake_stub->write_object_requests.size());
    const auto & small_request = fake_stub->write_object_requests.front();
    EXPECT_EQ(0, small_request.write_offset());
    EXPECT_TRUE(small_request.finish_write());
    EXPECT_EQ("abc", cordToString(small_request.checksummed_data().content()));
    EXPECT_EQ("clickhouse", small_request.write_object_spec().resource().metadata().at("owner"));

    fake_stub->write_object_requests.clear();
    const size_t max_chunk = google::storage::v2::ServiceConstants::MAX_WRITE_CHUNK_BYTES;
    String exact_payload(max_chunk, 'x');
    StoredObject exact("clickhouse-data/exact-chunk", "exact-chunk");
    {
        auto out = storage->writeObject(exact, WriteMode::Rewrite, {}, max_chunk * 2, {});
        out->write(exact_payload.data(), exact_payload.size());
        out->finalize();
    }
    ASSERT_EQ(1, fake_stub->write_object_requests.size());
    EXPECT_EQ(0, fake_stub->write_object_requests.front().write_offset());
    EXPECT_EQ(max_chunk, fake_stub->write_object_requests.front().checksummed_data().content().size());
    EXPECT_TRUE(fake_stub->write_object_requests.front().finish_write());
}

TEST(GCSObjectStorageWriteBuffer, RepeatedSyncAndFinalize)
{
    auto fake_stub = std::make_shared<GCS::FakeStub>();
    auto storage = makeFakeGCSObjectStorage(fake_stub);

    StoredObject object("clickhouse-data/sync", "sync");
    auto out = storage->writeObject(object, WriteMode::Rewrite, {}, 4, {});
    writeString("abc", *out);
    out->sync();
    writeString("def", *out);
    out->finalize();

    EXPECT_EQ(1, fake_stub->write_object_finish_calls);
    ASSERT_EQ(2, fake_stub->write_object_requests.size());
    EXPECT_EQ(0, fake_stub->write_object_requests[0].write_offset());
    EXPECT_FALSE(fake_stub->write_object_requests[0].finish_write());
    EXPECT_EQ(3, fake_stub->write_object_requests[1].write_offset());
    EXPECT_TRUE(fake_stub->write_object_requests[1].finish_write());
}

TEST(GCSObjectStorageWriteBuffer, WriteFalseReportsFinishStatus)
{
    auto fake_stub = std::make_shared<GCS::FakeStub>();
    auto storage = makeFakeGCSObjectStorage(fake_stub);
    fake_stub->use_object_map = false;
    fake_stub->write_object_write_returns_false = true;
    fake_stub->write_object_finish_status = grpc::Status(grpc::StatusCode::PERMISSION_DENIED, "denied");

    auto out = storage->writeObject(StoredObject("clickhouse-data/write-false", "write-false"), WriteMode::Rewrite, {}, 4, {});
    writeString("abc", *out);
    EXPECT_THROW(out->finalize(), Exception);
    EXPECT_EQ(1, fake_stub->write_object_finish_calls);
}

TEST(GCSObjectStorageWriteBuffer, WritesDoneAndFinishFailures)
{
    {
        auto fake_stub = std::make_shared<GCS::FakeStub>();
        auto storage = makeFakeGCSObjectStorage(fake_stub);
        fake_stub->use_object_map = false;
        fake_stub->write_object_writes_done_returns_false = true;

        auto out = storage->writeObject(StoredObject("clickhouse-data/writes-done", "writes-done"), WriteMode::Rewrite, {}, 4, {});
        writeString("abc", *out);
        EXPECT_THROW(out->finalize(), Exception);
        EXPECT_EQ(1, fake_stub->write_object_finish_calls);
    }

    for (auto code : {grpc::StatusCode::PERMISSION_DENIED, grpc::StatusCode::UNAVAILABLE, grpc::StatusCode::INVALID_ARGUMENT})
    {
        auto fake_stub = std::make_shared<GCS::FakeStub>();
        auto storage = makeFakeGCSObjectStorage(fake_stub);
        fake_stub->use_object_map = false;
        fake_stub->write_object_finish_status = grpc::Status(code, "finish failed");

        auto out = storage->writeObject(StoredObject("clickhouse-data/finish-fails", "finish-fails"), WriteMode::Rewrite, {}, 4, {});
        writeString("abc", *out);
        EXPECT_THROW(out->finalize(), Exception);
        EXPECT_EQ(1, fake_stub->write_object_finish_calls);
    }
}

TEST(GCSObjectStorageCore, ListPaginationAndExclusiveStartAfter)
{
    auto fake_stub = std::make_shared<GCS::FakeStub>();
    auto storage = makeFakeGCSObjectStorage(fake_stub);

    for (size_t i = 0; i != 1002; ++i)
        addFakeObject(fake_stub, "clickhouse-data/page-" + std::to_string(100000 + i));

    RelativePathsWithMetadata children;
    storage->listObjects("clickhouse-data/page-", children, 0);
    EXPECT_EQ(1002, children.size());
    ASSERT_GE(fake_stub->list_objects_requests.size(), 2);
    EXPECT_EQ("1000", fake_stub->list_objects_requests[1].page_token());

    auto iterator = storage->iterate("clickhouse-data/page-", 3, /* with_tags */ false, "clickhouse-data/page-100000");
    ASSERT_TRUE(iterator->isValid());
    EXPECT_EQ("clickhouse-data/page-100001", iterator->current()->relative_path);
    ASSERT_FALSE(fake_stub->list_objects_requests.empty());
    EXPECT_EQ("clickhouse-data/page-100000", fake_stub->list_objects_requests.back().lexicographic_start());
}

TEST(GCSObjectStorageCore, MetadataAndNotFoundSemantics)
{
    auto fake_stub = std::make_shared<GCS::FakeStub>();
    auto storage = makeFakeGCSObjectStorage(fake_stub);
    auto object = writeFakeObject(storage, "clickhouse-data/metadata", "payload", ObjectAttributes{{"owner", "clickhouse"}});

    EXPECT_TRUE(storage->exists(object));
    auto metadata = storage->getObjectMetadata(object.remote_path, /* with_tags */ true);
    EXPECT_EQ(7, metadata.size_bytes);
    EXPECT_EQ("clickhouse", metadata.attributes["owner"]);
    EXPECT_TRUE(metadata.tags.empty());

    EXPECT_FALSE(storage->tryGetObjectMetadata("clickhouse-data/missing", false).has_value());
    EXPECT_THROW(storage->getObjectMetadata("clickhouse-data/missing", false), Exception);

    fake_stub->get_object_status = grpc::Status(grpc::StatusCode::UNAVAILABLE, "unavailable");
    EXPECT_THROW(storage->exists(object), Exception);
}

TEST(GCSObjectStorageCore, ReadOnlyRejectsWritesAndInvalidNames)
{
    auto fake_stub = std::make_shared<GCS::FakeStub>();
    auto read_only_storage = makeFakeGCSObjectStorage(fake_stub, true);
    EXPECT_THROW(
        read_only_storage->writeObject(StoredObject("clickhouse-data/read-only", "read-only"), WriteMode::Rewrite, {}, 4, {}),
        Exception);

    auto storage = makeFakeGCSObjectStorage(fake_stub);
    EXPECT_THROW(storage->writeObject(StoredObject("/absolute", "absolute"), WriteMode::Rewrite, {}, 4, {}), Exception);
    EXPECT_THROW(storage->readObject(StoredObject("/absolute", "absolute"), {}, {}), Exception);
    EXPECT_THROW(storage->getObjectMetadata("/absolute", false), Exception);
    RelativePathsWithMetadata children;
    EXPECT_THROW(storage->listObjects("/absolute", children, 1), Exception);

    auto config = makeNativeGCSConfig();
    config->setString("disk.key_prefix", "/absolute");
    EXPECT_THROW(getGCSObjectStorageSettings("native_gcs_disk", *config, "disk", getContext().context), Exception);
}

TEST(GCSObjectStorageCore, DeleteNotFoundAndMultiDelete)
{
    auto fake_stub = std::make_shared<GCS::FakeStub>();
    auto storage = makeFakeGCSObjectStorage(fake_stub);
    auto first = writeFakeObject(storage, "clickhouse-data/delete-a", "a");
    auto second = writeFakeObject(storage, "clickhouse-data/delete-b", "b");
    StoredObject missing("clickhouse-data/delete-missing", "delete-missing");

    storage->removeObjectIfExists(missing);
    storage->removeObjectsIfExist({first, missing, second});

    EXPECT_FALSE(storage->exists(first));
    EXPECT_FALSE(storage->exists(second));
    EXPECT_FALSE(storage->exists(missing));
}


TEST(GCSObjectStorageCore, FakeDiskObjectStorageLocalMetadataScenario)
{
    auto fake_stub = std::make_shared<GCS::FakeStub>();
    ObjectStoragePtr object_storage = makeFakeGCSObjectStorage(fake_stub);

    Poco::TemporaryFile temp_dir;
    temp_dir.createDirectories();
    const auto metadata_path = std::filesystem::path(temp_dir.path()) / "metadata";
    std::filesystem::create_directories(metadata_path);
    auto metadata_disk = std::make_shared<DiskLocal>("metadata_disk", metadata_path);
    MetadataStoragePtr metadata_storage = std::make_shared<MetadataStorageFromDisk>(
        metadata_disk,
        "/",
        object_storage->createKeyGenerator(),
        /* persist_removal_queue_ */ true,
        /* removal_log_compaction_threshold_ */ 1000);

    std::unordered_map<Location, LocationInfo> cluster_registry = {{"main", {true, true, ""}}};
    std::unordered_map<Location, ObjectStoragePtr> object_storage_registry = {{"main", object_storage}};
    auto cluster = std::make_shared<ClusterConfiguration>("native_gcs", std::move(cluster_registry));
    auto object_storages = std::make_shared<ObjectStorageRouter>(std::move(object_storage_registry));
    Poco::AutoPtr<Poco::Util::XMLConfiguration> config(new Poco::Util::XMLConfiguration());
    auto disk = std::make_shared<DiskObjectStorage>(
        "native_gcs",
        std::move(cluster),
        std::move(metadata_storage),
        std::move(object_storages),
        nullptr,
        *config,
        "");

    disk->createDirectory("dir");
    {
        auto out = disk->writeFile("dir/file.txt", DBMS_DEFAULT_BUFFER_SIZE, WriteMode::Rewrite, {});
        writeString("payload", *out);
        out->finalize();
    }

    EXPECT_TRUE(disk->existsFile("dir/file.txt"));
    EXPECT_EQ(7, disk->getFileSize("dir/file.txt"));
    auto in = disk->readFile("dir/file.txt", {}, {});
    EXPECT_EQ("payload", readAll(*in));

    std::vector<String> files;
    disk->listFiles("dir", files);
    EXPECT_EQ(std::vector<String>{"file.txt"}, files);

    {
        auto out = disk->writeFile("dir/file.txt", DBMS_DEFAULT_BUFFER_SIZE, WriteMode::Rewrite, {});
        writeString("rewritten", *out);
        out->finalize();
    }
    auto rewritten = disk->readFile("dir/file.txt", {}, {});
    EXPECT_EQ("rewritten", readAll(*rewritten));

    disk->removeFile("dir/file.txt");
    EXPECT_FALSE(disk->existsFile("dir/file.txt"));
    disk->shutdown();
}
#endif
