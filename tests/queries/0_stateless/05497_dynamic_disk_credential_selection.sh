#!/usr/bin/env bash
# Tags: zookeeper, no-fasttest
# The indirect disk type needs Keeper; the encrypted fixture needs SSL.

CUR_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
# shellcheck source=../shell_config.sh
. "$CUR_DIR"/../shell_config.sh

ZK_PATH="/clickhouse/${CLICKHOUSE_TEST_UNIQUE_NAME}"
SERVER_DISK="selection_server_${CLICKHOUSE_DATABASE}"
PLAIN_DISK="selection_plain_${CLICKHOUSE_DATABASE}"
KEY="1234567812345678"

run()
{
    local out
    if out=$(${CLICKHOUSE_CLIENT} --multiquery "$@" 2>&1); then
        echo "OK"
    else
        echo "$out" | grep -oE '\([A-Z_]+\)' | head -1
    fi
}

${CLICKHOUSE_KEEPER_CLIENT} -q "create '${ZK_PATH}' 'encrypted'"

# An indirect type makes the server-credential provenance observable without real S3 credentials.
echo "create credential disk: $(run --dynamic_disk_allow_from_zk=1 --s3_allow_server_credentials_in_user_queries=1 -q "CREATE TABLE source_disk (x UInt8) ENGINE = MergeTree ORDER BY x SETTINGS disk = disk(name = '${SERVER_DISK}', type = 'from_zk ${ZK_PATH}', disk = 'default', path = 'disks/${CLICKHOUSE_DATABASE}_selection_server/', key = '${KEY}')")"
echo "create literal disk: $(run --s3_allow_server_credentials_in_user_queries=1 -q "CREATE TABLE plain_disk (x UInt8) ENGINE = MergeTree ORDER BY x SETTINGS disk = disk(name = '${PLAIN_DISK}', type = encrypted, disk = 'default', path = 'disks/${CLICKHOUSE_DATABASE}_selection_plain/', key = '${KEY}')")"

for engine in Log TinyLog StripeLog Set; do
    echo "${engine} direct: $(run --s3_allow_server_credentials_in_user_queries=0 -q "CREATE TABLE denied_${engine} (x UInt8) ENGINE = ${engine} SETTINGS disk = '${SERVER_DISK}'")"
done
echo "Join direct: $(run --s3_allow_server_credentials_in_user_queries=0 -q "CREATE TABLE denied_join (x UInt8) ENGINE = Join(ANY, LEFT, x) SETTINGS disk = '${SERVER_DISK}'")"

for engine in Log TinyLog StripeLog; do
    echo "${engine} policy: $(run --s3_allow_server_credentials_in_user_queries=0 -q "CREATE TABLE denied_policy_${engine} (x UInt8) ENGINE = ${engine} SETTINGS storage_policy = '__${SERVER_DISK}'")"
done
echo "Distributed policy: $(run --s3_allow_server_credentials_in_user_queries=0 -q "CREATE TABLE denied_distributed (x UInt8) ENGINE = Distributed(test_shard_localhost, currentDatabase(), source_disk, rand(), '__${SERVER_DISK}')")"

# Selecting a disk that did not use server credentials remains allowed.
echo "Log literal disk: $(run --s3_allow_server_credentials_in_user_queries=0 -q "CREATE TABLE allowed_log (x UInt8) ENGINE = Log SETTINGS disk = '${PLAIN_DISK}'; INSERT INTO allowed_log VALUES (42)")"
echo "Set literal disk: $(run --s3_allow_server_credentials_in_user_queries=0 -q "CREATE TABLE allowed_set (x UInt8) ENGINE = Set SETTINGS disk = '${PLAIN_DISK}'; INSERT INTO allowed_set VALUES (42)")"
echo "Join literal disk: $(run --s3_allow_server_credentials_in_user_queries=0 -q "CREATE TABLE allowed_join (x UInt8) ENGINE = Join(ANY, LEFT, x) SETTINGS disk = '${PLAIN_DISK}'; INSERT INTO allowed_join VALUES (42)")"
echo "Log opted-in disk: $(run --s3_allow_server_credentials_in_user_queries=1 -q "CREATE TABLE allowed_opted (x UInt8) ENGINE = Log SETTINGS disk = '${SERVER_DISK}'; INSERT INTO allowed_opted VALUES (42)")"

# Keeping the same policy is table maintenance, not a fresh selection of server credentials.
echo "create opted-in policy: $(run --s3_allow_server_credentials_in_user_queries=1 -q "CREATE TABLE alter_policy (x UInt8) ENGINE = MergeTree ORDER BY x SETTINGS storage_policy = '__${SERVER_DISK}'")"
echo "unrelated column ALTER: $(run --s3_allow_server_credentials_in_user_queries=0 -q "ALTER TABLE alter_policy ADD COLUMN y UInt8")"
echo "unrelated setting ALTER: $(run --s3_allow_server_credentials_in_user_queries=0 -q "ALTER TABLE alter_policy MODIFY SETTING parts_to_throw_insert = 500")"
echo "create plain policy: $(run --s3_allow_server_credentials_in_user_queries=0 -q "CREATE TABLE change_policy (x UInt8) ENGINE = MergeTree ORDER BY x SETTINGS storage_policy = '__${PLAIN_DISK}'")"
echo "select credential policy by ALTER: $(run --s3_allow_server_credentials_in_user_queries=0 -q "ALTER TABLE change_policy MODIFY SETTING storage_policy = '__${SERVER_DISK}'")"

${CLICKHOUSE_KEEPER_CLIENT} -q "rmr '${ZK_PATH}'"
