# `MakeGrpcClient` experiment branch

## Branch

- Branch name: `gcs-grpc-make-grpc-client-experiment`
- Base commit: `046405713d7`
- Main working branch at creation: `gcs-grpc-coherent`

## Creation command

```bash
git worktree add -b gcs-grpc-make-grpc-client-experiment \
  tmp/gcs-grpc-s3-perf-parity/p02-client-architecture-comparison/make-grpc-client-worktree \
  HEAD
```

Result:

```text
Preparing worktree (new branch 'gcs-grpc-make-grpc-client-experiment')
HEAD is now at 046405713d7 roundrobin stub and fix offset read
```

## Worktree safety handling

- The experiment was isolated in a separate worktree under `tmp/gcs-grpc-s3-perf-parity/p02-client-architecture-comparison/make-grpc-client-worktree`.
- Pre-existing main-worktree submodule state in `contrib/liburing` and `contrib/sysroot` was not touched.
- Pre-existing plan/P01 note updates remained in the main worktree and were not copied into the experiment branch.
- No experiment changes were merged into the main production path during P02.

## Cleanup note

The temporary worktree is evidence for branch creation, but it is not intended to be committed. The branch itself remains available as `gcs-grpc-make-grpc-client-experiment`.

## Temporary worktree removal

After the source/API blocker was documented, the temporary worktree was removed to avoid leaving a large untracked checkout inside the main repository. The branch `gcs-grpc-make-grpc-client-experiment` remains.

Command:

```bash
git worktree remove tmp/gcs-grpc-s3-perf-parity/p02-client-architecture-comparison/make-grpc-client-worktree
```
