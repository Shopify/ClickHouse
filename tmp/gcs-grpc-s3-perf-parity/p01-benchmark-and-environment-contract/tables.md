# Benchmark table contract

Generated: 2026-05-12 23:29:52 UTC

Database: `p06_gcs_20260512T174206Z`
Tables: `native_read`, `s3_read`

## Existence

```text
Defaulted container "builder" out of: builder, bootstrap (init)
1
1
```

## Table metadata

```text
Defaulted container "builder" out of: builder, bootstrap (init)
database	name	engine	total_rows	total_bytes
p06_gcs_20260512T174206Z	native_read	MergeTree	5000000	746505033
p06_gcs_20260512T174206Z	s3_read	MergeTree	5000000	746505033
```

## Active parts

```text
Defaulted container "builder" out of: builder, bootstrap (init)
table	active_parts	rows	bytes_on_disk	readable_bytes_on_disk
native_read	5	5000000	746505033	711.92 MiB
s3_read	5	5000000	746505033	711.92 MiB
```

## Row count validation

```text
Defaulted container "builder" out of: builder, bootstrap (init)
table	rows
s3_read	5000000
native_read	5000000
```

## SHOW CREATE TABLE `p06_gcs_20260512T174206Z.native_read`

```sql
Defaulted container "builder" out of: builder, bootstrap (init)
CREATE TABLE p06_gcs_20260512T174206Z.native_read\n(\n    `d` Date,\n    `k` UInt64 CODEC(NONE),\n    `g` UInt32 CODEC(NONE),\n    `payload` String CODEC(NONE)\n)\nENGINE = MergeTree\nPARTITION BY tuple()\nORDER BY (g, k)\nSETTINGS disk = disk(type = object_storage, object_storage_type = \'[HIDDEN]\', bucket = \'[HIDDEN]\', key_prefix = \'[HIDDEN]\', endpoint = \'[HIDDEN]\', request_timeout_ms = \'[HIDDEN]\'), min_bytes_for_wide_part = 0, index_granularity = 8192
```

## SHOW CREATE TABLE `p06_gcs_20260512T174206Z.s3_read`

```sql
Defaulted container "builder" out of: builder, bootstrap (init)
CREATE TABLE p06_gcs_20260512T174206Z.s3_read\n(\n    `d` Date,\n    `k` UInt64 CODEC(NONE),\n    `g` UInt32 CODEC(NONE),\n    `payload` String CODEC(NONE)\n)\nENGINE = MergeTree\nPARTITION BY tuple()\nORDER BY (g, k)\nSETTINGS disk = disk(type = s3, endpoint = \'[HIDDEN]\', use_environment_credentials = \'[HIDDEN]\', support_batch_delete = \'[HIDDEN]\', request_timeout_ms = \'[HIDDEN]\'), min_bytes_for_wide_part = 0, index_granularity = 8192
```

