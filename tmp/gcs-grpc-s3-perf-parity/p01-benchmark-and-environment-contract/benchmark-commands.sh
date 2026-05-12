#!/usr/bin/env bash
set -uo pipefail

OUT_DIR="${OUT_DIR:-tmp/gcs-grpc-s3-perf-parity/p01-benchmark-and-environment-contract}"
LOG_DIR="$OUT_DIR/logs"
mkdir -p "$LOG_DIR"

KUBECTL=(kubectl exec -n ch-builder clickhouse-builder-0 --)
CH=/work/ch-dev/build/programs/clickhouse
DB=p06_gcs_20260512T174206Z
REPEATS=${REPEATS:-3}

run_case() {
    local label="$1"
    local table="$2"
    local method="$3"
    local i
    for i in $(seq 1 "$REPEATS"); do
        local log="$LOG_DIR/${label}_${method}_run${i}.log"
        echo "== ${label} method=${method} run=${i} log=${log}"
        "${KUBECTL[@]}" bash -lc "$CH client --print-profile-events --time --query \"SELECT * FROM $DB.$table SETTINGS remote_filesystem_read_method='$method' FORMAT Null\"" >"$log" 2>&1
        local rc=$?
        echo "exit_code=${rc}" >>"$log"
        if [[ $rc -ne 0 ]]; then
            echo "case ${label}/${method}/run${i} failed with exit code ${rc}; see ${log}" >&2
        fi
    done
}

run_case native native_read threadpool
run_case s3 s3_read threadpool
run_case native native_read read
run_case s3 s3_read read
