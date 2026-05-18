#!/usr/bin/env bash
set -euo pipefail

NS="${AR_K8S_NAMESPACE:-ch-builder}"
POD="${AR_K8S_POD:-clickhouse-builder-0}"
RUN_ID="${AR_RUN_ID:-$(date -u +%Y%m%dT%H%M%SZ)}"
REMOTE_ROOT="/work/ch-dev"
BUILD_LOG="build/autoresearch_build_${RUN_ID}.log"
READ_DB="${AR_READ_DB:-p04_real_copy_readsrc_20260518T221002Z}"

copy_source_file() {
    local path="$1"
    kubectl cp -n "$NS" -c builder "$path" "$POD:$REMOTE_ROOT/$path" >/dev/null
}

for path in \
    src/IO/GCS/GCSXMLClient.cpp \
    src/IO/GCS/GCSXMLClient.h \
    src/Disks/DiskObjectStorage/ObjectStorages/GCS/GCSObjectStorage.cpp \
    src/Disks/DiskObjectStorage/ObjectStorages/GCS/GCSObjectStorage.h \
    src/IO/S3/PocoHTTPClient.cpp \
    src/IO/S3/PocoHTTPClient.h \
    src/IO/WriteBufferFromS3.cpp \
    src/Common/HTTPConnectionPool.cpp \
    src/Common/BufferAllocationPolicy.cpp
do
    copy_source_file "$path"
done

kubectl exec -n "$NS" "$POD" -- bash -lc "cd $REMOTE_ROOT && ninja -C build clickhouse > $BUILD_LOG 2>&1 && /work/gcs-grpc-testing/start_server.sh >/work/ch-dev/build/autoresearch_restart_${RUN_ID}.log 2>&1"

kubectl exec -i -n "$NS" "$POD" -- bash -s -- "$RUN_ID" "$READ_DB" <<'REMOTE'
set -euo pipefail

RUN_ID="$1"
READ_DB="$2"
source /work/gcs-grpc-testing/env.sh
cd /work/ch-dev
CLIENT="${P06_CLICKHOUSE_CLIENT:-/work/ch-dev/build/programs/clickhouse-client}"
OUT_DIR="/work/results/autoresearch_gcs_rw_${RUN_ID}"
mkdir -p "$OUT_DIR"
TIMINGS="$OUT_DIR/timings.tsv"
CHECKS="$OUT_DIR/checks.tsv"
printf "phase\tvariant\tseconds\n" >"$TIMINGS"
printf "phase\tvariant\tstatus\tdetail\n" >"$CHECKS"

client() {
    "$CLIENT" "$@"
}

sql_quote() {
    local value="$1"
    value=${value//\\/\\\\}
    value=${value//\'/\'\'}
    printf "'%s'" "$value"
}

drop_caches() {
    client --query "SYSTEM DROP MARK CACHE" >/dev/null 2>&1 || true
    client --query "SYSTEM DROP UNCOMPRESSED CACHE" >/dev/null 2>&1 || true
    client --query "SYSTEM DROP FILESYSTEM CACHE" >/dev/null 2>&1 || true
}

measure_query() {
    local phase="$1"
    local variant="$2"
    local query="$3"
    shift 3
    local log_file="$OUT_DIR/${phase}_${variant}.log"
    local seconds
    client --time "$@" --query "$query" >"$log_file" 2>&1
    seconds=$(awk 'BEGIN{s=""} /^[0-9]+([.][0-9]+)?$/ {s=$1} END{print s}' "$log_file")
    if [[ -z "$seconds" ]]; then
        echo "failed to parse timing for $phase $variant" >&2
        cat "$log_file" >&2
        exit 2
    fi
    printf "%s\t%s\t%s\n" "$phase" "$variant" "$seconds" >>"$TIMINGS"
}

native_disk_def() {
    local prefix="$1"
    printf "disk(type=object_storage, object_storage_type='gcs', bucket=%s, key_prefix=%s, endpoint=%s, request_timeout_ms=120000)" \
        "$(sql_quote "$P06_BUCKET")" \
        "$(sql_quote "$prefix")" \
        "$(sql_quote "${P06_NATIVE_ENDPOINT:-google-c2p:///storage.googleapis.com}")"
}

xml_disk_def() {
    local prefix="$1"
    printf "disk(type=object_storage, object_storage_type='gcs', bucket=%s, key_prefix=%s, endpoint=%s, request_timeout_ms=120000, write_transport='xml_multipart')" \
        "$(sql_quote "$P06_BUCKET")" \
        "$(sql_quote "$prefix")" \
        "$(sql_quote "${P06_NATIVE_ENDPOINT:-google-c2p:///storage.googleapis.com}")"
}

variant_disk_def() {
    local variant="$1"
    local prefix="$2"
    case "$variant" in
        native) native_disk_def "$prefix/native" ;;
        xml|xml_64m20) xml_disk_def "$prefix/$variant" ;;
        *) echo "unknown variant $variant" >&2; exit 2 ;;
    esac
}

variant_args() {
    local variant="$1"
    case "$variant" in
        xml_64m20)
            printf '%s\n' \
                "--s3_strict_upload_part_size=67108864" \
                "--s3_max_single_part_upload_size=67108864" \
                "--s3_max_inflight_parts_for_one_file=20"
            ;;
    esac
}

create_target_sql() {
    local source="$1"
    local target="$2"
    local disk_def="$3"
    local show_create
    show_create="$(client --query "SHOW CREATE TABLE $source FORMAT TSVRaw")"
    SHOW_CREATE="$show_create" TARGET_TABLE="$target" DISK_DEF="$disk_def" python3 - <<'PY'
import os
import re
sql = os.environ['SHOW_CREATE'].strip()
target = os.environ['TARGET_TABLE']
disk = os.environ['DISK_DEF']
sql = re.sub(r'^CREATE TABLE\s+[^\n]+', f'CREATE TABLE {target}', sql, count=1)
settings_re = re.compile(r'\nSETTINGS\s+(.+)\s*$', re.S)
match = settings_re.search(sql)
if match:
    sql = settings_re.sub('\nSETTINGS ' + match.group(1).strip() + ', disk = ' + disk, sql)
else:
    sql += '\nSETTINGS disk = ' + disk
print(sql)
PY
}

ensure_small_source() {
    local exists
    exists="$(client --query "EXISTS TABLE autoresearch_src.small_many_files FORMAT TSVRaw")"
    if [[ "$exists" == "1" ]]; then
        return
    fi
    python3 - <<'PY' >"/work/ch-dev/build/autoresearch_create_small_source.sql"
cols = 96
print('CREATE DATABASE IF NOT EXISTS autoresearch_src;')
print('DROP TABLE IF EXISTS autoresearch_src.small_many_files SYNC;')
print('CREATE TABLE autoresearch_src.small_many_files')
print('(')
print('    k UInt64 CODEC(NONE),')
for i in range(cols):
    comma = ',' if i + 1 < cols else ''
    print(f'    c{i:03d} String CODEC(NONE){comma}')
print(')')
print('ENGINE = MergeTree')
print('ORDER BY k')
print('SETTINGS min_bytes_for_wide_part = 0, min_rows_for_wide_part = 0, index_granularity = 8192;')
print('INSERT INTO autoresearch_src.small_many_files')
select = ['number AS k'] + [f'hex(sipHash128(number, {i + 1})) AS c{i:03d}' for i in range(cols)]
print('SELECT')
print('    ' + ',\n    '.join(select))
print('FROM numbers(50000)')
print('SETTINGS max_insert_block_size = 50000, use_query_cache = 0;')
print('OPTIMIZE TABLE autoresearch_src.small_many_files FINAL;')
PY
    client --multiquery <"/work/ch-dev/build/autoresearch_create_small_source.sql" >"$OUT_DIR/create_small_source.log" 2>&1
}

small_read_sql() {
    local fqn="$1"
    python3 - "$fqn" <<'PY'
import sys
fqn = sys.argv[1]
expr = ' + '.join([f'length(c{i:03d})' for i in range(96)])
print(f"SELECT sum({expr}) FROM {fqn} SETTINGS use_query_cache=0, use_uncompressed_cache=0, enable_filesystem_cache=0, use_page_cache_for_object_storage=0, max_threads=4")
PY
}

real_read_sql() {
    local fqn="$1"
    cat <<SQL
SELECT
    sum(length(message))
    + sum(length(\`attrs.url.query\`))
    + sum(arraySum(arrayMap(x -> length(x), mapValues(json_attributes))))
    + sum(arraySum(arrayMap(x -> length(x), mapValues(string_attributes))))
    + sum(arraySum(arrayMap(arr -> arraySum(arrayMap(x -> length(x), arr)), mapValues(string_array_attributes))))
FROM $fqn
SETTINGS use_query_cache=0, use_uncompressed_cache=0, enable_filesystem_cache=0, use_page_cache_for_object_storage=0, max_threads=4
SQL
}

verify_target() {
    local phase="$1"
    local variant="$2"
    local db="$3"
    local table="$4"
    local expected_rows="$5"
    local rows parts
    read -r rows parts < <(client --query "SELECT sum(rows), count() FROM system.parts WHERE database='$db' AND table='$table' AND active FORMAT TSVRaw")
    if [[ "$rows" == "$expected_rows" && "$parts" == "1" ]]; then
        printf "%s\t%s\tpass\trows=%s parts=%s\n" "$phase" "$variant" "$rows" "$parts" >>"$CHECKS"
    else
        printf "%s\t%s\tfail\trows=%s parts=%s\n" "$phase" "$variant" "${rows:-absent}" "${parts:-absent}" >>"$CHECKS"
        exit 3
    fi
}

ensure_small_source
BENCH_DB="ar_gcs_rw_${RUN_ID}"
client --query "DROP DATABASE IF EXISTS $BENCH_DB SYNC" >/dev/null 2>&1 || true
client --query "CREATE DATABASE $BENCH_DB"
PREFIX="${P06_OBJECT_PREFIX%/}/autoresearch-gcs-rw/${RUN_ID}"
variants=(xml xml_64m20 native)

for variant in "${variants[@]}"; do
    disk_def="$(variant_disk_def "$variant" "$PREFIX/real_write")"
    target="$BENCH_DB.real_${variant}"
    create_target_sql "p04_real_part.core_n4_v0_base" "$target" "$disk_def" >"$OUT_DIR/create_real_${variant}.sql"
    client --multiquery <"$OUT_DIR/create_real_${variant}.sql" >"$OUT_DIR/create_real_${variant}.log" 2>&1
    mapfile -t args < <(variant_args "$variant")
    measure_query real_write "$variant" "ALTER TABLE $target ATTACH PARTITION ID '1778709600' FROM p04_real_part.core_n4_v0_base" "${args[@]}"
    verify_target real_write "$variant" "$BENCH_DB" "real_${variant}" 40000000
done

for variant in "${variants[@]}"; do
    drop_caches
    measure_query real_read "$variant" "$(real_read_sql "$READ_DB.${variant}_1")"
done

for variant in "${variants[@]}"; do
    disk_def="$(variant_disk_def "$variant" "$PREFIX/small_write")"
    target="$BENCH_DB.small_${variant}"
    create_target_sql "autoresearch_src.small_many_files" "$target" "$disk_def" >"$OUT_DIR/create_small_${variant}.sql"
    client --multiquery <"$OUT_DIR/create_small_${variant}.sql" >"$OUT_DIR/create_small_${variant}.log" 2>&1
    mapfile -t args < <(variant_args "$variant")
    measure_query small_write "$variant" "ALTER TABLE $target ATTACH PARTITION tuple() FROM autoresearch_src.small_many_files" "${args[@]}"
    verify_target small_write "$variant" "$BENCH_DB" "small_${variant}" 50000
done

for variant in "${variants[@]}"; do
    drop_caches
    measure_query small_read "$variant" "$(small_read_sql "$BENCH_DB.small_${variant}")"
done

client --query "DROP DATABASE IF EXISTS $BENCH_DB SYNC" >/dev/null 2>&1 || true

python3 - "$TIMINGS" <<'PY'
import csv
import sys
from collections import defaultdict
path = sys.argv[1]
rows = list(csv.DictReader(open(path), delimiter='\t'))
phase_totals = defaultdict(float)
variant_totals = defaultdict(float)
total = 0.0
for row in rows:
    sec = float(row['seconds'])
    total += sec
    phase_totals[row['phase']] += sec
    variant_totals[row['variant']] += sec
print(f"METRIC total_seconds={total:.6f}")
for key, value in sorted(phase_totals.items()):
    print(f"METRIC {key}_seconds={value:.6f}")
for key, value in sorted(variant_totals.items()):
    print(f"METRIC {key}_seconds={value:.6f}")
for row in rows:
    print(f"DETAIL {row['phase']} {row['variant']} {float(row['seconds']):.6f}")
PY
REMOTE
