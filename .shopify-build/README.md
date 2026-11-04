# Shopify fork CI

The `clickhouse` Buildkite pipeline builds the exact source commit, packages its binary as a development Docker image, and tests that binary. It does not deploy anything or produce production/release tags.

## Pipeline

1. Shopify Build builds `.shopify-build/Dockerfile` for `linux/amd64`.
2. The compile stage fetches the public `Shopify/ClickHouse` commit supplied by Shopify Build's `REVISION` build argument. It verifies `HEAD`, initializes recursive submodules, and runs CMake and Ninja. It does not download a candidate ClickHouse binary.
3. The runtime image copies only the compiled binary and build evidence. Installation uses that binary's `install` command, with no package-repository fallback.
4. Test jobs compare the image manifest and binary SHA256 with `BUILDKITE_COMMIT`, then run server/client/local SQL smoke checks and S3 tests against an isolated MinIO service.
5. Declared disk-identity regression scripts run against the candidate with a disposable Keeper/server. A branch containing the fork's upgrade integration fixture must contain the complete required functional suite; a missing script/reference is an error.
6. When the legacy identity suite is declared, the MinIO job creates populated writer/reader tables with the checksum-pinned26.3.33.24 fixture, stops it, starts the candidate on the same directory, widens bindings, exchanges names, permanently detaches old bindings without dropping them, inserts a new-column value, and restarts again. Identity, paths and synthetic values must remain correct. The old download is a test fixture, never the image's candidate binary.

A branch without those regression files still runs SQL and fresh S3 smoke checks. Its result explicitly says the upgrade suite was not declared; it is not reported as an upgrade-test pass.

## Images and evidence

The container definition uses `environment: dev`, `application: clickhouse-fork`, no `latest` tag and no versioned branches. Shopify Build owns registry authentication and the resulting commit-SHA tag. The intended development image location is:

```text
us-docker.pkg.dev/shopify-docker-images/containers/apps/dev/clickhouse-fork:<commit>
```

Verify the actual pushed digest in Buildkite before using an image. Image existence alone is not a test pass or deployment approval.

Each test job retains `artifacts/fork-ci` with the binary/source manifest, CMake/Ninja logs, native server/Keeper logs, functional stdout/stderr and a result JSON. Fixture tarballs/executables are removed before upload. The data under that directory is synthetic; no production credentials are used. Artifact upload is required.

Base/builder/MinIO images are pinned by digest. The legacy fixture is pinned by SHA512 in `fixtures.json`.

## Builder capacity

The compile stage requires at least4CPUs,8GiB effective memory and80GiB free disk before downloading submodules. These are minimum configuration gates, not a measured performance guarantee. It disables ThinLTO/debug symbols, limits compiler jobs from the effective CPU/memory quota and permits one linker job. It does not pass `-j` or use `nproc` with Ninja.

Ordinary Shopify Build run jobs default to2CPU/5GB. Large run-job classes and high concurrency require Test Infrastructure approval; this repository was not in the inspected allowlists. This pipeline does not bypass those restrictions. Compilation uses the Docker image-builder path. If that allocation fails the preflight or the full build is too slow, request a suitable image-builder allocation from Test Infrastructure. A repository configuration change cannot grant that capacity.

Dockerfile builds are an exception to the standard dynamic-container path; coordinate with Test Infrastructure as required by Shopify Build's Dockerfile documentation. No runner approval or allocation is claimed by this PR.

## Enabling and adopting CI

Spy created the pipeline and starter PR13. Commit-status publication is intentionally disabled during setup. After reviewing a successful build, Sharad can enable it through Spy; the agent does not post to Slack. Verify PR builds and branch filters before adding a required merge gate.

PR13 targets `master`. Backport PR12 targets `26.8`, whose checkout must also contain the pipeline files. After this CI change is reviewed/merged, port the configuration to the release branch through a separate direct-base change. Do not rebase the source fix onto unrelated master changes or assume a master-only workflow makes the release-branch build available.

Do not disable the inherited upstream GitHub workflows, create new runner infrastructure, change registry permissions, publish production images, or merge/deploy from this pipeline without separate approval.

## Validation status

The initial implementation is not a successful build claim. Static checks and a Buildkite run must be recorded separately. A build/resource/test failure must remain red; no test or build step is soft-failed. Buildkite CLI authentication and any required infrastructure approval are operational prerequisites outside this source configuration.
