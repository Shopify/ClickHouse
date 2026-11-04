#!/usr/bin/env python3
"""Exercise the source-built candidate in disposable local processes and CI MinIO."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import socket
import subprocess
import tarfile
import time
import urllib.request
from urllib.parse import urlparse
from xml.sax.saxutils import escape

BINARY = Path("/usr/bin/clickhouse")
META = Path("/opt/fork-ci")
OUTPUT = Path("artifacts/fork-ci").resolve()
REGRESSIONS = [
    "05494_custom_disk_identity_without_credentials_marker",
    "05496_dynamic_disk_wrapper_of_server_credentials_disk",
    "05497_dynamic_disk_credential_selection",
]


def checksum(path, algorithm="sha256"):
    digest = hashlib.new(algorithm)
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def port():
    with socket.socket() as sock:
        sock.bind(("127.0.0.1", 0))
        return sock.getsockname()[1]


def wait_for(check, seconds=60):
    deadline = time.monotonic() + seconds
    last = None
    while time.monotonic() < deadline:
        try:
            return check()
        except (RuntimeError, subprocess.SubprocessError) as error:
            last = error
            time.sleep(0.5)
    raise RuntimeError("Readiness check failed: " + str(last))


class Server:
    def __init__(self, binary, directory, opt_in=False, keeper_port=None):
        self.binary = Path(binary)
        self.directory = directory
        directory.mkdir(parents=True, exist_ok=True)
        self.tcp = port()
        self.http = port()
        self.process = None
        self.stream = None
        self.config = directory / "server.xml"
        for name in ["data", "tmp", "user_files", "schemas", "caches"]:
            (directory / name).mkdir(exist_ok=True)
        settings = "<s3_allow_server_credentials_in_user_queries>1</s3_allow_server_credentials_in_user_queries>" if opt_in else ""
        (directory / "users.xml").write_text("""<clickhouse><profiles><default>
            <max_memory_usage>805306368</max_memory_usage><max_threads>2</max_threads>
            <dynamic_disk_allow_from_env>1</dynamic_disk_allow_from_env>
            <dynamic_disk_allow_from_zk>1</dynamic_disk_allow_from_zk>""" + settings + """
            </default></profiles><users><default><password></password>
            <networks><ip>127.0.0.1</ip></networks><profile>default</profile><quota>default</quota>
            </default></users><quotas><default/></quotas></clickhouse>""")
        zk = "" if keeper_port is None else f"<zookeeper><node><host>127.0.0.1</host><port>{keeper_port}</port></node></zookeeper>"
        self.config.write_text(f"""<clickhouse><logger><level>warning</level>
            <log>{escape(str(directory / 'server.log'))}</log><errorlog>{escape(str(directory / 'server-error.log'))}</errorlog>
            </logger><listen_host>127.0.0.1</listen_host><tcp_port>{self.tcp}</tcp_port><http_port>{self.http}</http_port>
            <path>{escape(str(directory / 'data'))}/</path><tmp_path>{escape(str(directory / 'tmp'))}/</tmp_path>
            <user_files_path>{escape(str(directory / 'user_files'))}/</user_files_path>
            <format_schema_path>{escape(str(directory / 'schemas'))}/</format_schema_path>
            <users_config>{escape(str(directory / 'users.xml'))}</users_config><default_profile>default</default_profile>
            <default_database>default</default_database><max_server_memory_usage>1610612736</max_server_memory_usage>
            <mark_cache_size>16777216</mark_cache_size><uncompressed_cache_size>0</uncompressed_cache_size>
            <custom_local_disks_base_directory>{escape(str(directory / 'data'))}/</custom_local_disks_base_directory>
            <custom_cached_disks_base_directory>{escape(str(directory / 'caches'))}/</custom_cached_disks_base_directory>{zk}</clickhouse>""")

    def start(self):
        self.stream = (self.directory / "process.log").open("a")
        self.process = subprocess.Popen([str(self.binary), "server", "--config-file", str(self.config)], stdout=self.stream, stderr=self.stream)
        try:
            wait_for(lambda: self.query("SELECT 1", timeout=5))
        except BaseException:
            self.stop()
            raise
        return self

    def query(self, sql, timeout=90):
        result = subprocess.run([str(self.binary), "client", "--host", "127.0.0.1", "--port", str(self.tcp), "--multiquery", "--query", sql], capture_output=True, text=True, timeout=timeout)
        if result.returncode:
            raise RuntimeError(result.stderr)
        return result.stdout

    def stop(self):
        if self.process:
            self.process.terminate()
            try:
                self.process.wait(timeout=30)
            except subprocess.TimeoutExpired:
                self.process.kill()
                self.process.wait(timeout=10)
            self.process = None
        if self.stream:
            self.stream.close()
            self.stream = None


def verify_provenance():
    manifest = json.loads((META / "manifest.json").read_text())
    expected = os.environ.get("BUILDKITE_COMMIT", "")
    if not expected or manifest["source_revision"] != expected:
        raise RuntimeError("Candidate source revision does not match the Buildkite commit")
    if checksum(BINARY) != manifest["binary_sha256"]:
        raise RuntimeError("Runtime image does not contain the compiled candidate binary")
    for name in ["manifest.json", "cmake.log", "ninja.log"]:
        shutil.copy2(META / name, OUTPUT / name)
    return manifest


def declared_regressions():
    root = Path("tests/queries/0_stateless")
    available = [name for name in REGRESSIONS if (root / (name + ".sh")).is_file()]
    fork_fixture = Path("tests/integration/test_dynamic_disk_identity_upgrade/test.py")
    if fork_fixture.is_file() and len(available) != len(REGRESSIONS):
        raise RuntimeError("Declared fork upgrade suite is incomplete; refusing to skip missing tests")
    for name in available:
        if not (root / (name + ".reference")).is_file():
            raise RuntimeError("Missing required golden reference: " + name)
    return available


def smoke():
    server = Server(BINARY, OUTPUT / "smoke").start()
    try:
        result = server.query("CREATE TABLE check_data (x UInt64) ENGINE=MergeTree ORDER BY x; INSERT INTO check_data VALUES (1),(2),(3); SELECT count(),sum(x) FROM check_data")
        if result != "3\t6\n":
            raise RuntimeError("Source-built server/client data smoke failed")
        result = subprocess.check_output([str(BINARY), "local", "--tmp", "--query", "SELECT sum(number) FROM numbers(10)"], text=True)
        if result != "45\n":
            raise RuntimeError("Source-built local smoke failed")
    finally:
        server.stop()


def functional():
    regressions = declared_regressions()
    if not regressions:
        return {"declared_suite": False, "executed": [], "reason": "No disk-identity suite is declared on this branch; server/client/local smoke still ran"}
    directory = OUTPUT / "keeper"
    directory.mkdir(exist_ok=True)
    keeper_port, raft_port = port(), port()
    config = directory / "keeper.xml"
    config.write_text(f"""<clickhouse><logger><level>warning</level></logger><listen_host>127.0.0.1</listen_host>
        <keeper_server><tcp_port>{keeper_port}</tcp_port><server_id>1</server_id>
        <log_storage_path>{directory}/log/</log_storage_path><snapshot_storage_path>{directory}/snapshots/</snapshot_storage_path>
        <raft_configuration><server><id>1</id><hostname>127.0.0.1</hostname><port>{raft_port}</port></server></raft_configuration>
        </keeper_server></clickhouse>""")
    with (directory / "keeper.log").open("w") as log:
        keeper = subprocess.Popen([str(BINARY), "keeper", "--config-file", str(config)], stdout=log, stderr=log)
        server = Server(BINARY, OUTPUT / "functional", keeper_port=keeper_port)
        try:
            def keeper_ready():
                subprocess.run([str(BINARY), "keeper-client", "--port", str(keeper_port), "--query", "ls /"], capture_output=True, check=True, timeout=5)
            wait_for(keeper_ready)
            subprocess.run([str(BINARY), "keeper-client", "--port", str(keeper_port), "--query", "create '/clickhouse' ''"], capture_output=True, check=True, timeout=10)
            server.start()
            for name in regressions:
                database = "fork_" + name[:5]
                server.query("CREATE DATABASE " + database)
                env = dict(os.environ, CLICKHOUSE_BINARY=str(BINARY), CLICKHOUSE_DATABASE=database,
                    CLICKHOUSE_HOST="127.0.0.1", CLICKHOUSE_PORT_TCP=str(server.tcp), CLICKHOUSE_PORT_HTTP=str(server.http),
                    CLICKHOUSE_PORT_KEEPER=str(keeper_port), CLICKHOUSE_CONFIG=str(server.config),
                    CLICKHOUSE_TMP=str(OUTPUT / name), CLICKHOUSE_CLIENT_SERVER_LOGS_LEVEL="error")
                script = Path("tests/queries/0_stateless") / (name + ".sh")
                with (OUTPUT / (name + ".stdout.log")).open("w") as out, (OUTPUT / (name + ".stderr.log")).open("w") as err:
                    result = subprocess.run(["bash", str(script)], env=env, stdout=out, stderr=err, timeout=600)
                actual = (OUTPUT / (name + ".stdout.log")).read_text().strip()
                expected = script.with_suffix(".reference").read_text().strip()
                if result.returncode or actual != expected:
                    raise RuntimeError("Functional regression failed: " + name + "; see retained stdout/stderr logs")
        finally:
            server.stop()
            keeper.terminate()
            try:
                keeper.wait(timeout=30)
            except subprocess.TimeoutExpired:
                keeper.kill()
                keeper.wait(timeout=10)
    return {"declared_suite": True, "executed": regressions}


def disk(endpoint_var="FORK_MINIO_ENDPOINT", reader=False):
    value = "disk(type='s3_plain_rewritable', endpoint='from_env " + endpoint_var + "', access_key_id='from_env FORK_MINIO_KEY', secret_access_key='from_env FORK_MINIO_SECRET'"
    if reader:
        value += ", readonly=true"
    return value + ")"


def s3_tables(server, prefix="", wider=False):
    columns = "x UInt64, y String" if wider else "x UInt64"
    writer, reader = disk(), disk(reader=True)
    cache = "disk(name='fork_reader_cache',type='cache',path='" + str(server.directory / "caches/reader") + "',max_size='64Mi',disk=" + reader + ")"
    for name, value in [("w", writer), ("r", cache)]:
        server.query(f"CREATE TABLE IF NOT EXISTS d.{prefix}{name} ({columns}) ENGINE=MergeTree ORDER BY x SETTINGS table_disk=true,refresh_parts_interval=1,disk={value}")


def assert_rows(server, table, expected):
    def check():
        actual = server.query("SELECT count(),sum(x) FROM " + table)
        if actual != expected:
            raise RuntimeError("Unexpected synthetic fixture rows in " + table)
    wait_for(check)


def old_binary():
    lock = json.loads(Path(".shopify-build/fixtures.json").read_text())["legacy"]
    parsed = urlparse(lock["url"])
    if parsed.scheme != "https" or parsed.hostname != "packages.clickhouse.com" or len(lock["sha512"]) != 128:
        raise RuntimeError("Legacy fixture must be a checksum-pinned official package, not a candidate substitute")
    path = OUTPUT / "legacy.tgz"
    with urllib.request.urlopen(lock["url"], timeout=60) as response, path.open("wb") as out:
        shutil.copyfileobj(response, out)
    if checksum(path, "sha512") != lock["sha512"]:
        raise RuntimeError("Legacy fixture archive checksum mismatch")
    binary = OUTPUT / "legacy-clickhouse"
    with tarfile.open(path) as archive:
        member = archive.getmember(lock["member"])
        if not member.isfile():
            raise RuntimeError("Legacy fixture member is not a regular file")
        with archive.extractfile(member) as source, binary.open("wb") as out:
            shutil.copyfileobj(source, out)
    binary.chmod(0o755)
    return binary


def minio():
    endpoint = os.environ["FORK_MINIO_ENDPOINT"]
    parsed = urlparse(endpoint)
    if parsed.hostname != "minio" or parsed.port != 9000 or parsed.scheme != "http":
        raise RuntimeError("This suite may use only its isolated CI MinIO service")
    needs_upgrade = REGRESSIONS[0] in declared_regressions()
    binary = old_binary() if needs_upgrade else BINARY
    directory = OUTPUT / "s3-server"
    server = Server(binary, directory, opt_in=not needs_upgrade)
    try:
        server.start()
        server.query("CREATE DATABASE d ENGINE=Atomic")
        wait_for(lambda: s3_tables(server))
        server.query("INSERT INTO d.w VALUES (1),(2),(3)")
        assert_rows(server, "d.r", "3\t6\n")
        identity_sql = "SELECT name,storage_policy,data_paths FROM system.tables WHERE database='d' AND name IN ('r','w') ORDER BY name"
        before = server.query(identity_sql)
        if needs_upgrade:
            server.stop()
            server = Server(BINARY, directory, opt_in=True).start()
            if server.query(identity_sql) != before:
                raise RuntimeError("Upgrade changed legacy disk identities or paths")
            s3_tables(server, prefix="wide_", wider=True)
            for name in ["w", "r"]:
                assert_rows(server, "d.wide_" + name, "3\t6\n")
                server.query(f"EXCHANGE TABLES d.{name} AND d.wide_{name}; DETACH TABLE d.wide_{name} PERMANENTLY")
            server.query("INSERT INTO d.w VALUES (4,'new')")
            assert_rows(server, "d.r", "4\t10\n")
            server.stop()
            server = Server(BINARY, directory, opt_in=True).start()
            if server.query(identity_sql) != before:
                raise RuntimeError("Post-widening restart changed legacy identities or paths")
            assert_rows(server, "d.w", "4\t10\n")
            assert_rows(server, "d.r", "4\t10\n")
            if server.query("SELECT x,y FROM d.r ORDER BY x") != "1\t\n2\t\n3\t\n4\tnew\n":
                raise RuntimeError("Legacy upgrade did not preserve old/new column values")
        return {"fresh_s3_smoke": True, "legacy_upgrade_executed": needs_upgrade}
    finally:
        server.stop()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("suite", choices=["smoke", "functional", "minio"])
    suite = parser.parse_args().suite
    OUTPUT.mkdir(parents=True, exist_ok=True)
    result = {"suite": suite, "passed": False}
    try:
        verify_provenance()
        if suite == "smoke":
            smoke()
            details = {"server_client_local": True}
        elif suite == "functional":
            smoke()
            details = functional()
        else:
            details = minio()
        result.update(passed=True, details=details)
        print(json.dumps(result), flush=True)
    except BaseException as error:
        result["error"] = str(error)
        raise
    finally:
        (OUTPUT / (suite + "-result.json")).write_text(json.dumps(result, indent=2) + "\n")
        # Keep fixture logs/data, not duplicate downloaded executables in CI artifacts.
        for name in ["legacy.tgz", "legacy-clickhouse"]:
            path = OUTPUT / name
            if path.is_file():
                path.unlink()


if __name__ == "__main__":
    main()
