#!/usr/bin/env python3
"""Build the requested public fork commit; never substitute a packaged binary."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys

SOURCE = Path("/work/source")
BUILD = Path("/work/build")
OUTPUT = Path("/fork-output")
GIB = 1024 ** 3


def limits():
    memory = os.sysconf("SC_PAGE_SIZE") * os.sysconf("SC_PHYS_PAGES")
    for filename in ["/sys/fs/cgroup/memory.max", "/sys/fs/cgroup/memory/memory.limit_in_bytes"]:
        path = Path(filename)
        if path.exists():
            value = path.read_text().strip()
            if value.isdigit():
                memory = min(memory, int(value))
    cpus = len(os.sched_getaffinity(0))
    quota = Path("/sys/fs/cgroup/cpu.max")
    if quota.exists():
        value, period = quota.read_text().split()
        if value != "max":
            cpus = min(cpus, max(1, int(value) // int(period)))
    else:
        quota = Path("/sys/fs/cgroup/cpu/cpu.cfs_quota_us")
        period = Path("/sys/fs/cgroup/cpu/cpu.cfs_period_us")
        if quota.exists() and period.exists() and int(quota.read_text()) > 0:
            cpus = min(cpus, max(1, int(quota.read_text()) // int(period.read_text())))
    return memory, cpus


def run(command, log):
    with log.open("w") as output:
        result = subprocess.run(command, cwd=SOURCE, stdout=output, stderr=subprocess.STDOUT)
    if result.returncode:
        with log.open() as output:
            shutil.copyfileobj(output, sys.stderr)
        raise SystemExit(result.returncode)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--revision", required=True)
    revision = parser.parse_args().revision
    if not re.fullmatch(r"[0-9a-f]{40}", revision):
        raise SystemExit("REVISION must be the exact 40-character source commit")
    OUTPUT.mkdir(parents=True, exist_ok=True)
    (OUTPUT / "binary").mkdir(exist_ok=True)
    metadata = OUTPUT / "metadata"
    metadata.mkdir(exist_ok=True)
    memory, cpus = limits()
    disk = shutil.disk_usage("/").free
    resources = {"memory_bytes": memory, "cpus": cpus, "free_disk_bytes": disk}
    print(json.dumps(resources), flush=True)
    if memory < 8 * GIB or cpus < 4 or disk < 80 * GIB:
        raise SystemExit(
            "ClickHouse source builder needs at least 4 CPUs, 8 GiB memory and 80 GiB free disk for this configuration. "
            "Request a suitable image-builder allocation from Test Infrastructure; do not bypass resource allowlists."
        )
    for command in ["git", "cmake", "ninja", "clang-22", "clang++-22"]:
        if not shutil.which(command):
            raise SystemExit("Missing required build tool: " + command)
    SOURCE.mkdir(parents=True, exist_ok=True)
    subprocess.run(["git", "init", "--quiet", str(SOURCE)], check=True)
    subprocess.run(["git", "-C", str(SOURCE), "remote", "add", "origin", "https://github.com/Shopify/ClickHouse.git"], check=True)
    subprocess.run(["git", "-C", str(SOURCE), "fetch", "--depth=1", "origin", revision], check=True)
    subprocess.run(["git", "-C", str(SOURCE), "checkout", "--detach", "FETCH_HEAD"], check=True)
    actual = subprocess.check_output(["git", "-C", str(SOURCE), "rev-parse", "HEAD"], text=True).strip()
    if actual != revision:
        raise SystemExit("Source checkout does not match REVISION")
    subprocess.run(["git", "-C", str(SOURCE), "submodule", "update", "--init", "--recursive", "--depth=1"], check=True)
    jobs = max(1, min(8, cpus, memory // (3 * GIB)))
    BUILD.mkdir(parents=True, exist_ok=True)
    run([
        "cmake", "-S", str(SOURCE), "-B", str(BUILD), "-G", "Ninja",
        "-DCMAKE_BUILD_TYPE=Release", "-DENABLE_THINLTO=OFF", "-DDISABLE_ALL_DEBUG_SYMBOLS=ON",
        "-DCMAKE_C_COMPILER=clang-22", "-DCMAKE_CXX_COMPILER=clang++-22", "-DCOMPILER_CACHE=disabled",
        "-DCMAKE_TOOLCHAIN_FILE=" + str(SOURCE / "cmake/linux/toolchain-x86_64.cmake"),
        "-DENABLE_TESTS=OFF", "-DENABLE_UTILS=OFF", "-DENABLE_CLICKHOUSE_SELF_EXTRACTING=OFF",
        "-DPARALLEL_COMPILE_JOBS=" + str(jobs), "-DPARALLEL_LINK_JOBS=1",
    ], BUILD / "cmake.log")
    run(["ninja", "-C", str(BUILD), "clickhouse"], BUILD / "ninja.log")
    for name in ["cmake.log", "ninja.log"]:
        shutil.copy2(BUILD / name, metadata / name)
    binary = BUILD / "programs/clickhouse"
    if not binary.is_file() or not os.access(binary, os.X_OK):
        raise SystemExit("Source build did not produce an executable clickhouse binary")
    destination = OUTPUT / "binary/clickhouse"
    shutil.copy2(binary, destination)
    checksum = hashlib.sha256()
    with destination.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            checksum.update(chunk)
    digest = checksum.hexdigest()
    version = subprocess.check_output([str(destination), "--version"], text=True).strip()
    (metadata / "manifest.json").write_text(json.dumps({
        "source_repository": "https://github.com/Shopify/ClickHouse",
        "source_revision": revision, "binary_sha256": digest, "binary_version": version,
        "resources": resources, "compile_jobs": jobs,
        "thin_lto": False, "debug_symbols": False,
    }, indent=2) + "\n")
    print("Built source revision " + revision + ", binary sha256 " + digest, flush=True)


if __name__ == "__main__":
    main()
