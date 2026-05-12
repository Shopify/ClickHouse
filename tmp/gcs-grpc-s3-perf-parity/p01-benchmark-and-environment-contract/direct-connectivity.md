# Native GCS endpoint and direct-connectivity evidence

Generated: 2026-05-12 23:30:49 UTC

## Classification

Direct-connectivity status: diagnostic-only. The table definition hides the bucket and endpoint in `SHOW CREATE TABLE`, and the remote container did not provide enough non-secret bucket/endpoint detail to run a definitive Google direct-connectivity diagnostic.

## System disks

```text
Defaulted container "builder" out of: builder, bootstrap (init)
name	path	type	object_storage_type	metadata_type	is_remote	is_broken	cache_path
__tmp_internal_187639134332364974146593625578718342589	/work/ch-dev/build/programs/disks/__tmp_internal_187639134332364974146593625578718342589/	ObjectStorage	S3	Local	1	0	
__tmp_internal_47314414007917353450675846413377023341	/work/ch-dev/build/programs/disks/__tmp_internal_47314414007917353450675846413377023341/	ObjectStorage	GCS	Local	1	0	
default	/work/ch-dev/build/programs/	Local	None	None	0	0	
```

## Table disk definitions

```sql
Defaulted container "builder" out of: builder, bootstrap (init)
CREATE TABLE p06_gcs_20260512T174206Z.native_read\n(\n    `d` Date,\n    `k` UInt64 CODEC(NONE),\n    `g` UInt32 CODEC(NONE),\n    `payload` String CODEC(NONE)\n)\nENGINE = MergeTree\nPARTITION BY tuple()\nORDER BY (g, k)\nSETTINGS disk = disk(type = object_storage, object_storage_type = \'[HIDDEN]\', bucket = \'[HIDDEN]\', key_prefix = \'[HIDDEN]\', endpoint = \'[HIDDEN]\', request_timeout_ms = \'[HIDDEN]\'), min_bytes_for_wide_part = 0, index_granularity = 8192
CREATE TABLE p06_gcs_20260512T174206Z.s3_read\n(\n    `d` Date,\n    `k` UInt64 CODEC(NONE),\n    `g` UInt32 CODEC(NONE),\n    `payload` String CODEC(NONE)\n)\nENGINE = MergeTree\nPARTITION BY tuple()\nORDER BY (g, k)\nSETTINGS disk = disk(type = s3, endpoint = \'[HIDDEN]\', use_environment_credentials = \'[HIDDEN]\', support_batch_delete = \'[HIDDEN]\', request_timeout_ms = \'[HIDDEN]\'), min_bytes_for_wide_part = 0, index_granularity = 8192
```

## Relevant settings

```text
Defaulted container "builder" out of: builder, bootstrap (init)
name	value	changed
allow_experimental_s3queue	1	0
allow_prefetched_read_pool_for_remote_filesystem	1	0
backup_restore_s3_retry_attempts	1000	0
backup_restore_s3_retry_initial_backoff_ms	25	0
backup_restore_s3_retry_jitter_factor	0.1	0
backup_restore_s3_retry_max_backoff_ms	5000	0
backup_slow_all_threads_after_retryable_s3_error	0	0
compatibility_s3_presigned_url_query_in_path	0	0
enable_s3_requests_logging	0	0
merge_tree_min_bytes_for_concurrent_read_for_remote_filesystem	0	0
merge_tree_min_rows_for_concurrent_read_for_remote_filesystem	0	0
remote_filesystem_read_method	threadpool	0
remote_filesystem_read_prefetch	1	0
s3_allow_multipart_copy	1	0
s3_allow_parallel_part_upload	1	0
s3_check_objects_after_upload	0	0
s3_connect_timeout_ms	1000	0
s3_create_new_file_on_insert	0	0
s3_disable_checksum	0	0
s3_ignore_file_doesnt_exist	0	0
s3_list_object_keys_size	1000	0
s3_max_connections	1024	0
s3_max_get_burst	0	0
s3_max_get_rps	0	0
s3_max_inflight_parts_for_one_file	20	0
s3_max_part_number	10000	0
s3_max_put_burst	0	0
s3_max_put_rps	0	0
s3_max_redirects	10	0
s3_max_single_operation_copy_size	33554432	0
s3_max_single_part_upload_size	33554432	0
s3_max_single_read_retries	4	0
s3_max_unexpected_write_error_retries	4	0
s3_max_upload_part_size	5368709120	0
s3_min_upload_part_size	16777216	0
s3_path_filter_limit	1000	0
s3_request_timeout_ms	30000	0
s3_retry_attempts	500	0
s3_skip_empty_files	1	0
s3_slow_all_threads_after_network_error	1	0
s3_slow_all_threads_after_retryable_error	0	0
s3_strict_upload_part_size	0	0
s3_throw_on_zero_files_match	0	0
s3_truncate_on_insert	0	0
s3_upload_part_size_multiply_factor	2	0
s3_upload_part_size_multiply_parts_count_threshold	500	0
s3_uri_style	auto	0
s3_use_adaptive_timeouts	1	0
s3_validate_request_settings	1	0
s3queue_allow_experimental_sharded_mode	0	0
s3queue_default_zookeeper_path	/clickhouse/s3queue/	0
s3queue_enable_logging_to_s3queue_log	0	0
s3queue_keeper_fault_injection_probability	0	0
s3queue_migrate_old_metadata_to_buckets	0	0
schema_inference_use_cache_for_s3	1	0
```

## Cloud metadata probe

```text
Defaulted container "builder" out of: builder, bootstrap (init)
projects/196567729986/zones/us-central1-c
```

## Direct-connectivity diagnostic attempt

```text
Defaulted container "builder" out of: builder, bootstrap (init)
gcloud storage diagnose unavailable
```

## P01 impact

Because DirectPath status is not proven, P01 benchmark results are treated as diagnostic-only for broad GCS gRPC performance claims. They remain useful for comparing this exact remote setup and for downstream phase acceptance after the same environment contract is reused.
