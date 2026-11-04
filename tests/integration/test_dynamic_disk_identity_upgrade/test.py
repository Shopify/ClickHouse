import pytest

from helpers.cluster import ClickHouseCluster
from helpers.test_tools import assert_eq_with_retry


@pytest.fixture(scope="module")
def started_cluster():
    cluster = ClickHouseCluster(__file__)
    cluster.add_instance(
        "node",
        image="clickhouse/clickhouse-server",
        tag="26.3.33.24",
        with_installed_binary=True,
        with_minio=True,
        stay_alive=True,
        main_configs=["configs/dynamic_disks.xml"],
        user_configs=["configs/identity.xml"],
        env_variables={
            "S3_ENDPOINT": "http://minio1:9001/root/identity_legacy/",
            "S3_ENDPOINT_MODERN": "http://minio1:9001/root/identity_modern/",
            "S3_ENDPOINT_ALIAS": "http://minio1:9001/root/identity_legacy/",
            "S3_KEY": "minio",
            "S3_SECRET": "ClickHouse_Minio_P@ssw0rd",
        },
    )
    try:
        cluster.start()
        yield cluster
    finally:
        cluster.shutdown()


def test_upgrade_redeclare_exchange_and_restart(started_cluster):
    node = started_cluster.instances["node"]
    writer_disk = """disk(type = 's3_plain_rewritable',
        endpoint = 'from_env S3_ENDPOINT', access_key_id = 'from_env S3_KEY',
        secret_access_key = 'from_env S3_SECRET')"""
    reader_disk = """disk(name = 'legacy_reader_cache', type = 'cache',
        path = '/var/lib/clickhouse/caches/identity_legacy/', max_size = '1Gi',
        disk = disk(type = 's3_plain_rewritable', endpoint = 'from_env S3_ENDPOINT',
        access_key_id = 'from_env S3_KEY', secret_access_key = 'from_env S3_SECRET', readonly = true))"""
    node.query("CREATE DATABASE identity ENGINE = Atomic")
    for name, disk in [("w", writer_disk), ("r", reader_disk)]:
        node.query(
            f"CREATE TABLE identity.{name} (x UInt64) ENGINE = MergeTree ORDER BY x "
            f"SETTINGS table_disk = true, refresh_parts_interval = 1, disk = {disk}"
        )
    node.query("INSERT INTO identity.w VALUES (1), (2), (3)")
    assert_eq_with_retry(node, "SELECT count(), sum(x) FROM identity.r", "3\t6")

    identities_query = """SELECT name, storage_policy, data_paths FROM system.tables
        WHERE database = 'identity' AND name IN ('r', 'w') ORDER BY name"""
    old_identities = node.query(identities_query)
    old_parts = node.query(
        "SELECT name, rows, bytes_on_disk FROM system.parts "
        "WHERE database = 'identity' AND table = 'w' AND active ORDER BY name"
    )

    def enable_credentials_for_new_server(instance):
        instance.replace_config(
            "/etc/clickhouse-server/users.d/identity.xml",
            """<clickhouse><profiles><default>
                <dynamic_disk_allow_from_env>1</dynamic_disk_allow_from_env>
                <s3_allow_server_credentials_in_user_queries>1</s3_allow_server_credentials_in_user_queries>
                </default></profiles></clickhouse>""",
        )

    node.restart_with_latest_version(callback_onstop=enable_credentials_for_new_server)
    assert node.query(identities_query) == old_identities
    assert node.query("SELECT count(), sum(x) FROM identity.w") == "3\t6\n"
    assert_eq_with_retry(node, "SELECT count(), sum(x) FROM identity.r", "3\t6")

    # This is a real old-data-directory upgrade, not a new table created on the patched server.
    for name, disk, uuid in [
        ("w", writer_disk, "00000000-0000-0000-0000-000000000101"),
        ("r", reader_disk, "00000000-0000-0000-0000-000000000102"),
    ]:
        node.query(
            f"ATTACH TABLE identity.{name}_wide UUID '{uuid}' (x UInt64, y String) "
            f"ENGINE = MergeTree ORDER BY x SETTINGS table_disk = true, "
            f"refresh_parts_interval = 1, disk = {disk}"
        )
        assert_eq_with_retry(
            node, f"SELECT count(), sum(x) FROM identity.{name}_wide", "3\t6"
        )
        # Persisting a fresh marker here would rename the reused legacy backend at the next restart.
        assert node.query(
            "SELECT position(create_table_query, '_server_credentials_allowed') "
            f"FROM system.tables WHERE database = 'identity' AND name = '{name}_wide'"
        ) == "0\n"
        node.query(f"EXCHANGE TABLES identity.{name} AND identity.{name}_wide")
        # Do not drop another binding on the shared writable prefix.
        node.query(f"DETACH TABLE identity.{name}_wide PERMANENTLY")

    assert node.query(identities_query) == old_identities
    assert node.query(
        "SELECT name, rows, bytes_on_disk FROM system.parts "
        "WHERE database = 'identity' AND table = 'w' AND active ORDER BY name"
    ) == old_parts
    node.query("INSERT INTO identity.w VALUES (4, 'new')")
    assert_eq_with_retry(
        node, "SELECT x, y FROM identity.r ORDER BY x", "1\t\n2\t\n3\t\n4\tnew"
    )

    # A genuinely different definition must not register another writable backend on the same prefix.
    conflicting_disk = writer_disk.replace("S3_ENDPOINT", "S3_ENDPOINT_ALIAS")
    error = node.query_and_get_error(
        "CREATE TABLE identity.conflicting (x UInt64) ENGINE = MergeTree ORDER BY x "
        f"SETTINGS table_disk = true, disk = {conflicting_disk}"
    )
    assert "multiple plain-rewritable disks with the same object storage prefix" in error

    # A newly created marked disk must keep its current identity as well.
    modern_disk = writer_disk.replace("S3_ENDPOINT", "S3_ENDPOINT_MODERN")
    node.query(
        "CREATE TABLE identity.modern (x UInt64) ENGINE = MergeTree ORDER BY x "
        f"SETTINGS table_disk = true, disk = {modern_disk}"
    )
    node.query("INSERT INTO identity.modern VALUES (42)")
    modern_policy = node.query(
        "SELECT storage_policy FROM system.tables WHERE database = 'identity' AND name = 'modern'"
    )
    node.restart_clickhouse()
    assert node.query(identities_query) == old_identities
    assert node.query("SELECT x, y FROM identity.w ORDER BY x") == "1\t\n2\t\n3\t\n4\tnew\n"
    assert_eq_with_retry(
        node, "SELECT x, y FROM identity.r ORDER BY x", "1\t\n2\t\n3\t\n4\tnew"
    )
    assert node.query("SELECT count(), sum(x) FROM identity.modern") == "1\t42\n"
    node.query(
        "ATTACH TABLE identity.modern_wide UUID '00000000-0000-0000-0000-000000000103' "
        "(x UInt64, y String) ENGINE = MergeTree ORDER BY x "
        f"SETTINGS table_disk = true, disk = {modern_disk}"
    )
    assert node.query(
        "SELECT storage_policy FROM system.tables WHERE database = 'identity' AND name = 'modern_wide'"
    ) == modern_policy
    error = node.query_and_get_error(
        "CREATE TABLE identity.denied (x UInt64) ENGINE = MergeTree ORDER BY x "
        f"SETTINGS table_disk = true, disk = {writer_disk}",
        settings={"s3_allow_server_credentials_in_user_queries": 0},
    )
    assert "ACCESS_DENIED" in error
