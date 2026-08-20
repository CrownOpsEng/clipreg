# Version and release policy

## Version authority

`VERSION` is the sole base SemVer authority. The C binary receives its version from the build, not from a second hard-coded constant.

For a clean checkout exactly at tag `v$VERSION`, `clipreg --version` reports exactly `VERSION`. Untagged Git builds append SemVer build metadata (`+g<short-commit>` and `.dirty` when applicable), so PR/main builds are identifiable without changing `VERSION` on every commit.

Release tooling rejects tag/version drift.

## Pre-1.0 semantics

ClipReg is in initial development. SemVer major zero permits incompatible changes, but changes should still be deliberate and documented because users may already depend on commands, config, register data, and deployment behavior.

## Promotion states

The prerelease identifier is an engineering state, not a counter for every merged PR.

### Beta

A beta is an installable architecture/function checkpoint. Known release blockers may remain if they are explicit and the release is marked prerelease.

Current native checkpoint: `v0.2.0-beta.1`.

### Release candidate

An RC means the team believes the exact candidate could become `v0.2.0` without code changes if qualification finds no blocker. Do not label known-broken code as an RC merely because it is not final.

Before `v0.2.0-rc.1`:

- the v0.2 milestone has no open `priority:blocker` issues;
- no known daemon liveness, data-loss/corruption, security, migration, or install blocker remains;
- `make check` is green;
- live COSMIC qualification is recorded in the release-prep PR;
- upgrade/migration from the supported prior line is checked;
- no feature work is intentionally queued for v0.2.0.

If code changes after an RC, the change lands normally through PR. Cut another RC only when a new distributable candidate is useful; do not create an RC tag for every patch/PR.

### Final

`v0.2.0` promotes a qualified RC. If executable behavior changes after the last RC, cut another RC first. The final promotion should otherwise be version/changelog/release metadata only.

## Development between releases

Changes land on protected `main` through PRs. CI proves every PR/main commit. Developers/testers install directly from a branch/source checkout when evaluating an unreleased fix; Git build metadata identifies the exact binary.

Tags are reserved for meaningful published checkpoints. Published tags/releases are immutable.

## Release procedure

1. Open a release-prep PR that updates `VERSION` and `CHANGELOG.md` to the intended beta/RC/final version and records qualification evidence/open items.
2. Run `make check` and the required live qualification for the promotion level.
3. Merge through normal protected-branch rules.
4. Run `scripts/release.sh --version <version> --plan` from the exact merged commit.
5. Apply the release only after reviewing the plan.
6. Publish an annotated `v<version>` tag and GitHub release; prerelease identifiers are published with GitHub's prerelease flag.

Do not force-move a release tag or replace published release contents in place.
